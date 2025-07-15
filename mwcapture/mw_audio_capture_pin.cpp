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
#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "mw_audio_capture_pin.h"

#define S_PARTIAL_DATABURST    ((HRESULT)2L)
#define S_POSSIBLE_BITSTREAM    ((HRESULT)3L)
#define S_NO_CHANNELS    ((HRESULT)2L)

constexpr auto bitstreamDetectionWindowSecs = 0.075;
constexpr auto bitstreamDetectionRetryAfter = 1.0 / bitstreamDetectionWindowSecs;
constexpr auto bitstreamBufferSize = 6144;

magewell_audio_capture_pin::audio_capture::audio_capture(magewell_audio_capture_pin* pin, HCHANNEL hChannel) :
	pin(pin),
	mLogData(pin->mLogData)
{
	#ifndef NO_QUILL
	int64_t now;
	pin->GetReferenceTime(&now);

	LOG_INFO(mLogData.logger, "[{}] MWCreateAudioCapture {} Hz {} bits {} channels at {}", mLogData.prefix,
		pin->mAudioFormat.fs, pin->mAudioFormat.bitDepth, pin->mAudioFormat.inputChannelCount, now);
	#endif

	mEvent = MWCreateAudioCapture(hChannel, MWCAP_AUDIO_CAPTURE_NODE_EMBEDDED_CAPTURE, pin->mAudioFormat.fs,
	                              pin->mAudioFormat.bitDepth, pin->mAudioFormat.inputChannelCount, CaptureFrame, pin);
	if (mEvent == nullptr)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] MWCreateAudioCapture failed {} Hz {} bits {} channels", mLogData.prefix,
		          pin->mAudioFormat.fs, pin->mAudioFormat.bitDepth, pin->mAudioFormat.inputChannelCount);
		#endif
	}
}

magewell_audio_capture_pin::audio_capture::~audio_capture()
{
	if (mEvent != nullptr)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] AudioCapture", mLogData.prefix);
		#endif

		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] Ready to MWDestoryAudioCapture", mLogData.prefix);
		#endif

		const auto hr = MWDestoryAudioCapture(mEvent);
		mEvent = nullptr;

		#ifndef NO_QUILL
		if (MW_SUCCEEDED == hr)
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] MWDestoryAudioCapture complete", mLogData.prefix);
		}
		else
		{
			LOG_WARNING(mLogData.logger, "[{}] MWDestoryAudioCapture failed", mLogData.prefix);
		}
		#endif
	}
}

//////////////////////////////////////////////////////////////////////////
// magewell_audio_capture_pin
//////////////////////////////////////////////////////////////////////////
magewell_audio_capture_pin::magewell_audio_capture_pin(HRESULT* phr, magewell_capture_filter* pParent, bool pPreview) :
	hdmi_audio_capture_pin(
		phr,
		pParent,
		pPreview ? "AudioPreview" : "AudioCapture",
		pPreview ? L"AudioPreview" : L"AudioCapture",
		pPreview ? "AudioPreview" : "AudioCapture",
		pParent->GetDeviceType()
	),
	mNotify(nullptr),
	mCaptureEvent(nullptr),
	mNotifyEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	mDataBurstBuffer(bitstreamBufferSize)
// initialise to a reasonable default size that is not wastefully large but also is unlikely to need to be expanded very often
{
	mCapturedFrame.data = new uint8_t[maxFrameLengthInBytes];
	mCapturedFrame.length = maxFrameLengthInBytes;

	mDataBurstBuffer.assign(bitstreamBufferSize, 0);
	DWORD dwInputCount = 0;
	auto hChannel = pParent->GetChannelHandle();
	auto hr = MWGetAudioInputSourceArray(hChannel, nullptr, &dwInputCount);
	if (hr != MW_SUCCEEDED)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] MWGetAudioInputSourceArray", mLogData.prefix);
		#endif
	}

	if (dwInputCount == 0)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] No audio signal detected", mLogData.prefix);
		#endif
	}
	else
	{
		auto hr = LoadSignal(&hChannel);
		mFilter->OnAudioSignalLoaded(&mAudioSignal);
		if (hr == S_OK)
		{
			LoadFormat(&mAudioFormat, &mAudioSignal);
			mFilter->OnAudioFormatLoaded(&mAudioFormat);
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to load audio signal", mLogData.prefix);
			#endif
		}
	}

	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] Audio Status Fs: {} Bits: {} Channels: {} Codec: {}", mLogData.prefix,
	            mAudioFormat.fs,
	            mAudioFormat.bitDepth, mAudioFormat.outputChannelCount, codecNames[mAudioFormat.codec]);
	#endif

	#if defined RECORD_ENCODED || defined RECORD_RAW
	time_t timeNow = time(nullptr);
	struct tm* tmLocal;
	tmLocal = localtime(&timeNow);

	#ifdef RECORD_ENCODED
	strcpy_s(mEncodedInFileName, std::filesystem::temp_directory_path().string().c_str());
	CHAR encodedInFileName[128];
	sprintf_s(encodedInFileName, "\\%s-%d-%02d-%02d-%02d-%02d-%02d.encin",
		pPreview ? "audio_prev" : "audio_cap", tmLocal->tm_year + 1900, tmLocal->tm_mon + 1, tmLocal->tm_mday, tmLocal->tm_hour, tmLocal->tm_min, tmLocal->tm_sec);
	strcat_s(mEncodedInFileName, encodedInFileName);

	if (fopen_s(&mEncodedInFile, mEncodedInFileName, "wb") != 0)
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to open {}", mLogData.prefix, mEncodedInFileName);
	}

	strcpy_s(mEncodedOutFileName, std::filesystem::temp_directory_path().string().c_str());
	CHAR encodedOutFileName[128];
	sprintf_s(encodedOutFileName, "\\%s-%d-%02d-%02d-%02d-%02d-%02d.encout",
		pPreview ? "audio_prev" : "audio_cap", tmLocal->tm_year + 1900, tmLocal->tm_mon + 1, tmLocal->tm_mday, tmLocal->tm_hour, tmLocal->tm_min, tmLocal->tm_sec);
	strcat_s(mEncodedOutFileName, encodedOutFileName);

	if (fopen_s(&mEncodedOutFile, mEncodedOutFileName, "wb") != 0)
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to open {}", mLogData.prefix, mEncodedOutFileName);
	}
	#endif

	#ifdef RECORD_RAW
	strcpy_s(mRawFileName, std::filesystem::temp_directory_path().string().c_str());
	CHAR rawFileName[128];
	sprintf_s(rawFileName, "\\%s-%d-%02d-%02d-%02d-%02d-%02d.raw",
		pPreview ? "audio_prev" : "audio_cap", tmLocal->tm_year + 1900, tmLocal->tm_mon + 1, tmLocal->tm_mday, tmLocal->tm_hour, tmLocal->tm_min, tmLocal->tm_sec);
	strcat_s(mRawFileName, rawFileName);

	if (fopen_s(&mRawFile, mRawFileName, "wb") != 0)
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to open {}", mLogData.prefix, mRawFileName);
	}
	#endif

	#endif
}

magewell_audio_capture_pin::~magewell_audio_capture_pin()
{
	CloseHandle(mNotifyEvent);
	#ifdef RECORD_ENCODED
	if (0 != fclose(mEncodedInFile))
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to close {}", mLogData.prefix, mEncodedInFileName);
	}
	if (0 != fclose(mEncodedOutFile))
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to close {}", mLogData.prefix, mEncodedOutFileName);
	}
	#endif

	#ifdef RECORD_RAW
	if (0 != fclose(mRawFile))
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to close {}", mLogData.prefix, mRawFileName);
	}
	#endif
}

