/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/ezcapture
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#include "bm_audio_capture_pin.h"

 // audio is limited to 48kHz and an audio packet is only delivered with a video frame
 // lowest fps is 23.976 so the max no of samples should be 48000/(24000/1001) = 2002
 // but there can be backlogs so allow for a few frames for safety
constexpr uint16_t maxSamplesPerFrame = 8192;

blackmagic_audio_capture_pin::blackmagic_audio_capture_pin(HRESULT* phr, blackmagic_capture_filter* pParent, bool pPreview) :
	hdmi_audio_capture_pin(
		phr,
		pParent,
		pPreview ? "AudioPreview" : "AudioCapture",
		pPreview ? L"AudioPreview" : L"AudioCapture",
		pPreview ? "AudioPreview" : "AudioCapture"
	)
{
}

blackmagic_audio_capture_pin::~blackmagic_audio_capture_pin()
= default;

HRESULT blackmagic_audio_capture_pin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
	REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hasFrame = false;
	auto retVal = S_FALSE;
	auto handle = mFilter->GetAudioFrameHandle();
	// keep going til we have a frame to process
	while (!hasFrame)
	{
		if (CheckStreamState(nullptr) == STREAM_DISCARDING)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream is discarding", mLogData.prefix);
			#endif

			break;
		}

		if (IsStopped())
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream has not started, retry after backoff", mLogData.prefix);
			#endif

			BACKOFF;
			continue;
		}

		// grab next frame 
		DWORD dwRet = WaitForSingleObject(handle, 1000);

		// unknown, try again
		if (dwRet == WAIT_FAILED)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Wait for frame failed, retrying", mLogData.prefix);
			#endif
			continue;
		}

		if (dwRet == WAIT_OBJECT_0)
		{
			mCurrentFrame = mFilter->GetAudioFrame();
			auto newAudioFormat = mCurrentFrame->GetFormat();
			if (newAudioFormat.outputChannelCount == 0)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] No output channels in signal, retry after backoff",
					mLogData.prefix);
				#endif

				mSinceLast = 0;

				BACKOFF;
				continue;
			}

			// detect format changes
			if (ShouldChangeMediaType(&newAudioFormat))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] AudioFormat changed! Attempting to reconnect", mLogData.prefix);
				#endif

				CMediaType proposedMediaType(m_mt);
				AudioFormatToMediaType(&proposedMediaType, &newAudioFormat);
				auto hr = DoChangeMediaType(&proposedMediaType, &newAudioFormat);
				if (FAILED(hr))
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
						"[{}] AudioFormat changed but not able to reconnect ({:#08x}) retry after backoff",
						mLogData.prefix, static_cast<unsigned long>(hr));
					#endif

					// TODO communicate that we need to change somehow
					BACKOFF;
					continue;
				}

				mFilter->OnAudioFormatLoaded(&mAudioFormat);
			}

			retVal = audio_capture_pin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);

			if (SUCCEEDED(retVal))
			{
				hasFrame = true;
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
					"[{}] Audio frame buffered but unable to get delivery buffer, retry after backoff",
					mLogData.prefix);
				#endif
			}
		}
		if (!hasFrame)
			SHORT_BACKOFF;
	}
	return retVal;
}

HRESULT blackmagic_audio_capture_pin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] blackmagic_audio_capture_pin::OnThreadCreate", mLogData.prefix);
	#endif

	return mFilter->PinThreadCreated();
}

