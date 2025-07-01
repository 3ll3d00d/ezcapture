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
#include "bm_video_capture_pin.h"

using mics = std::chrono::microseconds;
static int64_t get_steady_clock_uptime_mics()
{
	auto now = std::chrono::steady_clock::now();
	auto referenceTime = std::chrono::time_point_cast<mics>(now);
	return referenceTime.time_since_epoch().count();
}


blackmagic_video_capture_pin::blackmagic_video_capture_pin(HRESULT* phr, blackmagic_capture_filter* pParent,
                                                           bool pPreview, video_format pVideoFormat) :
	hdmi_video_capture_pin(
		phr,
		pParent,
		pPreview ? "VideoPreview" : "VideoCapture",
		pPreview ? L"Preview" : L"Capture",
		pPreview ? "VideoPreview" : "VideoCapture",
		std::move(pVideoFormat),
		{
			// standard consumer formats
			{YUV2, {YV16, YUV2_YV16}},
			{V210, {P210, V210_P210}}, // supported natively by madvr
			{R210, {RGB48, R210_BGR48}}, // supported natively by jrvr >= MC34
			// unlikely to be seen in the wild so just fallback to RGB using decklink sdk
			{AY10, {RGBA, ANY_RGB}},
			{R12B, {RGBA, ANY_RGB}},
			{R12L, {RGBA, ANY_RGB}},
			{R10B, {RGBA, ANY_RGB}},
			{R10L, {RGBA, ANY_RGB}},
		},
		BM_DECKLINK
	)
{
}

blackmagic_video_capture_pin::~blackmagic_video_capture_pin()
{
	mCurrentFrame.reset();
}

HRESULT blackmagic_video_capture_pin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                                        REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hasFrame = false;
	auto retVal = S_FALSE;
	auto handle = mFilter->GetVideoFrameHandle();

	while (!hasFrame)
	{
		if (CheckStreamState(nullptr) == STREAM_DISCARDING)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream is discarding", mLogData.prefix);
			#endif

			mHasSignal = false;
			break;
		}
		if (IsStreamStopped())
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream has not started, retry after backoff", mLogData.prefix);
			#endif

			mHasSignal = false;
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

			mHasSignal = false;
			continue;
		}

		if (dwRet == WAIT_OBJECT_0)
		{
			mCurrentFrame = mFilter->GetVideoFrame();
			auto newVideoFormat = mCurrentFrame->GetVideoFormat();
			hasFrame = true;

			mHasSignal = true;

			auto hr = OnVideoSignal(newVideoFormat);
			if (FAILED(hr))
			{
				mCurrentFrame.reset();
				BACKOFF;
				continue;
			}

			retVal = video_capture_pin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);

			if (FAILED(retVal))
			{
				hasFrame = false;

				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
				            "[{}] Video frame buffered but unable to get delivery buffer, retry after backoff",
				            mLogData.prefix);
				#endif
			}

			if (hasFrame && mFirst)
			{
				OnChangeMediaType();
			}

			if (!hasFrame)
			{
				mCurrentFrame.reset();
				SHORT_BACKOFF;
			}
		}
	}
	return retVal;
}

HRESULT blackmagic_video_capture_pin::FillBuffer(IMediaSample* pms)
{
	auto retVal = S_OK;

	auto endTime = mCurrentFrame->GetFrameTime();
	auto startTime = endTime - mCurrentFrame->GetFrameDuration();
	pms->SetTime(&startTime, &endTime);
	mPreviousFrameTime = mCurrentFrameTime;
	mCurrentFrameTime = endTime;

	pms->SetSyncPoint(true);
	auto gap = mCurrentFrame->GetFrameIndex() - mFrameCounter;
	pms->SetDiscontinuity(gap != 1);

	mFrameCounter = mCurrentFrame->GetFrameIndex();

	AppendHdrSideDataIfNecessary(pms, endTime);

	auto now = get_steady_clock_uptime_mics();

	auto t1 = std::chrono::high_resolution_clock::now();

	if (mFrameWriter->WriteTo(mCurrentFrame.get(), pms) != S_OK)
	{
		return S_FALSE;
	}

	auto t2 = std::chrono::high_resolution_clock::now();
	auto convLat = duration_cast<std::chrono::microseconds>(t2 - t1).count();
	auto capLat = now - mCurrentFrame->GetCaptureTime();

	RecordLatency();

	#ifndef NO_QUILL
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	if (!mLoggedLatencyHeader)
	{
		LOG_TRACE_L1(mLogData.videoLat,
		             "idx,cap_lat,conv_lat,"
		             "pt,st,et,"
		             "ct,interval,delta"
		             "dur,len,gap");
		mLoggedLatencyHeader = true;
	}
	auto frameInterval = mCurrentFrameTime - mPreviousFrameTime;
	LOG_TRACE_L1(mLogData.videoLat,
	             "{},{:.3f},{:.3f},"
	             "{},{},{},"
	             "{},{},{},"
	             "{},{},{}",
	             mFrameCounter, static_cast<double>(capLat) / 1000.0, static_cast<double>(convLat) / 1000.0,
	             mPreviousFrameTime, startTime, endTime,
	             rt, frameInterval, frameInterval - mCurrentFrame->GetFrameDuration(),
	             mCurrentFrame->GetFrameDuration(), mCurrentFrame->GetLength(), gap);
	#endif

	mCurrentFrame.reset();
	mCurrentFrame = nullptr;

	if (S_FALSE == HandleStreamStateChange(pms))
	{
		retVal = S_FALSE;
	}

	return retVal;
}

HRESULT blackmagic_video_capture_pin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] blackmagic_video_capture_pin::OnThreadCreate", mLogData.prefix);
	#endif

	UpdateDisplayStatus();

	mRateSwitcher.InitIfNecessary();

	return mFilter->PinThreadCreated();
}

void blackmagic_video_capture_pin::DoThreadDestroy()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] blackmagic_video_capture_pin::DoThreadDestroy", mLogData.prefix);
	#endif

	mFilter->PinThreadDestroyed();
}

void blackmagic_video_capture_pin::OnChangeMediaType()
{
	video_capture_pin::OnChangeMediaType();

	mFilter->NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(mVideoFormat.cx, mVideoFormat.cy), 0);

	ALLOCATOR_PROPERTIES props;
	m_pAllocator->GetProperties(&props);
	auto bufferSize = props.cbBuffer;
	auto frameSize = mVideoFormat.imageSize;
	if (std::cmp_greater(bufferSize, frameSize))
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger,
		             "[{}] Likely padding requested by renderer, frame size is {} but buffer allocated is {}",
		             mLogData.prefix, frameSize, bufferSize);
		#endif
	}
}