HRESULT magewell_audio_capture_pin::OnThreadCreate()
{
	hdmi_audio_capture_pin::OnThreadCreate();
	bool enabled;
	mFilter->IsHighThreadPriorityEnabled(&enabled);
	if (enabled)
	{
		BumpThreadPriority();
	}

	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] magewell_audio_capture_pin::OnThreadCreate", mLogData.prefix);
	#endif

	memset(mCompressedBuffer, 0, sizeof(mCompressedBuffer));

	auto hChannel = mFilter->GetChannelHandle();
	LoadSignal(&hChannel);
	mFilter->OnAudioSignalLoaded(&mAudioSignal);

	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == MW_PRO)
	{
		if (MWStartAudioCapture(hChannel) != MW_SUCCEEDED)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] magewell_audio_capture_pin::OnThreadCreate Unable to MWStartAudioCapture",
			          mLogData.prefix);
			#endif
		}

		// register for signal change events & audio buffered
		mNotify = MWRegisterNotify(hChannel, mNotifyEvent,
		                           MWCAP_NOTIFY_AUDIO_INPUT_SOURCE_CHANGE | MWCAP_NOTIFY_AUDIO_SIGNAL_CHANGE |
		                           MWCAP_NOTIFY_AUDIO_FRAME_BUFFERED);
		if (!mNotify)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] magewell_audio_capture_pin::OnThreadCreate Unable to MWRegistryNotify",
			          mLogData.prefix);
			#endif
		}
	}
	else
	{
		mAudioCapture = std::make_unique<audio_capture>(this, mFilter->GetChannelHandle());
	}
	return NOERROR;
}

void magewell_audio_capture_pin::DoThreadDestroy()
{
	if (mNotify)
	{
		MWUnregisterNotify(mFilter->GetChannelHandle(), mNotify);
	}
	StopCapture();
	if (mCaptureEvent)
	{
		CloseHandle(mCaptureEvent);
	}
}

HRESULT magewell_audio_capture_pin::LoadSignal(HCHANNEL* hChannel)
{
	auto hr = MWGetAudioSignalStatus(*hChannel, &mAudioSignal.signalStatus);
	if (MW_SUCCEEDED != hr)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] LoadSignal MWGetAudioSignalStatus", mLogData.prefix);
		#endif
		return S_FALSE;
	}

	MWCAP_INPUT_SPECIFIC_STATUS status;
	hr = MWGetInputSpecificStatus(*hChannel, &status);
	if (hr == MW_SUCCEEDED)
	{
		DWORD tPdwValidFlag = 0;
		if (!status.bValid)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] MWGetInputSpecificStatus is invalid", mLogData.prefix);
			#endif
		}
		else if (status.dwVideoInputType != MWCAP_VIDEO_INPUT_TYPE_HDMI)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Video input type is not HDMI {}", mLogData.prefix,
			          status.dwVideoInputType);
			#endif
		}
		else
		{
			if (MW_SUCCEEDED != MWGetHDMIInfoFrameValidFlag(*hChannel, &tPdwValidFlag))
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Unable to detect HDMI InfoFrame", mLogData.prefix);
				#endif
			}
			else if (tPdwValidFlag & MWCAP_HDMI_INFOFRAME_MASK_AUDIO)
			{
				HDMI_INFOFRAME_PACKET pkt;
				MWGetHDMIInfoFramePacket(*hChannel, MWCAP_HDMI_INFOFRAME_ID_AUDIO, &pkt);
				mAudioSignal.audioInfo = pkt.audioInfoFramePayload;
			}
			else
			{
				mAudioSignal.audioInfo = {};
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] No HDMI Audio infoframe detected", mLogData.prefix);
				#endif
				return S_FALSE;
			}
		}
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] LoadSignal MWGetInputSpecificStatus", mLogData.prefix);
		#endif
		return S_FALSE;
	}

	if (mAudioSignal.signalStatus.wChannelValid == 0)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] No valid audio channels detected {}", mLogData.prefix,
		             mAudioSignal.signalStatus.wChannelValid);
		#endif
		return S_NO_CHANNELS;
	}
	return S_OK;
}

void magewell_audio_capture_pin::CaptureFrame(const BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam)
{
	magewell_audio_capture_pin* pin = static_cast<magewell_audio_capture_pin*>(pParam);

	if (cbFrame == 0)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L2(pin->mLogData.logger, "[{}] Ignoring zero length frame at {}", pin->mLogData.prefix, u64TimeStamp);
		#endif

		return;
	}

	CAutoLock lck(&pin->mCaptureCritSec);
	memcpy(pin->mCapturedFrame.data, pbFrame, cbFrame);
	pin->mCapturedFrame.length = cbFrame;

	// u64TimeStamp is time since capture started so have to add the StreamStartTime for compatibility
	auto ts = u64TimeStamp + pin->mStreamStartTime;
	pin->mCapturedFrame.ts = ts;
	pin->mFrameTs.snap(ts, BUFFERING);

	int64_t now;
	pin->GetReferenceTime(&now);
	pin->mFrameTs.snap(now, READING);

	if (!SetEvent(pin->mNotifyEvent))
	{
		auto err = GetLastError();
		#ifndef NO_QUILL
		LOG_ERROR(pin->mLogData.logger, "[{}] Failed to notify on frame {:#08x}", pin->mLogData.prefix, err);
		#endif
	}
	else
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(pin->mLogData.logger, "[{}] Notifying frame at {} (sz: {}, ts: {}/{})", pin->mLogData.prefix, ts,
		             cbFrame, u64TimeStamp, pin->mFrameTs.get(READING));
		#endif
	}
}

HRESULT magewell_audio_capture_pin::DoChangeMediaType(const CMediaType* pmt, const audio_format* newAudioFormat)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] Proposing new audio format Fs: {} Bits: {} Channels: {} Codec: {}",
	            mLogData.prefix,
	            newAudioFormat->fs, newAudioFormat->bitDepth, newAudioFormat->outputChannelCount,
	            codecNames[newAudioFormat->codec]);
	#endif
	long newSize = MWCAP_AUDIO_SAMPLES_PER_FRAME * newAudioFormat->bitDepthInBytes * newAudioFormat->outputChannelCount;
	if (newAudioFormat->codec != PCM)
	{
		newSize = newAudioFormat->dataBurstSize;
	}
	long oldSize = MWCAP_AUDIO_SAMPLES_PER_FRAME * mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	if (mAudioFormat.codec != PCM)
	{
		oldSize = mAudioFormat.dataBurstSize;
	}
	auto shouldRenegotiateOnQueryAccept = newSize != oldSize || mAudioFormat.codec != newAudioFormat->codec;
	auto retVal = RenegotiateMediaType(pmt, newSize, shouldRenegotiateOnQueryAccept);
	if (retVal == S_OK)
	{
		mAudioFormat = *newAudioFormat;
		if (mFilter->GetDeviceType() != MW_PRO)
		{
			mAudioCapture = std::make_unique<audio_capture>(this, mFilter->GetChannelHandle());
		}
	}
	return retVal;
}