HRESULT blackmagic_audio_capture_pin::FillBuffer(IMediaSample* pms)
{
	auto retVal = S_OK;

	if (CheckStreamState(nullptr) == STREAM_DISCARDING)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] Stream is discarding", mLogData.prefix);
		#endif

		return S_FALSE;
	}

	BYTE* pmsData;
	pms->GetPointer(&pmsData);

	auto gap = mCurrentFrame->GetFrameIndex() - mFrameCounter;
	mFrameCounter = mCurrentFrame->GetFrameIndex();

	long size = mCurrentFrame->GetLength();
	long maxSize = pms->GetSize();
	if (size > maxSize)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Audio frame is larger than expected {} vs {}", mLogData.prefix, size,
			maxSize);
		#endif
	}
	long actualSize = std::min(size, maxSize);

	auto endTime = mCurrentFrame->GetFrameTime();
	auto sampleLength = mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	auto sampleCount = size / sampleLength;
	auto frameDuration = mAudioFormat.sampleInterval * sampleCount;
	auto startTime = std::max(endTime - static_cast<int64_t>(frameDuration), 0LL);

	if (mAudioFormat.outputChannelCount == 2)
	{
		memcpy(pmsData, mCurrentFrame->GetData(), actualSize);
	}
	else
	{
		const uint16_t* inputSamples = static_cast<const uint16_t*>(mCurrentFrame->GetData());
		uint16_t* outputSamples = reinterpret_cast<uint16_t*>(pmsData);
		auto inputSampleCount = mCurrentFrame->GetLength() / sizeof(uint16_t);
		auto i = 0;
		#ifdef __AVX2__
		if (mAudioFormat.outputChannelCount == 8 || mAudioFormat.outputChannelCount == 4)
		{
			auto chunks = inputSampleCount / 16;
			const __m256i shuffle_mask = mAudioFormat.outputChannelCount == 8
				?
				_mm256_setr_epi8(
					0, 1, 2, 3, 6, 7, 4, 5, 12, 13, 14, 15, 8, 9, 10, 11,
					0, 1, 2, 3, 6, 7, 4, 5, 12, 13, 14, 15, 8, 9, 10, 11
				)
				:
				_mm256_setr_epi8(
					0, 1, 2, 3, 6, 7, 4, 5, 8, 9, 10, 11, 14, 15, 12, 13,
					0, 1, 2, 3, 6, 7, 4, 5, 8, 9, 10, 11, 14, 15, 12, 13
				)
				;
			for (auto j = 0; j < chunks; j++)
			{
				__m256i samples = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(inputSamples + i));
				__m256i shuffled = _mm256_shuffle_epi8(samples, shuffle_mask);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(outputSamples + i), shuffled);
				i += 16;
			}
		}
		else if (mAudioFormat.outputChannelCount == 6)
		{
			// ignore, alignment issues in writing 96 bytes from each lane, maybe use sse instead?
		}
		#endif
		// input is 1 2 3 4 5 6 7 8 repeating but have to swap 3 and 4 & 5/6 7/8
		if (mAudioFormat.outputChannelCount == 8)
		{
			for (; i < inputSampleCount - 7; i += mAudioFormat.outputChannelCount)
			{
				outputSamples[i] = inputSamples[i]; // L
				outputSamples[i + 1] = inputSamples[i + 1]; // R
				outputSamples[i + 2] = inputSamples[i + 3]; // C
				outputSamples[i + 3] = inputSamples[i + 2]; // LFE
				outputSamples[i + 4] = inputSamples[i + 6]; // SL
				outputSamples[i + 5] = inputSamples[i + 7]; // SR
				outputSamples[i + 6] = inputSamples[i + 4]; // BL
				outputSamples[i + 7] = inputSamples[i + 5]; // BR
			}
		}
		else if (mAudioFormat.outputChannelCount == 6)
		{
			for (; i < inputSampleCount - 5; i += mAudioFormat.outputChannelCount)
			{
				outputSamples[i] = inputSamples[i];
				outputSamples[i + 1] = inputSamples[i + 1];
				outputSamples[i + 2] = inputSamples[i + 3];
				outputSamples[i + 3] = inputSamples[i + 2];
				outputSamples[i + 4] = inputSamples[i + 4];
				outputSamples[i + 5] = inputSamples[i + 5];
			}
		}
		else if (mAudioFormat.outputChannelCount == 4)
		{
			for (; i < inputSampleCount - 3; i += mAudioFormat.outputChannelCount)
			{
				outputSamples[i] = inputSamples[i];
				outputSamples[i + 1] = inputSamples[i + 1];
				outputSamples[i + 2] = inputSamples[i + 3];
				outputSamples[i + 3] = inputSamples[i + 2];
			}
		}
	}

	pms->SetTime(&startTime, &endTime);
	pms->SetSyncPoint(true);
	pms->SetDiscontinuity(gap != 1);
	pms->SetActualDataLength(actualSize);

	mPreviousFrameTime = mCurrentFrameTime;
	mCurrentFrameTime = endTime;

	REFERENCE_TIME now;
	mFilter->GetReferenceTime(&now);
	auto capLat = now - mCurrentFrame->GetCaptureTime();
	RecordLatency();

	#ifndef NO_QUILL
	if (!mLoggedLatencyHeader)
	{
		LOG_TRACE_L1(mLogData.audioLat,
			"codec,idx,lat,"
			"pt,st,et,"
			"ct,delta,len,"
			"count,gap");
		mLoggedLatencyHeader = true;
	}
	LOG_TRACE_L1(mLogData.audioLat,
		"{},{},{},"
		"{},{},{},"
		"{},{},{},"
		"{},{}",
		codecNames[mAudioFormat.codec], mFrameCounter, capLat,
		mPreviousFrameTime, startTime, mCurrentFrameTime,
		now, mCurrentFrameTime - mPreviousFrameTime, mCurrentFrame->GetLength(),
		sampleCount, gap);
	#endif

	if (mUpdatedMediaType)
	{
		CMediaType cmt(m_mt);
		AM_MEDIA_TYPE* sendMediaType = CreateMediaType(&cmt);
		pms->SetMediaType(sendMediaType);
		DeleteMediaType(sendMediaType);
		mUpdatedMediaType = false;
	}
	if (S_FALSE == HandleStreamStateChange(pms))
	{
		retVal = S_FALSE;
	}
	return retVal;
}

HRESULT blackmagic_audio_capture_pin::DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] Proposing new audio format Fs: {} Bits: {} Channels: {} Codec: {}",
		mLogData.prefix,
		newAudioFormat->fs, newAudioFormat->bitDepth, newAudioFormat->outputChannelCount,
		codecNames[newAudioFormat->codec]);
	#endif
	long newSize = maxSamplesPerFrame * newAudioFormat->bitDepthInBytes * newAudioFormat->outputChannelCount;
	long oldSize = maxSamplesPerFrame * mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	auto shouldRenegotiateOnQueryAccept = newSize != oldSize || mAudioFormat.codec != newAudioFormat->codec;
	auto retVal = RenegotiateMediaType(pmt, newSize, shouldRenegotiateOnQueryAccept);
	if (retVal == S_OK)
	{
		mAudioFormat = *newAudioFormat;
	}
	return retVal;
}

bool blackmagic_audio_capture_pin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
{
	pProperties->cbBuffer = maxSamplesPerFrame * mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	if (pProperties->cBuffers < 1)
	{
		pProperties->cBuffers = 16;
		return false;
	}
	return true;
}

void blackmagic_audio_capture_pin::DoThreadDestroy()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] blackmagic_audio_capture_pin::DoThreadDestroy", mLogData.prefix);
	#endif

	mFilter->PinThreadDestroyed();
}