// loops til we have a frame to process, dealing with any mediatype changes as we go and then grabs a buffer once it's time to go
HRESULT magewell_audio_capture_pin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                                      REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hChannel = mFilter->GetChannelHandle();
	auto proDevice = mFilter->GetDeviceType() == MW_PRO;
	auto hasFrame = false;
	auto retVal = S_FALSE;
	int64_t now;
	// keep going til we have a frame to process
	while (!hasFrame)
	{
		auto frameCopied = false;
		if (CheckStreamState(nullptr) == STREAM_DISCARDING)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream is discarding", mLogData.prefix);
			#endif

			mSinceCodecChange = 0;
			break;
		}

		if (IsStopped())
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream has not started, retry after backoff", mLogData.prefix);
			#endif

			mSinceCodecChange = 0;
			BACKOFF;
			continue;
		}

		auto sigLoaded = LoadSignal(&hChannel);

		if (S_OK != sigLoaded)
		{
			if (mSinceCodecChange > 0)
			{
				mFilter->OnAudioSignalLoaded(&mAudioSignal);
			}

			mSinceCodecChange = 0;
			BACKOFF;
			continue;
		}
		if (mAudioSignal.signalStatus.cBitsPerSample == 0)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Reported bit depth is 0, retry after backoff", mLogData.prefix);
			#endif

			if (mSinceCodecChange > 0)
				mFilter->OnAudioSignalLoaded(&mAudioSignal);

			mSinceCodecChange = 0;
			BACKOFF;
			continue;
		}
		if (mAudioSignal.audioInfo.byChannelAllocation > 0x31)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Reported channel allocation is {}, retry after backoff", mLogData.prefix,
			            mAudioSignal.audioInfo.byChannelAllocation);
			#endif

			if (mSinceCodecChange > 0)
				mFilter->OnAudioSignalLoaded(&mAudioSignal);

			mSinceCodecChange = 0;
			BACKOFF;
			continue;
		}

		audio_format newAudioFormat(mAudioFormat);
		LoadFormat(&newAudioFormat, &mAudioSignal);

		if (newAudioFormat.outputChannelCount == 0)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] No output channels in signal, retry after backoff", mLogData.prefix);
			#endif

			if (mSinceCodecChange > 0)
				mFilter->OnAudioSignalLoaded(&mAudioSignal);

			mSinceLast = 0;
			mSinceCodecChange = 0;

			BACKOFF;
			continue;
		}

		// grab next frame
		if (mFrameTs.get(WAITING) == 0LL)
		{
			GetReferenceTime(&now);
			mFrameTs.snap(now, WAITING);
		}
		DWORD dwRet = WaitForSingleObject(mNotifyEvent, 1000);

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
			// TODO magewell SDK bug means audio is always reported as PCM, until fixed allow 6 frames of audio to pass through before declaring it definitely PCM
			// 12 frames is 7680 bytes of 2 channel audio & 30720 of 8 channel which should be more than enough to be sure
			mBitstreamDetectionWindowLength = std::lround(
				bitstreamDetectionWindowSecs / (static_cast<double>(MWCAP_AUDIO_SAMPLES_PER_FRAME) / newAudioFormat.
					fs));
			if (mDetectedCodec != PCM)
			{
				newAudioFormat.codec = mDetectedCodec;
			}

			if (proDevice)
			{
				mStatusBits = 0;
				auto hr = MWGetNotifyStatus(hChannel, mNotify, &mStatusBits);
				if (mStatusBits & MWCAP_NOTIFY_AUDIO_SIGNAL_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Audio signal change, retry after backoff", mLogData.prefix);
					#endif

					if (mSinceCodecChange > 0)
						mFilter->OnAudioSignalLoaded(&mAudioSignal);

					mSinceLast = 0;
					mSinceCodecChange = 0;
					mFrameTs.reset();

					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_AUDIO_INPUT_SOURCE_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Audio input source change, retry after backoff",
					             mLogData.prefix);
					#endif

					if (mSinceCodecChange > 0)
						mFilter->OnAudioSignalLoaded(&mAudioSignal);

					mSinceLast = 0;
					mSinceCodecChange = 0;
					mFrameTs.reset();

					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_AUDIO_FRAME_BUFFERED)
				{
					GetReferenceTime(&now);
					mFrameTs.snap(now, WAIT_COMPLETE);

					hr = MWCaptureAudioFrame(hChannel, &mAudioSignal.frameInfo);
					if (MW_SUCCEEDED == hr)
					{
						frameCopied = true;
						mPreviousFrameTime = mCurrentFrameTime;
						mFrameTs.snap(mAudioSignal.frameInfo.llTimestamp, BUFFERING);
						mCurrentFrameTime = mFrameTs.get(BUFFERING);

						#ifndef NO_QUILL
						LOG_TRACE_L3(mLogData.logger, "[{}] Audio frame buffered and captured at {}", mLogData.prefix,
						             mCurrentFrameTime);
						#endif

						GetReferenceTime(&now);
						mFrameTs.snap(now, READING);

						memcpy(mFrameBuffer, mAudioSignal.frameInfo.adwSamples, maxFrameLengthInBytes);

						GetReferenceTime(&now);
						mFrameTs.snap(now, READ);
					}
					else
					{
						// NB: evidence suggests this is harmless but logging for clarity
						if (mDataBurstSize > 0)
						{
							#ifndef NO_QUILL
							LOG_WARNING(mLogData.logger,
							            "[{}] Audio frame buffered but capture failed ({}), possible packet corruption after {} bytes",
							            mLogData.prefix,
							            static_cast<int>(hr), mDataBurstRead);
							#endif
						}
						else
						{
							#ifndef NO_QUILL
							LOG_WARNING(mLogData.logger, "[{}] Audio frame buffered but capture failed ({}), retrying",
							            mLogData.prefix, static_cast<int>(hr));
							#endif
						}
						mFrameTs.reset();
						continue;
					}
				}
			}
			else
			{
				CAutoLock lck(&mCaptureCritSec);

				frameCopied = true;

				mPreviousFrameTime = mCurrentFrameTime;
				mCurrentFrameTime = mFrameTs.get(BUFFERING);

				#ifndef NO_QUILL
				LOG_TRACE_L3(mLogData.logger, "[{}] Audio frame buffered and captured at {}", mLogData.prefix,
				             mCurrentFrameTime);
				#endif

				GetReferenceTime(&now);
				mFrameTs.snap(now, READING);

				memcpy(mFrameBuffer, mCapturedFrame.data, mCapturedFrame.length);

				GetReferenceTime(&now);
				mFrameTs.snap(now, READ);
			}
		}

		if (frameCopied)
		{
			mFrameCounter++;

			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] Reading frame {}", mLogData.prefix, mFrameCounter);
			#endif

			#ifdef RECORD_RAW
			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] raw,{},{}", mLogData.prefix, mFrameCounter, maxFrameLengthInBytes);
			#endif
			fwrite(mFrameBuffer, maxFrameLengthInBytes, 1, mRawFile);
			#endif

			codec* detectedCodec = &newAudioFormat.codec;
			const auto mightBeBitstream = newAudioFormat.fs >= 48000 && mSinceLast < mBitstreamDetectionWindowLength;
			const auto examineBitstream = newAudioFormat.codec != PCM || mightBeBitstream || mDataBurstSize > 0;
			if (examineBitstream)
			{
				#ifndef NO_QUILL
				if (!mProbeOnTimer && newAudioFormat.codec == PCM)
				{
					LOG_TRACE_L2(mLogData.logger,
					             "[{}] Bitstream probe in frame {} - {} {} Hz (since: {} len: {} burst: {})",
					             mLogData.prefix, mFrameCounter,
					             codecNames[newAudioFormat.codec], newAudioFormat.fs, mSinceLast,
					             mBitstreamDetectionWindowLength, mDataBurstSize);
				}
				#endif

				CopyToBitstreamBuffer(mFrameBuffer);

				uint16_t bufferSize = mAudioFormat.bitDepthInBytes * MWCAP_AUDIO_SAMPLES_PER_FRAME * mAudioFormat.
					inputChannelCount;
				auto res = ParseBitstreamBuffer(bufferSize, &detectedCodec);
				if (S_OK == res || S_PARTIAL_DATABURST == res)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L2(mLogData.logger, "[{}] Detected bitstream in frame {} {} (res: {:#08x})",
					             mLogData.prefix, mFrameCounter, codecNames[mDetectedCodec],
					             static_cast<unsigned long>(res));
					#endif
					mProbeOnTimer = false;
					if (mDetectedCodec == *detectedCodec)
					{
						if (mDataBurstPayloadSize > 0) mSinceCodecChange++;
					}
					else
					{
						mSinceCodecChange = 0;
						mDetectedCodec = *detectedCodec;
					}
					mSinceLast = 0;
					if (mDataBurstPayloadSize > 0)
					{
						#ifndef NO_QUILL
						LOG_TRACE_L3(mLogData.logger,
						             "[{}] Bitstream databurst complete, collected {} bytes from {} frames",
						             mLogData.prefix, mDataBurstPayloadSize, ++mDataBurstFrameCount);
						#endif
						newAudioFormat.dataBurstSize = mDataBurstPayloadSize;
						mDataBurstFrameCount = 0;
					}
					else
					{
						if (S_PARTIAL_DATABURST == res) mDataBurstFrameCount++;
						continue;
					}
				}
				else
				{
					if (++mSinceLast < mBitstreamDetectionWindowLength)
					{
						// skip to the next frame if we're in the initial probe otherwise allow publication downstream to continue
						if (!mProbeOnTimer)
						{
							continue;
						}
					}
					else
					{
						#ifndef NO_QUILL
						if (mSinceLast == mBitstreamDetectionWindowLength)
						{
							LOG_TRACE_L1(mLogData.logger,
							             "[{}] Probe complete after {} frames, not bitstream (timer? {})",
							             mLogData.prefix, mSinceLast, mProbeOnTimer);
						}
						#endif
						mProbeOnTimer = false;
						mDetectedCodec = PCM;
						mBytesSincePaPb = 0;
					}
				}
			}
			else
			{
				mSinceLast++;
			}
			int probeTrigger = std::lround(mBitstreamDetectionWindowLength * bitstreamDetectionRetryAfter);
			if (mSinceLast >= probeTrigger)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Triggering bitstream probe after {} frames", mLogData.prefix,
				             mSinceLast);
				#endif
				mProbeOnTimer = true;
				mSinceLast = 0;
				mBytesSincePaPb = 0;
			}

			// don't try to publish PAUSE_OR_NULL downstream
			if (mDetectedCodec == PAUSE_OR_NULL)
			{
				mSinceCodecChange = 0;
				continue;
			}

			newAudioFormat.codec = mDetectedCodec;

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

				mFilter->OnAudioSignalLoaded(&mAudioSignal);
				mFilter->OnAudioFormatLoaded(&mAudioFormat);
			}

			if (newAudioFormat.codec == PCM || mDataBurstPayloadSize > 0)
			{
				retVal = audio_capture_pin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
				GetReferenceTime(&now);
				mFrameTs.snap(now, BUFFER_ALLOCATED);

				if (SUCCEEDED(retVal))
				{
					hasFrame = true;
				}
				else
				{
					mSinceCodecChange = 0;
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] Audio frame buffered but unable to get delivery buffer, retry after backoff",
					            mLogData.prefix);
					#endif
				}
			}
		}

		if (!hasFrame)
		{
			mFrameTs.reset();
			SHORT_BACKOFF;
		}
	}
	return retVal;
}

HRESULT magewell_audio_capture_pin::FillBuffer(IMediaSample* pms)
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
	auto sampleSize = pms->GetSize();
	auto bytesCaptured = 0L;
	auto samplesCaptured = 0;

	if (mAudioFormat.codec != PCM)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] Sending {} {} bytes", mLogData.prefix, mDataBurstPayloadSize,
		             codecNames[mAudioFormat.codec]);
		#endif

		for (auto i = 0; i < mDataBurstPayloadSize; i++)
		{
			pmsData[i] = mDataBurstBuffer[i];
		}
		pms->SetActualDataLength(mDataBurstPayloadSize);
		samplesCaptured++;
		bytesCaptured = mDataBurstPayloadSize;
		mDataBurstPayloadSize = 0;
		ResizeMetrics(mCompressedAudioRefreshRate);
	}
	else
	{
		ResizeMetrics(static_cast<double>(mAudioFormat.fs) / MWCAP_AUDIO_SAMPLES_PER_FRAME);

		// channel order on input is L0-L3,R0-R3 which has to be remapped to L0,R0,L1,R1,L2,R2,L3,R3
		// each 4 byte sample is left zero padded if the incoming stream is a lower bit depth (which is typically the case for HDMI audio)
		// must also apply the channel offsets to ensure each input channel is offset as necessary to be written to the correct output channel index
		auto outputChannelIdxL = -1;
		auto outputChannelIdxR = -1;
		auto outputChannels = -1;
		auto mustRescaleLfe = mAudioFormat.lfeLevelAdjustment != unity; // NOLINT(clang-diagnostic-float-equal)

		#ifndef NO_QUILL
		if (mustRescaleLfe)
		{
			LOG_ERROR(mLogData.logger, "[{}] ERROR! Rescale LFE not implemented!", mLogData.prefix);
		}
		#endif

		for (auto pairIdx = 0; pairIdx < mAudioFormat.inputChannelCount / 2; ++pairIdx)
		{
			auto channelIdxL = pairIdx * 2;
			auto outputOffsetL = mAudioFormat.channelOffsets[channelIdxL];
			if (outputOffsetL != not_present) outputChannelIdxL = ++outputChannels;

			auto channelIdxR = channelIdxL + 1;
			auto outputOffsetR = mAudioFormat.channelOffsets[channelIdxR];
			if (outputOffsetR != not_present) outputChannelIdxR = ++outputChannels;

			if (outputOffsetL == not_present && outputOffsetR == not_present) continue;

			for (auto sampleIdx = 0; sampleIdx < MWCAP_AUDIO_SAMPLES_PER_FRAME; sampleIdx++)
			{
				auto inByteStartIdxL = sampleIdx * MWCAP_AUDIO_MAX_NUM_CHANNELS; // scroll to the sample block
				inByteStartIdxL += pairIdx; // scroll to the left channel slot
				inByteStartIdxL *= maxBitDepthInBytes; // convert from slot index to byte index

				auto inByteStartIdxR = sampleIdx * MWCAP_AUDIO_MAX_NUM_CHANNELS; // scroll to the sample block
				inByteStartIdxR += pairIdx + MWCAP_AUDIO_MAX_NUM_CHANNELS / 2;
				// scroll to the left channel slot then jump to the matching right channel
				inByteStartIdxR *= maxBitDepthInBytes; // convert from slot index to byte index

				auto outByteStartIdxL = sampleIdx * mAudioFormat.outputChannelCount; // scroll to the sample block
				outByteStartIdxL += (outputChannelIdxL + outputOffsetL); // jump to the output channel slot
				outByteStartIdxL *= mAudioFormat.bitDepthInBytes; // convert from slot index to byte index

				auto outByteStartIdxR = sampleIdx * mAudioFormat.outputChannelCount; // scroll to the sample block
				outByteStartIdxR += (outputChannelIdxR + outputOffsetR); // jump to the output channel slot
				outByteStartIdxR *= mAudioFormat.bitDepthInBytes; // convert from slot index to byte index

				if (mAudioFormat.lfeChannelIndex == channelIdxL && mustRescaleLfe)
				{
					// PCM in network (big endian) byte order hence have to shift rather than use memcpy
					//   convert to an int
					int sampleValueL = mFrameBuffer[inByteStartIdxL] << 24 | mFrameBuffer[inByteStartIdxL + 1] << 16 |
						mFrameBuffer[inByteStartIdxL + 2] << 8 | mFrameBuffer[inByteStartIdxL + 3];

					int sampleValueR = mFrameBuffer[inByteStartIdxR] << 24 | mFrameBuffer[inByteStartIdxR + 1] << 16 |
						mFrameBuffer[inByteStartIdxR + 2] << 8 | mFrameBuffer[inByteStartIdxR + 3];

					//   adjust gain to a double
					double scaledValueL = mAudioFormat.lfeLevelAdjustment * sampleValueL;
					double scaledValueR = mAudioFormat.lfeLevelAdjustment * sampleValueR;

					// TODO
					//   triangular dither back to 16 or 24 bit PCM

					//   convert back to bytes and write to the sample
				}
				else
				{
					// skip past any zero padding (if any)
					inByteStartIdxL += maxBitDepthInBytes - mAudioFormat.bitDepthInBytes;
					inByteStartIdxR += maxBitDepthInBytes - mAudioFormat.bitDepthInBytes;
					for (int k = 0; k < mAudioFormat.bitDepthInBytes; ++k)
					{
						if (outputOffsetL != not_present)
						{
							auto outIdx = outByteStartIdxL + k;
							bytesCaptured++;
							if (outIdx < sampleSize)
							{
								pmsData[outIdx] = mFrameBuffer[inByteStartIdxL + k];
							}
							else
							{
								#ifndef NO_QUILL
								LOG_ERROR(mLogData.logger,
								          "[{}] Skipping L byte {} when sample should only be {} bytes long",
								          mLogData.prefix, outIdx, sampleSize);
								#endif
							}
						}

						if (outputOffsetR != not_present)
						{
							auto outIdx = outByteStartIdxR + k;
							bytesCaptured++;
							if (outIdx < sampleSize)
							{
								pmsData[outIdx] = mFrameBuffer[inByteStartIdxR + k];
							}
							else
							{
								#ifndef NO_QUILL
								LOG_ERROR(mLogData.logger,
								          "[{}] Skipping R byte {} when sample should only be {} bytes long",
								          mLogData.prefix, outIdx, sampleSize);
								#endif
							}
						}
					}
				}
				if (pairIdx == 0) samplesCaptured++;
			}
		}

		#ifdef RECORD_ENCODED
		LOG_TRACE_L3(mLogData.logger, "[{}] pcm_out,{},{}", mLogData.prefix, mFrameCounter, bytesCaptured);
		fwrite(pmsData, bytesCaptured, 1, mEncodedOutFile);
		#endif
	}

	int64_t now;
	GetReferenceTime(&now);
	mFrameTs.snap(now, CONVERTED);

	auto endTime = mCurrentFrameTime;
	auto startTime = endTime - static_cast<long>(mAudioFormat.sampleInterval * MWCAP_AUDIO_SAMPLES_PER_FRAME);

	mFrameTs.end();

	RecordLatency();

	#ifndef NO_QUILL
	if (!mLoggedLatencyHeader)
	{
		mLoggedLatencyHeader = true;
		LOG_TRACE_L1(mLogData.audioLat,
		             "codec,since,idx,"
		             "waiting,waitComplete,bufferAllocated,"
		             "buffering,buffered,reading,"
		             "read,converted,sysTime,"
		             "bytes,samples,startTime,"
		             "endTime");
	}
	auto ts = mFrameTs;
	LOG_TRACE_L1(mLogData.audioLat,
	             "{},{},{},"
	             "{},{},{},"
	             "{},{},{},"
	             "{},{},{},"
	             "{},{},{},"
	             "{}",
	             codecNames[mAudioFormat.codec], mSinceCodecChange, mFrameCounter,
	             ts.get(WAITING), ts.get(WAIT_COMPLETE), ts.get(BUFFER_ALLOCATED),
	             ts.get(BUFFERING), ts.get(BUFFERED), ts.get(READING),
	             ts.get(READ), ts.get(CONVERTED), ts.get(COMPLETE),
	             bytesCaptured, samplesCaptured, startTime,
	             endTime);

	LOG_TRACE_L1(mLogData.logger, "[{}] Captured frame {}", mLogData.prefix, mFrameCounter);
	#endif

	pms->SetTime(&startTime, &endTime);
	pms->SetSyncPoint(mAudioFormat.codec == PCM);
	pms->SetDiscontinuity(mSinceCodecChange < 2 && mAudioFormat.codec != PCM);
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

void magewell_audio_capture_pin::StopCapture()
{
	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == MW_PRO)
	{
		MWStopAudioCapture(mFilter->GetChannelHandle());
	}
	else
	{
		if (mAudioCapture)
		{
			mAudioCapture.reset();
		}
	}
}

bool magewell_audio_capture_pin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
{
	if (mAudioFormat.codec == PCM)
	{
		pProperties->cbBuffer = MWCAP_AUDIO_SAMPLES_PER_FRAME * mAudioFormat.bitDepthInBytes * mAudioFormat.
			outputChannelCount;
	}
	else
	{
		pProperties->cbBuffer = mDataBurstBuffer.size();
	}
	if (pProperties->cBuffers < 1)
	{
		pProperties->cBuffers = 16;
		return false;
	}
	return true;
}

// copies the inbound byte stream into a format that can be probed
void magewell_audio_capture_pin::CopyToBitstreamBuffer(BYTE* buf)
{
	// copies from input to output skipping zero bytes and with a byte swap per sample
	auto bytesCopied = 0;
	for (auto pairIdx = 0; pairIdx < mAudioFormat.inputChannelCount / 2; ++pairIdx)
	{
		for (auto sampleIdx = 0; sampleIdx < MWCAP_AUDIO_SAMPLES_PER_FRAME; sampleIdx++)
		{
			int inStartL = (sampleIdx * MWCAP_AUDIO_MAX_NUM_CHANNELS + pairIdx) * maxBitDepthInBytes;
			int inStartR = (sampleIdx * MWCAP_AUDIO_MAX_NUM_CHANNELS + pairIdx + MWCAP_AUDIO_MAX_NUM_CHANNELS / 2) *
				maxBitDepthInBytes;
			int outStart = (sampleIdx * mAudioFormat.inputChannelCount + pairIdx * mAudioFormat.inputChannelCount) *
				mAudioFormat.bitDepthInBytes;
			for (int byteIdx = 0; byteIdx < mAudioFormat.bitDepthInBytes; ++byteIdx)
			{
				auto outL = outStart + byteIdx;
				auto outR = outStart + mAudioFormat.bitDepthInBytes + byteIdx;
				auto inL = inStartL + maxBitDepthInBytes - byteIdx - 1;
				auto inR = inStartR + maxBitDepthInBytes - byteIdx - 1;
				// byte swap because compressed audio is big endian 
				mCompressedBuffer[outL] = buf[inL];
				mCompressedBuffer[outR] = buf[inR];
				bytesCopied += 2;
			}
		}
	}
	#ifdef RECORD_ENCODED
	LOG_TRACE_L3(mLogData.logger, "[{}] encoder_in,{},{}", mLogData.prefix, mFrameCounter, bytesCopied);
	fwrite(mCompressedBuffer, bytesCopied, 1, mEncodedInFile);
	#endif
}

// probes a non PCM buffer for the codec based on format of the IEC 61937 dataframes and/or copies the content to the data burst burffer
HRESULT magewell_audio_capture_pin::ParseBitstreamBuffer(uint16_t bufSize, enum codec** codec)
{
	uint16_t bytesRead = 0;
	bool copiedBytes = false;
	bool partialDataBurst = false;
	bool maybeBitstream = false;

	#ifndef NO_QUILL
	bool foundPause = **codec == PAUSE_OR_NULL;
	#endif

	while (bytesRead < bufSize)
	{
		uint16_t remainingInBurst = std::max(mDataBurstSize - mDataBurstRead, 0);
		if (remainingInBurst > 0)
		{
			uint16_t remainingInBuffer = bufSize - bytesRead;
			auto toCopy = std::min(remainingInBurst, remainingInBuffer);
			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] Copying {} bytes of databurst from {}-{} to {}-{}", mLogData.prefix,
			             toCopy,
			             bytesRead, bytesRead + toCopy - 1, mDataBurstRead, mDataBurstRead + toCopy - 1);
			#endif
			for (auto i = 0; i < toCopy; i++)
			{
				mDataBurstBuffer[mDataBurstRead + i] = mCompressedBuffer[bytesRead + i];
			}
			bytesRead += toCopy;
			mDataBurstRead += toCopy;
			remainingInBurst -= toCopy;
			mBytesSincePaPb += toCopy;
			copiedBytes = true;

			if (remainingInBurst == 0)
			{
				mDataBurstPayloadSize = mDataBurstSize;
				#ifdef RECORD_ENCODED
				LOG_TRACE_L3(mLogData.logger, "[{}] encoder_out,{},{}", mLogData.prefix, mFrameCounter, mDataBurstSize);
				fwrite(mDataBurstBuffer.data(), mDataBurstSize, 1, mEncodedOutFile);
				#endif
			}
		}
		// more to read = will need another frame
		if (remainingInBurst > 0)
		{
			partialDataBurst = true;
			continue;
		}

		// no more to read so reset the databurst state ready for the next frame
		mDataBurstSize = mDataBurstRead = 0;

		// burst complete so search the frame for the PaPb preamble F8 72 4E 1F (248 114 78 31)
		for (; (bytesRead < bufSize) && mPaPbBytesRead != 4; ++bytesRead, ++mBytesSincePaPb)
		{
			if (mCompressedBuffer[bytesRead] == 0xf8 && mPaPbBytesRead == 0
				|| mCompressedBuffer[bytesRead] == 0x72 && mPaPbBytesRead == 1
				|| mCompressedBuffer[bytesRead] == 0x4e && mPaPbBytesRead == 2
				|| mCompressedBuffer[bytesRead] == 0x1f && mPaPbBytesRead == 3)
			{
				if (++mPaPbBytesRead == 4)
				{
					mDataBurstSize = mDataBurstRead = 0;
					bytesRead++;
					#ifndef NO_QUILL
					if (!foundPause)
						LOG_TRACE_L2(mLogData.logger, "[{}] Found PaPb at position {}-{} ({} since last)", mLogData.prefix,
					             bytesRead - 4, bytesRead, mBytesSincePaPb);
					#endif
					// e.g. 24576 / 768 = 32 for EAC3 will produce a new output sample every 32nd inbound sample
					// at 192kHz there are 1000 samples per second hence effective sample rate is 1000 / 32 = ~31Hzd
					mCompressedAudioRefreshRate = static_cast<double>(mAudioFormat.fs / MWCAP_AUDIO_SAMPLES_PER_FRAME) /
						((mBytesSincePaPb - 3) / bufSize);
					mBytesSincePaPb = 4;
					maybeBitstream = false;
					break;
				}
			}
			else
			{
				mPaPbBytesRead = 0;
			}
		}

		if (mPaPbBytesRead == 1 || mPaPbBytesRead == 2 || mPaPbBytesRead == 3)
		{
			#ifndef NO_QUILL
			if (!foundPause)
			{
				LOG_TRACE_L3(mLogData.logger, "[{}] PaPb {} bytes found", mLogData.prefix, mPaPbBytesRead);
			}
			#endif
			maybeBitstream = true;
			continue;
		}

		// grab PcPd preamble words
		uint8_t bytesToCopy = std::min(bufSize - bytesRead, 4 - mPcPdBytesRead);
		if (bytesToCopy > 0)
		{
			memcpy(mPcPdBuffer, mCompressedBuffer + bytesRead, bytesToCopy);
			mPcPdBytesRead += bytesToCopy;
			bytesRead += bytesToCopy;
			mBytesSincePaPb += bytesToCopy;
			copiedBytes = true;
		}

		if (mPcPdBytesRead != 4)
		{
			#ifndef NO_QUILL
			if (!foundPause && mPcPdBytesRead != 0)
				LOG_TRACE_L3(mLogData.logger, "[{}] Found PcPd at position {} but only {} bytes available", mLogData.prefix,
			             bytesRead - bytesToCopy, bytesToCopy);
			#endif
			continue;
		}

		mDataBurstSize = ((static_cast<uint16_t>(mPcPdBuffer[2]) << 8) + static_cast<uint16_t>(mPcPdBuffer[3]));
		auto dt = static_cast<uint8_t>(mPcPdBuffer[1] & 0x7f);
		GetCodecFromIEC61937Preamble(IEC61937DataType{dt}, &mDataBurstSize, *codec);

		// ignore PAUSE_OR_NULL, start search again
		if (**codec == PAUSE_OR_NULL)
		{
			#ifndef NO_QUILL
			if (!foundPause)
			{
				foundPause = true;
				LOG_TRACE_L2(mLogData.logger, "[{}] Found PAUSE_OR_NULL ({}) with burst size {}, start skipping",
				             mLogData.prefix, dt, mDataBurstSize);
			}
			#endif
			mPaPbBytesRead = mPcPdBytesRead = 0;
			mDataBurstSize = mDataBurstPayloadSize = mDataBurstRead = 0;
			continue;
		}

		#ifndef NO_QUILL
		if (foundPause)
		{
			LOG_TRACE_L2(mLogData.logger, "[{}] Exiting PAUSE_OR_NULL skip mode", mLogData.prefix);
			foundPause = false;
		}
		#endif

		if (mDataBurstBuffer.size() > mDataBurstSize)
		{
			mDataBurstBuffer.clear();
		}
		if (mDataBurstBuffer.size() < mDataBurstSize)
		{
			mDataBurstBuffer.resize(mDataBurstSize);
		}

		mPaPbBytesRead = mPcPdBytesRead = 0;
		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] Found codec {} with burst size {}", mLogData.prefix,
		             codecNames[static_cast<int>(**codec)], mDataBurstSize);
		#endif
	}
	return partialDataBurst
		       ? S_PARTIAL_DATABURST
		       : maybeBitstream
		       ? S_POSSIBLE_BITSTREAM
		       : copiedBytes
		       ? S_OK
		       : S_FALSE;
}

// identifies codecs that are known/expected to be carried via HDMI in an AV setup
// from IEC 61937-2 Table 2
HRESULT magewell_audio_capture_pin::GetCodecFromIEC61937Preamble(const IEC61937DataType dataType, uint16_t* burstSize,
                                                                 codec* codec)
{
	switch (dataType & 0xff)
	{
	case IEC61937_AC3:
		*burstSize /= 8; // bits
		*codec = AC3;
		break;
	case IEC61937_DTS1:
	case IEC61937_DTS2:
	case IEC61937_DTS3:
		*burstSize /= 8; // bits
		*codec = DTS;
		break;
	case IEC61937_DTSHD:
		*codec = DTSHD;
		break;
	case IEC61937_EAC3:
		*codec = EAC3;
		break;
	case IEC61937_TRUEHD:
		*codec = TRUEHD;
		break;
	case IEC61937_NULL:
	case IEC61937_PAUSE:
		*codec = PAUSE_OR_NULL;
		break;
	default:
		*codec = PAUSE_OR_NULL;
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Unknown IEC61937 datatype {} will be treated as PAUSE", mLogData.prefix,
		            dataType & 0xff);
		#endif
		break;
	}
	return S_OK;
}

void magewell_audio_capture_pin::LoadFormat(audio_format* audioFormat, const audio_signal* audioSignal) const
{
	auto audioIn = *audioSignal;
	auto currentChannelAlloc = audioFormat->channelAllocation;
	auto currentChannelMask = audioFormat->channelValidityMask;
	if (mFilter->GetDeviceType() != MW_PRO)
	{
		audioFormat->fs = 48000;
	}
	else
	{
		audioFormat->fs = audioIn.signalStatus.dwSampleRate;
	}
	audioFormat->bitDepth = audioIn.signalStatus.cBitsPerSample;
	audioFormat->bitDepthInBytes = audioFormat->bitDepth / 8;
	audioFormat->codec = audioIn.signalStatus.bLPCM ? PCM : BITSTREAM;
	audioFormat->sampleInterval = static_cast<double>(dshowTicksPerSecond) / audioFormat->fs;
	audioFormat->channelAllocation = audioIn.audioInfo.byChannelAllocation;
	audioFormat->channelValidityMask = audioIn.signalStatus.wChannelValid;

	if (audioFormat->channelAllocation == currentChannelAlloc && audioFormat->channelValidityMask == currentChannelMask)
	{
		// no change, leave untouched [inputChannelCount, outputChannelCount, channelMask, channelOffsets]
	}
	else
	{
		// https://ia903006.us.archive.org/11/items/CEA-861-E/CEA-861-E.pdf 
		if (audioIn.signalStatus.wChannelValid & (0x01 << 0))
		{
			if (audioIn.signalStatus.wChannelValid & (0x01 << 1))
			{
				if (audioIn.signalStatus.wChannelValid & (0x01 << 2))
				{
					if (audioIn.signalStatus.wChannelValid & (0x01 << 3))
					{
						audioFormat->inputChannelCount = 8;
						audioFormat->outputChannelCount = 8;
						audioFormat->channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
						audioFormat->channelOffsets.fill(0);
						// swap LFE and FC
						audioFormat->channelOffsets[2] = 1;
						audioFormat->channelOffsets[3] = -1;
						audioFormat->lfeChannelIndex = 2;
						audioFormat->channelLayout = "FL FR FC LFE BL BR SL SR";
					}
					else
					{
						audioFormat->inputChannelCount = 6;
						audioFormat->outputChannelCount = 6;
						audioFormat->channelMask = KSAUDIO_SPEAKER_5POINT1;
						audioFormat->channelOffsets.fill(0);
						audioFormat->channelOffsets[2] = 1;
						audioFormat->channelOffsets[3] = -1;
						audioFormat->channelOffsets[6] = not_present;
						audioFormat->channelOffsets[7] = not_present;
						audioFormat->lfeChannelIndex = 2;
						audioFormat->channelLayout = "FL FR FC LFE BL BR";
					}
				}
				else
				{
					audioFormat->inputChannelCount = 4;
					audioFormat->outputChannelCount = 4;
					audioFormat->channelMask = KSAUDIO_SPEAKER_3POINT1;
					audioFormat->channelOffsets.fill(not_present);
					audioFormat->channelOffsets[0] = 0;
					audioFormat->channelOffsets[1] = 0;
					audioFormat->channelOffsets[2] = 1;
					audioFormat->channelOffsets[3] = -1;
					audioFormat->lfeChannelIndex = 2;
					audioFormat->channelLayout = "FL FR FC LFE";
				}
			}
			else
			{
				audioFormat->inputChannelCount = 2;
				audioFormat->outputChannelCount = 2;
				audioFormat->channelMask = KSAUDIO_SPEAKER_STEREO;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->lfeChannelIndex = not_present;
				audioFormat->channelLayout = "FL FR";
			}

			// CEA-861-E Table 28
			switch (audioFormat->channelAllocation)
			{
			case 0x00:
				// FL FR
				audioFormat->channelLayout = "FL FR";
				break;
			case 0x01:
				// FL FR LFE --
				audioFormat->channelLayout = "FL FR LFE";
				audioFormat->channelMask = KSAUDIO_SPEAKER_2POINT1;
				audioFormat->inputChannelCount = 4;
				audioFormat->outputChannelCount = 3;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x02:
				// FL FR -- FC
				audioFormat->channelLayout = "FL FR FC";
				audioFormat->channelMask = KSAUDIO_SPEAKER_3POINT0;
				audioFormat->inputChannelCount = 4;
				audioFormat->outputChannelCount = 3;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x03:
				// FL FR LFE FC
				audioFormat->channelLayout = "FL FR FC LFE";
				audioFormat->channelMask = KSAUDIO_SPEAKER_3POINT1;
				audioFormat->inputChannelCount = 4;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1; // LFE -> FC
				audioFormat->channelOffsets[3] = -1; // FC -> LFE
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x04:
				// FL FR -- -- RC --
				audioFormat->channelLayout = "FL FR RC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 3;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x05:
				// FL FR LFE -- RC --
				audioFormat->channelLayout = "FL FR LFE RC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x06:
				// FL FR -- FC RC --
				audioFormat->channelLayout = "FL FR FC RC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets.fill(not_present);
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x07:
				// FL FR LFE FC RC --
				audioFormat->channelLayout = "FL FR LFE FC RC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x08:
				// FL FR -- -- RL RR
				audioFormat->channelLayout = "FL FR RL RR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
					SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				break;
			case 0x09:
				// FL FR LFE -- RL RR
				audioFormat->channelLayout = "FL FR LFE RL RR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x0A:
				// FL FR -- FC RL RR
				audioFormat->channelLayout = "FL FR FC RL RR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x0B:
				// FL FR LFE FC BL BR
				audioFormat->channelLayout = "FL FR FC LFE BL BR";
				audioFormat->channelMask = KSAUDIO_SPEAKER_5POINT1;
				audioFormat->inputChannelCount = 6;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x0C:
				// FL FR -- -- RL RR RC --
				audioFormat->channelLayout = "FL FR BL BR BC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
					SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x0D:
				// FL FR LFE -- RL RR RC --
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
				audioFormat->channelLayout = "FL FR LFE BL BR BC";
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x0E:
				// FL FR -- FC RL RR RC --
				audioFormat->channelLayout = "FL FR FC BL BR BC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x0F:
				// FL FR LFE FC RL RR RC --
				audioFormat->channelLayout = "FL FR FC LFE BL BR BC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x10:
				// FL FR -- -- RL RR RLC RRC
				audioFormat->channelLayout = "FL FR BL BR SL SR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_SIDE_LEFT |
					SPEAKER_SIDE_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = 2;
				audioFormat->channelOffsets[6] = -2;
				audioFormat->channelOffsets[7] = -2;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x11:
				// FL FR LFE -- RL RR RLC RRC
				audioFormat->channelLayout = "FL FR LFE BL BR SL SR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = 2;
				audioFormat->channelOffsets[6] = -2;
				audioFormat->channelOffsets[7] = -2;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x12:
				// FL FR -- FC RL RR RLC RRC
				audioFormat->channelLayout = "FL FR FC BL BR SL SR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = 2;
				audioFormat->channelOffsets[6] = -2;
				audioFormat->channelOffsets[7] = -2;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x13:
				// FL FR LFE FC RL RR RLC RRC (RL = side, RLC = back)
				audioFormat->channelLayout = "FL FR FC LFE BL BR SL SR";
				audioFormat->channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = 2;
				audioFormat->channelOffsets[6] = -2;
				audioFormat->channelOffsets[7] = -2;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x14:
				// FL FR -- -- -- -- FLC FRC
				audioFormat->channelLayout = "FL FR FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_LEFT_OF_CENTER |
					SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = not_present;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x15:
				// FL FR LFE -- -- -- FLC FRC
				audioFormat->channelLayout = "FL FR LFE FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = not_present;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x16:
				// FL FR -- FC -- -- FLC FRC
				audioFormat->channelLayout = "FL FR FC FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = not_present;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x17:
				// FL FR LFE FC -- -- FLC FRC
				audioFormat->channelLayout = "FL FR FC LFE FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = not_present;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x18:
				// FL FR -- -- RC -- FLC FRC
				audioFormat->channelLayout = "FL FR RC FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_CENTER |
					SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = -1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x19:
				// FL FR LFE -- RC -- FLC FRC
				audioFormat->channelLayout = "FL FR LFE RC FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_CENTER | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = -1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x1A:
				// FL FR -- FC RC -- FLC FRC
				audioFormat->channelLayout = "FL FR FC RC FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_CENTER | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = -1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x1B:
				// FL FR LFE FC RC -- FLC FRC
				audioFormat->channelLayout = "FL FR FC LFE RC FLC FRC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_CENTER | SPEAKER_FRONT_LEFT_OF_CENTER |
					SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 2;
				audioFormat->channelOffsets[5] = not_present;
				audioFormat->channelOffsets[6] = -1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x1C:
				// FL FR -- -- RL RR FLC FRC
				audioFormat->channelLayout = "FL FR BL BR FLC FLR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
					SPEAKER_BACK_RIGHT | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x1D:
				// FL FR LFE -- RL RR FLC FRC
				audioFormat->channelLayout = "FL FR LFE BL BR FLC FLR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_FRONT_LEFT_OF_CENTER |
					SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x1E:
				// FL FR -- FC RL RR FLC FRC
				audioFormat->channelLayout = "FL FR FC BL BR FLC FLR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_FRONT_LEFT_OF_CENTER |
					SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x1F:
				// FL FR LFE FC RL RR FLC FRC
				audioFormat->channelLayout = "FL FR LFE FC BL BR FLC FLR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_FRONT_LEFT_OF_CENTER |
					SPEAKER_FRONT_RIGHT_OF_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x20:
				// FL FR -- FC RL RR FCH --
				audioFormat->channelLayout = "FL FR FC BL BR TFC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x21:
				// FL FR LFE FC RL RR FCH --
				audioFormat->channelLayout = "FL FR FC LFE BL BR TFC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x22:
				// FL FR -- FC RL RR -- TC
				audioFormat->channelLayout = "FL FR FC BL BR TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x23:
				// FL FR LFE FC RL RR -- TC
				audioFormat->channelLayout = "FL FR FC LFE BL BR TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x24:
				// FL FR -- -- RL RR FLH FRH
				audioFormat->channelLayout = "FL FR BL BR TFL TFR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
					SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_FRONT_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x25:
				// FL FR LFE -- RL RR FLH FRH
				audioFormat->channelLayout = "FL FR LFE BL BR TFL TFR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_LEFT | SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_FRONT_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x26:
				// FL FR -- -- RL RR FLW FRW (WIDE not supported by Windows, discarded)
				audioFormat->channelLayout = "FL FR BL BR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT |
					SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 4;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x27:
				// FL FR LFE -- RL RR FLW FRW (WIDE not supported by Windows, discarded)
				audioFormat->channelLayout = "FL FR LFE BL BR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_BACK_LEFT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x28:
				// FL FR -- FC RL RR RC TC
				audioFormat->channelLayout = "FL FR FC BL BR BC TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER | SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x29:
				// FL FR LFE FC RL RR RC TC
				audioFormat->channelLayout = "FL FR FC LFE BL BR BC TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER |
					SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x2A:
				// FL FR -- FC RL RR RC FCH
				audioFormat->channelLayout = "FL FR FC BL BR BC TFC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER | SPEAKER_TOP_FRONT_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x2B:
				// FL FR LFE FC RL RR RC FCH
				audioFormat->channelLayout = "FL FR FC LFE BL BR BC TFC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER |
					SPEAKER_TOP_FRONT_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x2C:
				// FL FR -- FC RL RR FCH TC
				audioFormat->channelLayout = "FL FR FC BL BR TFC TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_CENTER | SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 0;
				audioFormat->channelOffsets[3] = not_present;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x2D:
				// FL FR LFE FC RL RR FCH TC
				audioFormat->channelLayout = "FL FR FC LFE BL BR TFC TC";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_CENTER |
					SPEAKER_TOP_CENTER;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 1;
				audioFormat->channelOffsets[7] = -1;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x2E:
				// FL FR -- FC RL RR FLH FRH
				audioFormat->channelLayout = "FL FR FC BL BR TFL TFR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_FRONT_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 7;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x2F:
				// FL FR LFE FC RL RR FLH FRH
				audioFormat->channelLayout = "FL FR FC LFE BL BR TFL TFR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_TOP_FRONT_LEFT |
					SPEAKER_TOP_FRONT_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 8;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = 0;
				audioFormat->channelOffsets[7] = 0;
				audioFormat->lfeChannelIndex = 2;
				break;
			case 0x30:
				// FL FR -- FC RL RR FLW FRW (WIDE not supported by Windows, discarded)
				audioFormat->channelLayout = "FL FR FC BL BR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 5;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = not_present;
				audioFormat->channelOffsets[3] = 0;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = not_present;
				break;
			case 0x31:
				// FL FR LFE FC RL RR FLW FRW (WIDE not supported by Windows, discarded)
				audioFormat->channelLayout = "FL FR FC LFE BL BR";
				audioFormat->channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY |
					SPEAKER_FRONT_CENTER | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
				audioFormat->inputChannelCount = 8;
				audioFormat->outputChannelCount = 6;
				audioFormat->channelOffsets[0] = 0;
				audioFormat->channelOffsets[1] = 0;
				audioFormat->channelOffsets[2] = 1;
				audioFormat->channelOffsets[3] = -1;
				audioFormat->channelOffsets[4] = 0;
				audioFormat->channelOffsets[5] = 0;
				audioFormat->channelOffsets[6] = not_present;
				audioFormat->channelOffsets[7] = not_present;
				audioFormat->lfeChannelIndex = 2;
				break;
			default:
				// ignore
				break;
			}

			// CEA-861-E Table 31
			audioFormat->lfeLevelAdjustment = audioIn.audioInfo.byLFEPlaybackLevel == 0x2 ? minus_10db : unity;
		}
		else
		{
			audioFormat->inputChannelCount = 0;
			audioFormat->outputChannelCount = 0;
			audioFormat->channelOffsets.fill(not_present);
			audioFormat->lfeChannelIndex = not_present;
		}
	}
}
