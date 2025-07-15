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

#include "mw_video_capture_pin.h"
#include "straight_through.h"
#include <memory>

magewell_video_capture_pin::VideoCapture::VideoCapture(magewell_video_capture_pin* pin, HCHANNEL hChannel) :
	pin(pin),
	mLogData(pin->mLogData)
{
	mEvent = MWCreateVideoCapture(hChannel, pin->mVideoFormat.cx, pin->mVideoFormat.cy,
	                              pin->mSignalledFormat.fourcc, pin->mVideoFormat.frameInterval, CaptureFrame,
	                              pin);
	#ifndef NO_QUILL
	if (mEvent == nullptr)
	{
		LOG_ERROR(mLogData.logger, "[{}] MWCreateVideoCapture failed [{}x{} '{}' {}]", mLogData.prefix,
		          pin->mVideoFormat.cx, pin->mVideoFormat.cy, pin->mSignalledFormat.name,
		          pin->mVideoFormat.frameInterval);
	}
	else
	{
		LOG_INFO(mLogData.logger, "[{}] MWCreateVideoCapture succeeded [{}x{} '{}' {}]", mLogData.prefix,
		         pin->mVideoFormat.cx, pin->mVideoFormat.cy, pin->mSignalledFormat.name,
		         pin->mVideoFormat.frameInterval);
	}
	#endif
}

magewell_video_capture_pin::VideoCapture::~VideoCapture()
{
	if (mEvent != nullptr)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] Ready to MWDestoryVideoCapture", mLogData.prefix);
		#endif

		const auto hr = MWDestoryVideoCapture(mEvent);
		mEvent = nullptr;

		#ifndef NO_QUILL
		if (MW_SUCCEEDED == hr)
		{
			LOG_INFO(mLogData.logger, "[{}] MWDestoryVideoCapture complete", mLogData.prefix);
		}
		else
		{
			LOG_WARNING(mLogData.logger, "[{}] MWDestoryVideoCapture failed", mLogData.prefix);
		}
		#endif
	}
}

//////////////////////////////////////////////////////////////////////////
//  magewell_video_capture_pin::VideoFrameGrabber
//////////////////////////////////////////////////////////////////////////
magewell_video_capture_pin::video_frame_grabber::video_frame_grabber(magewell_video_capture_pin* pin,
                                                                     HCHANNEL hChannel, device_type deviceType,
                                                                     IMediaSample* pms) :
	mLogData(pin->mLogData),
	hChannel(hChannel),
	deviceType(deviceType),
	pin(pin),
	pms(pms)
{
	this->pms->GetPointer(&pmsData);

	if (deviceType == MW_PRO)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] Pinning {} bytes", this->mLogData.prefix, this->pms->GetSize());
		#endif
		MWPinVideoBuffer(hChannel, pmsData, this->pms->GetSize());
	}
}

magewell_video_capture_pin::video_frame_grabber::~video_frame_grabber()
{
	if (deviceType == MW_PRO)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] Unpinning {} bytes, captured {} bytes", mLogData.prefix, pms->GetSize(),
		             pms->GetActualDataLength());
		#endif

		MWUnpinVideoBuffer(hChannel, pmsData);
	}
}

HRESULT magewell_video_capture_pin::video_frame_grabber::grab() const
{
	auto retVal = S_OK;
	auto hasFrame = false;
	auto proDevice = deviceType == MW_PRO;
	auto mustExit = false;
	int64_t now;
	while (!hasFrame && !mustExit)
	{
		if (proDevice)
		{
			auto hr = MWGetVideoBufferInfo(hChannel, &pin->mVideoSignal.bufferInfo);
			if (hr != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Can't get VideoBufferInfo ({})", mLogData.prefix,
				             static_cast<int>(hr));
				#endif

				continue;
			}

			BYTE bufferedFrameIdx = pin->mHasSignal
				                        ? pin->mVideoSignal.bufferInfo.iNewestBuffering
				                        : MWCAP_VIDEO_FRAME_ID_NEWEST_BUFFERING;
			hr = MWGetVideoFrameInfo(hChannel, bufferedFrameIdx, &pin->mVideoSignal.frameInfo);
			if (hr != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Can't get VideoFrameInfo ({})", mLogData.prefix,
				             static_cast<int>(hr));
				#endif

				continue;
			}

			pin->GetReferenceTime(&now);
			pin->mFrameTs.snap(now, READING);

			uint8_t* writeBuffer = pin->mFrameWriterStrategy == STRAIGHT_THROUGH ? pmsData : pin->mCapturedFrame.data;

			hr = MWCaptureVideoFrameToVirtualAddressEx(
				hChannel,
				pin->mHasSignal ? pin->mVideoSignal.bufferInfo.iNewestBuffering : MWCAP_VIDEO_FRAME_ID_NEWEST_BUFFERING,
				writeBuffer,
				pin->mVideoFormat.imageSize,
				pin->mVideoFormat.lineLength,
				FALSE,
				nullptr,
				pin->mVideoFormat.pixelFormat.fourcc,
				pin->mVideoFormat.cx,
				pin->mVideoFormat.cy,
				0,
				64,
				nullptr,
				nullptr,
				0,
				100,
				0,
				100,
				0,
				MWCAP_VIDEO_DEINTERLACE_BLEND,
				MWCAP_VIDEO_ASPECT_RATIO_IGNORE,
				nullptr,
				nullptr,
				pin->mVideoFormat.aspectX,
				pin->mVideoFormat.aspectY,
				static_cast<MWCAP_VIDEO_COLOR_FORMAT>(pin->mVideoFormat.colourFormat),
				static_cast<MWCAP_VIDEO_QUANTIZATION_RANGE>(pin->mVideoFormat.quantisation),
				static_cast<MWCAP_VIDEO_SATURATION_RANGE>(pin->mVideoFormat.saturation)
			);
			if (hr != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
				            "[{}] Unexpected failed call to MWCaptureVideoFrameToVirtualAddressEx ({})",
				            mLogData.prefix,
				            static_cast<int>(hr));
				#endif
				break;
			}
			do
			{
				DWORD dwRet = WaitForSingleObject(pin->mCaptureEvent, 1000);
				auto skip = dwRet != WAIT_OBJECT_0;
				if (skip)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Unexpected capture event ({:#08x})", mLogData.prefix, dwRet);
					#endif

					if (dwRet == STATUS_TIMEOUT)
					{
						#ifndef NO_QUILL
						LOG_TRACE_L1(mLogData.logger, "[{}] Wait for frame has timed out", mLogData.prefix);
						#endif
						mustExit = true;
						break;
					}

					if (pin->CheckStreamState(nullptr) == STREAM_DISCARDING)
					{
						mustExit = true;
						break;
					}
				}

				if (skip) continue;

				hr = MWGetVideoCaptureStatus(hChannel, &pin->mVideoSignal.captureStatus);

				#ifndef NO_QUILL
				if (hr != MW_SUCCEEDED)
				{
					LOG_TRACE_L1(mLogData.logger, "[{}] MWGetVideoCaptureStatus failed ({})", mLogData.prefix,
					             static_cast<int>(hr));
				}
				#endif

				hasFrame = pin->mVideoSignal.captureStatus.bFrameCompleted;
			}
			while (hr == MW_SUCCEEDED && !hasFrame);

			if (hasFrame)
			{
				hr = MWGetVideoFrameInfo(hChannel, bufferedFrameIdx, &pin->mVideoSignal.frameInfo);
				if (hr != MW_SUCCEEDED)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Can't get VideoFrameInfo ({})", mLogData.prefix,
					             static_cast<int>(hr));
					#endif

					continue;
				}

				pin->mFrameTs.snap(pin->mVideoSignal.frameInfo.allFieldStartTimes[0], BUFFERING);
				pin->mFrameTs.snap(pin->mVideoSignal.frameInfo.allFieldBufferedTimes[0], BUFFERED);
				pin->GetReferenceTime(&now);
				pin->mFrameTs.snap(now, READ);
				pin->mFrameCounter++;

				if (pin->mFrameWriterStrategy != STRAIGHT_THROUGH)
				{
					video_sample_buffer buffer{
						.index = pin->mFrameCounter,
						.data = pin->mCapturedFrame.data,
						.width = pin->mVideoFormat.cx,
						.height = pin->mVideoFormat.cy,
						.length = pin->mVideoFormat.imageSize
					};
					pin->mFrameWriter->WriteTo(&buffer, pms);
				}
			}
		}
		else
		{
			pin->GetReferenceTime(&now);

			CAutoLock lck(&pin->mCaptureCritSec);

			auto pmsLen = pms->GetSize();
			if (pmsLen < pin->mCapturedFrame.length)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger,
				             "[{}] MediaSample size too small, assume frame signal format change ({} vs {})",
				             mLogData.prefix, pmsLen, pin->mCapturedFrame.length);
				#endif
				BACKOFF;
			}
			else if (pin->mFrameWriterStrategy == STRAIGHT_THROUGH)
			{
				memcpy(pmsData, pin->mCapturedFrame.data, pin->mCapturedFrame.length);
				hasFrame = true;
				pin->mFrameTs.snap(now, READ);
				pin->mFrameCounter++;
			}
			else
			{
				video_sample_buffer buffer{
					.index = pin->mFrameCounter,
					.data = pin->mCapturedFrame.data,
					.width = pin->mVideoFormat.cx,
					.height = pin->mVideoFormat.cy,
					.length = pin->mCapturedFrame.length
				};
				pin->mFrameWriter->WriteTo(&buffer, pms);
				hasFrame = true;
				pin->mFrameTs.snap(now, READ);
				pin->mFrameCounter++;
			}
		}
	}
	if (hasFrame)
	{
		// in place byteswap so no need for frame conversion buffer
		if (pin->mVideoFormat.pixelFormat.format == pixel_format::AYUV)
		{
			// endianness is wrong on a per pixel basis
			uint32_t sampleIdx = 0;
			#ifdef __AVX__
			__m256i pixelEndianSwap = _mm256_set_epi8(
				12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3,
				12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
			);
			const uint32_t chunks = pin->mVideoFormat.imageSize / 32;
			__m256i* chunkToProcess = reinterpret_cast<__m256i*>(pmsData);
			for (uint32_t i = 0; i < chunks; ++i)
			{
				__m256i swapped = _mm256_shuffle_epi8(_mm256_loadu_si256(chunkToProcess), pixelEndianSwap);
				_mm256_storeu_si256(chunkToProcess, swapped);
				sampleIdx += 8;
				chunkToProcess++;
			}
			#endif
			uint32_t* sampleToProcess = reinterpret_cast<uint32_t*>(pmsData);
			const uint32_t sz = pin->mVideoFormat.imageSize / 4;
			for (; sampleIdx < sz; sampleIdx++)
			{
				sampleToProcess[sampleIdx] = _byteswap_ulong(sampleToProcess[sampleIdx]);
			}
		}
		pin->GetReferenceTime(&now);
		pin->mFrameTs.snap(now, CONVERTED);
		pin->mFrameTs.end();

		pin->mPreviousFrameTime = pin->mCurrentFrameTime;
		pin->mCurrentFrameTime = pin->mFrameTs.get(BUFFERING);
		auto endTime = pin->mCurrentFrameTime;
		auto startTime = endTime - pin->mVideoFormat.frameInterval;
		auto missedFrame = (pin->mCurrentFrameTime - pin->mPreviousFrameTime) >= (pin->mVideoFormat.frameInterval * 2);

		pms->SetTime(&startTime, &endTime);
		pms->SetSyncPoint(TRUE);
		pms->SetDiscontinuity(missedFrame);

		if (pin->mUpdatedMediaType)
		{
			CMediaType cmt(pin->m_mt);
			AM_MEDIA_TYPE* sendMediaType = CreateMediaType(&cmt);
			pms->SetMediaType(sendMediaType);
			DeleteMediaType(sendMediaType);
			pin->mUpdatedMediaType = false;
		}
		pin->AppendHdrSideDataIfNecessary(pms, endTime);
		pin->RecordLatency();
		pin->SnapTemperatureIfNecessary(endTime);

		#ifndef NO_QUILL
		bool isPro = pin->mFilter->GetDeviceType() == MW_PRO;
		if (!pin->mLoggedLatencyHeader)
		{
			pin->mLoggedLatencyHeader = true;
			if (isPro)
			{
				LOG_TRACE_L1(mLogData.videoLat,
				             "idx,waiting,waitComplete,"
				             "bufferAllocated,buffering,buffered,"
				             "reading,read,converted,"
				             "sysTime,actualInterval,expectedInterval,"
				             "deltaInterval,imageSize,missedFrames,"
				             "startTime,endTime");
			}
			else
			{
				LOG_TRACE_L1(mLogData.videoLat,
				             "idx,waitComplete,buffering,"
				             "reading,read,converted,"
				             "sysTime,actualInterval,expectedInterval,"
				             "deltaInterval,imageSize,missedFrames,"
				             "startTime,endTime");
			}
		}
		auto frameInterval = pin->mCurrentFrameTime - pin->mPreviousFrameTime;
		auto sz = pms->GetSize();

		auto vf = pin->mVideoFormat;
		if (vf.imageSize != sz)
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] Video format size mismatch (format: {} buffer: {})", mLogData.prefix,
			             vf.imageSize, sz);
		}
		auto ts = pin->mFrameTs;
		if (isPro)
		{
			LOG_TRACE_L1(mLogData.videoLat,
			             "{},{},{},"
			             "{},{},{},"
			             "{},{},{},"
			             "{},{},{},"
			             "{},{},{},"
			             "{},{}",
			             pin->mFrameCounter, ts.get(WAITING), ts.get(WAIT_COMPLETE),
			             ts.get(BUFFER_ALLOCATED), ts.get(BUFFERING), ts.get(BUFFERED),
			             ts.get(READING), ts.get(READ), ts.get(CONVERTED),
			             ts.get(COMPLETE), frameInterval, vf.frameInterval,
			             frameInterval - vf.frameInterval, vf.imageSize, missedFrame,
			             startTime, endTime);
		}
		else
		{
			LOG_TRACE_L1(mLogData.videoLat,
			             "{},{},{},"
			             "{},{},{},"
			             "{},{},{},"
			             "{},{},{},"
			             "{},{}",
			             pin->mFrameCounter, ts.get(WAIT_COMPLETE), ts.get(BUFFERING),
			             ts.get(READING), ts.get(READ), ts.get(CONVERTED),
			             ts.get(COMPLETE), frameInterval, vf.frameInterval,
			             frameInterval - vf.frameInterval, vf.imageSize, missedFrame,
			             startTime, endTime);
		}
		LOG_TRACE_L1(mLogData.logger, "[{}] Captured frame {} (signal? {})", mLogData.prefix, pin->mFrameCounter,
		             pin->mHasSignal);
		#endif
	}
	else
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] No frame loaded", mLogData.prefix);
		#endif
	}
	return retVal;
}

//////////////////////////////////////////////////////////////////////////
// magewell_video_capture_pin
//////////////////////////////////////////////////////////////////////////
magewell_video_capture_pin::magewell_video_capture_pin(HRESULT* phr, magewell_capture_filter* pParent, bool pPreview) :
	hdmi_video_capture_pin(
		phr,
		pParent,
		pPreview ? "VideoPreview" : "VideoCapture",
		pPreview ? L"Preview" : L"Capture",
		pPreview ? "VideoPreview" : "VideoCapture",
		video_format{},
		{
			{UYVY, {YV16, UYVY_YV16}},
			{YUY2, {YV16, YUY2_YV16}},
			{Y210, {P210, Y210_P210}},
			{BGR10, {RGB48, BGR10_BGR48}},
		},
		pParent->GetDeviceType()
	),
	mNotify(nullptr),
	mCaptureEvent(nullptr),
	mNotifyEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	mPixelFormatMatrix(proPixelFormats)
{
	auto hChannel = mFilter->GetChannelHandle();

	if (mFilter->GetDeviceType() != MW_PRO)
	{
		if (MW_SUCCEEDED == MWUSBGetVideoOutputFOURCC(hChannel, &mUsbCaptureFormats.fourccs))
		{
			if (MW_SUCCEEDED == MWUSBGetVideoOutputFrameInterval(hChannel, &mUsbCaptureFormats.frameIntervals))
			{
				if (MW_SUCCEEDED == MWUSBGetVideoOutputFrameSize(hChannel, &mUsbCaptureFormats.frameSizes))
				{
					mUsbCaptureFormats.usb = true;
					#ifndef NO_QUILL
					{
						std::string tmp{"['"};
						for (int i = 0; i < mUsbCaptureFormats.fourccs.byCount; i++)
						{
							if (i != 0) tmp += "', '";
							uint32_t adw_fourcc = mUsbCaptureFormats.fourccs.adwFOURCCs[i];
							tmp += static_cast<char>(adw_fourcc & 0xFF);
							tmp += static_cast<char>(adw_fourcc >> 8 & 0xFF);
							tmp += static_cast<char>(adw_fourcc >> 16 & 0xFF);
							tmp += static_cast<char>(adw_fourcc >> 24 & 0xFF);
						}
						tmp += "']";
						LOG_INFO(mLogData.logger, "[{}] USB colour formats {}", mLogData.prefix, tmp);
					}
					{
						std::string tmp{"['"};
						for (int i = 0; i < mUsbCaptureFormats.frameIntervals.byCount; i++)
						{
							if (i != 0) tmp += "', '";
							tmp += std::to_string(mUsbCaptureFormats.frameIntervals.adwIntervals[i]);
						}
						tmp += "']";
						LOG_INFO(mLogData.logger, "[{}] USB frame intervals {}", mLogData.prefix, tmp);
					}

					{
						std::string tmp{"['"};
						for (int i = 0; i < mUsbCaptureFormats.frameSizes.byCount; i++)
						{
							if (i != 0) tmp += "', '";
							auto sz = mUsbCaptureFormats.frameSizes.aSizes[i];
							tmp += std::to_string(sz.cx) + "x" + std::to_string(sz.cy);
						}
						tmp += "']";
						LOG_INFO(mLogData.logger, "[{}] USB frame sizes {}", mLogData.prefix, tmp);
					}
					#endif

					std::vector<DWORD> fourccs(mUsbCaptureFormats.fourccs.adwFOURCCs,
					                           std::end(mUsbCaptureFormats.fourccs.adwFOURCCs));
					mPixelFormatMatrix = generatePixelFormatMatrix(mFilter->GetDeviceType(), fourccs);
					#ifndef NO_QUILL
					{
						for (int i = 0; i < bitDepthCount; ++i)
						{
							auto& formats = mPixelFormatMatrix[i];
							auto tmp = std::format("[RGB: {}, 4:2:0: {}, 4:2:2: {}, 4:4:4: {}]",
							                       formats[0].name, formats[3].name, formats[1].name, formats[2].name);
							auto depth = i == 0 ? "8" : i == 1 ? "10" : "12";
							LOG_INFO(mLogData.logger, "[{}] Pixel format mapping: {} bit {}", mLogData.prefix, depth,
							         tmp);
						}
					}
					#endif
				}
				else
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger, "[{}] Could not load USB video frame sizes", mLogData.prefix);
					#endif
				}
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Could not load USB video frame intervals", mLogData.prefix);
				#endif
			}
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Could not load USB video FourCCs", mLogData.prefix);
			#endif
		}
	}

	auto hr = LoadSignal(&hChannel);
	mFilter->OnVideoSignalLoaded(&mVideoSignal);

	if (SUCCEEDED(hr))
	{
		LoadFormat(&mVideoFormat, &mVideoSignal, &mUsbCaptureFormats);

		#ifndef NO_QUILL
		LOG_WARNING(
			mLogData.logger,
			"[{}] Initialised video format {} x {} ({}:{}) @ {:.3f} Hz in {} bits ({} {} tf: {}) size {} bytes",
			mLogData.prefix, mVideoFormat.cx, mVideoFormat.cy, mVideoFormat.aspectX, mVideoFormat.aspectY,
			mVideoFormat.fps, mVideoFormat.pixelFormat.bitDepth, mVideoFormat.pixelFormat.name,
			mVideoFormat.colourFormatName, mVideoFormat.hdrMeta.transferFunction, mVideoFormat.imageSize);
		#endif
	}
	else
	{
		mVideoFormat.CalculateDimensions();

		#ifndef NO_QUILL
		LOG_WARNING(
			mLogData.logger,
			"[{}] Initialised video format using defaults {} x {} ({}:{}) @ {.3f} Hz in {} bits ({} {} tf: {}) size {} bytes",
			mLogData.prefix, mVideoFormat.cx, mVideoFormat.cy, mVideoFormat.aspectX, mVideoFormat.aspectY,
			mVideoFormat.fps, mVideoFormat.pixelFormat.bitDepth, mVideoFormat.pixelFormat.name,
			mVideoFormat.colourFormatName, mVideoFormat.hdrMeta.transferFunction, mVideoFormat.imageSize);
		#endif
	}
	mFilter->OnVideoFormatLoaded(&mVideoFormat);

	ResizeMetrics(mVideoFormat.fps);

	mCapturedFrame.data = new uint8_t[mVideoFormat.imageSize];
}

magewell_video_capture_pin::~magewell_video_capture_pin()
{
	CloseHandle(mNotifyEvent);
}

void magewell_video_capture_pin::DoThreadDestroy()
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

void magewell_video_capture_pin::LoadFormat(video_format* videoFormat, video_signal* videoSignal,
                                            const usb_capture_formats* captureFormats)
{
	auto subsampling = RGB_444;
	auto bitDepth = 8;
	if (videoSignal->signalStatus.state == MWCAP_VIDEO_SIGNAL_LOCKED)
	{
		videoFormat->cx = videoSignal->signalStatus.cx;
		videoFormat->cy = videoSignal->signalStatus.cy;
		videoFormat->aspectX = videoSignal->signalStatus.nAspectX;
		videoFormat->aspectY = videoSignal->signalStatus.nAspectY;
		videoFormat->quantisation = static_cast<quantisation_range>(videoSignal->signalStatus.quantRange);
		videoFormat->saturation = static_cast<saturation_range>(videoSignal->signalStatus.satRange);
		videoFormat->fps = static_cast<double>(dshowTicksPerSecond) / videoSignal->signalStatus.dwFrameDuration;
		videoFormat->frameInterval = videoSignal->signalStatus.dwFrameDuration;
		videoFormat->colourFormat = static_cast<colour_format>(videoSignal->signalStatus.colorFormat);

		bitDepth = videoSignal->inputStatus.hdmiStatus.byBitDepth;
		subsampling = static_cast<pixel_encoding>(videoSignal->inputStatus.hdmiStatus.pixelEncoding);

		LoadHdrMeta(&videoFormat->hdrMeta, &videoSignal->hdrInfo);
	}
	else
	{
		// invalid/no signal is 720x480 RGB 4:4:4 image 
		videoFormat->cx = 720;
		videoFormat->cy = 480;
		videoFormat->quantisation = QUANTISATION_FULL;
		videoFormat->saturation = SATURATION_FULL;
		videoFormat->colourFormat = RGB;
	}

	auto pfIdx = bitDepth == 8 ? 0 : bitDepth == 10 ? 1 : 2;

	auto deviceType = mFilter->GetDeviceType();
	videoFormat->bottomUpDib = deviceType == MW_PRO;
	videoFormat->pixelFormat = mPixelFormatMatrix[pfIdx][subsampling];
	if (videoFormat->colourFormat == REC709)
	{
		videoFormat->colourFormatName = "REC709";
	}
	else if (videoFormat->colourFormat == BT2020)
	{
		videoFormat->colourFormatName = "BT2020";
	}
	else if (videoFormat->colourFormat == RGB)
	{
		videoFormat->colourFormatName = "RGB";
	}
	else
	{
		videoFormat->colourFormatName = "UNK";
	}

	if (captureFormats->usb)
	{
		bool found = false;
		for (int i = 0; i < captureFormats->fourccs.byCount && !found; i++)
		{
			uint32_t adw_fourcc = captureFormats->fourccs.adwFOURCCs[i];
			if (adw_fourcc == videoFormat->pixelFormat.fourcc)
			{
				found = true;
			}
		}
		if (!found)
		{
			#ifndef NO_QUILL
			std::string tmp{"['"};
			for (int i = 0; i < captureFormats->fourccs.byCount; i++)
			{
				if (i != 0) tmp += "', '";
				uint32_t adw_fourcc = captureFormats->fourccs.adwFOURCCs[i];
				tmp += static_cast<char>(adw_fourcc & 0xFF);
				tmp += static_cast<char>(adw_fourcc >> 8 & 0xFF);
				tmp += static_cast<char>(adw_fourcc >> 16 & 0xFF);
				tmp += static_cast<char>(adw_fourcc >> 24 & 0xFF);
			}
			tmp += "']";

			LOG_ERROR(mLogData.logger,
			          "[{}] Supported format is {} but card is configured to use {}, video capture will fail",
			          mLogData.prefix, videoFormat->pixelFormat.name, tmp);
			#endif
		}

		found = false;
		for (int i = 0; i < captureFormats->frameIntervals.byCount && !found; i++)
		{
			if (abs(captureFormats->frameIntervals.adwIntervals[i] - videoFormat->frameInterval) < 100)
			{
				found = true;
			}
		}
		if (!found)
		{
			auto requestedInterval = videoFormat->frameInterval;
			videoFormat->frameInterval = captureFormats->frameIntervals.adwIntervals[captureFormats->frameIntervals.
				byDefault];
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Requested frame interval {} is not configured, using default of {} instead",
			            mLogData.prefix, requestedInterval, videoFormat->frameInterval);
			#endif
		}

		found = false;
		for (int i = 0; i < captureFormats->frameSizes.byCount && !found; i++)
		{
			if (captureFormats->frameSizes.aSizes[i].cx == videoFormat->cx && captureFormats->frameSizes.aSizes[i].cy ==
				videoFormat->cy)
			{
				found = true;
			}
		}
		if (!found)
		{
			auto requestedCx = videoFormat->cx;
			auto requestedCy = videoFormat->cy;
			videoFormat->cx = captureFormats->frameSizes.aSizes[captureFormats->frameSizes.byDefault].cx;
			videoFormat->cy = captureFormats->frameSizes.aSizes[captureFormats->frameSizes.byDefault].cy;

			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Requested frame dimension {}x{} is not configured, using default of {}x{} instead",
			            mLogData.prefix, requestedCx, requestedCy, videoFormat->cx, videoFormat->cy);
			#endif
		}
	}

	videoFormat->CalculateDimensions();
}

HRESULT magewell_video_capture_pin::LoadSignal(HCHANNEL* pChannel)
{
	auto hr = MWGetVideoSignalStatus(*pChannel, &mVideoSignal.signalStatus);
	auto retVal = S_OK;
	if (hr != MW_SUCCEEDED)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] LoadSignal MWGetVideoSignalStatus failed", mLogData.prefix);
		#endif

		mVideoSignal.signalStatus.state = MWCAP_VIDEO_SIGNAL_NONE;

		retVal = S_FALSE;
	}
	hr = MWGetInputSpecificStatus(*pChannel, &mVideoSignal.inputStatus);
	if (hr != MW_SUCCEEDED)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] LoadSignal MWGetInputSpecificStatus failed", mLogData.prefix);
		#endif

		mVideoSignal.inputStatus.bValid = false;

		retVal = S_FALSE;
	}
	else if (!mVideoSignal.inputStatus.bValid)
	{
		retVal = S_FALSE;
	}

	if (retVal != S_OK)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger,
		          "[{}] LoadSignal MWGetInputSpecificStatus is invalid, will display no/unsupported signal image",
		          mLogData.prefix);
		#endif

		mVideoSignal.inputStatus.hdmiStatus.byBitDepth = 8;
		mVideoSignal.inputStatus.hdmiStatus.pixelEncoding = HDMI_ENCODING_RGB_444;
		mHasHdrInfoFrame = true;
		mVideoSignal.hdrInfo = {};
		mVideoSignal.aviInfo = {};
	}
	else
	{
		DWORD tPdwValidFlag = 0;
		MWGetHDMIInfoFrameValidFlag(*pChannel, &tPdwValidFlag);
		HDMI_INFOFRAME_PACKET pkt;
		auto readPacket = false;
		if (tPdwValidFlag & MWCAP_HDMI_INFOFRAME_MASK_HDR)
		{
			if (MW_SUCCEEDED == MWGetHDMIInfoFramePacket(*pChannel, MWCAP_HDMI_INFOFRAME_ID_HDR, &pkt))
			{
				if (!mHasHdrInfoFrame)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR Infoframe is present tf: {} to {}", mLogData.prefix,
					             mVideoSignal.hdrInfo.byEOTF,
					             pkt.hdrInfoFramePayload.byEOTF);
					#endif
					mHasHdrInfoFrame = true;
				}
				mVideoSignal.hdrInfo = pkt.hdrInfoFramePayload;
				readPacket = true;
			}
		}
		if (!readPacket)
		{
			if (mHasHdrInfoFrame)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] HDR Infoframe no longer present", mLogData.prefix);
				#endif
				mHasHdrInfoFrame = false;
			}
			mVideoSignal.hdrInfo = {};
		}

		readPacket = false;
		if (tPdwValidFlag & MWCAP_HDMI_INFOFRAME_MASK_AVI)
		{
			if (MW_SUCCEEDED == MWGetHDMIInfoFramePacket(*pChannel, MWCAP_HDMI_INFOFRAME_ID_AVI, &pkt))
			{
				mVideoSignal.aviInfo = pkt.aviInfoFramePayload;
				readPacket = true;
			}
		}
		if (!readPacket)
		{
			mVideoSignal.aviInfo = {};
		}
	}
	return S_OK;
}

void magewell_video_capture_pin::OnFrameWriterStrategyUpdated()
{
	switch (mFrameWriterStrategy)
	{
	case STRAIGHT_THROUGH:
		mFrameWriter = std::make_unique<straight_through>(mLogData, mVideoFormat.cx, mVideoFormat.cy,
		                                                  &mVideoFormat.pixelFormat);
		break;
	case ANY_RGB:
	case YUV2_YV16:
	case R210_BGR48:
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Conversion strategy {} is not supported by mwcapture",
		          mLogData.prefix, to_string(mFrameWriterStrategy));
		#endif
		break;
	default:
		hdmi_video_capture_pin::OnFrameWriterStrategyUpdated();
	}
	// has to happen after signalledFormat has been updated
	auto resetVideoCapture = mFilter->GetDeviceType() != MW_PRO && !mFirst;
	if (resetVideoCapture && mVideoCapture)
	{
		mVideoCapture.reset();
	}

	if (mVideoFormat.imageSize > mVideoFormat.imageSize)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] Resetting capturedFrame.data to accommodate new image size",
		             mLogData.prefix, mVideoFormat.imageSize);
		#endif

		CAutoLock lck(&mCaptureCritSec);
		delete mCapturedFrame.data;
		mCapturedFrame.data = new uint8_t[mVideoFormat.imageSize];
		mCapturedFrame.length = 0;
		mCapturedFrame.ts = 0;
	}

	if (resetVideoCapture)
	{
		mVideoCapture = std::make_unique<VideoCapture>(this, mFilter->GetChannelHandle());
	}
}

void magewell_video_capture_pin::OnChangeMediaType()
{
	video_capture_pin::OnChangeMediaType();

	mFilter->NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(mVideoFormat.cx, mVideoFormat.cy), 0);
}

void magewell_video_capture_pin::CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam)
{
	magewell_video_capture_pin* pin = static_cast<magewell_video_capture_pin*>(pParam);

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
		LOG_TRACE_L3(pin->mLogData.logger, "[{}] Notifying frame at {} with len {}", pin->mLogData.prefix, ts, cbFrame);
		#endif
	}
}

// loops til we have a frame to process, dealing with any mediatype changes as we go and then grabs a buffer once it's time to go
HRESULT magewell_video_capture_pin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                                      REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hasFrame = false;
	auto retVal = S_FALSE;
	auto proDevice = mFilter->GetDeviceType() == MW_PRO;
	auto hChannel = mFilter->GetChannelHandle();
	int64_t now;

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
		auto channel = mFilter->GetChannelHandle();
		auto hr = LoadSignal(&channel);
		auto hadSignal = mHasSignal == true;

		mHasSignal = true;

		if (FAILED(hr))
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Can't load signal", mLogData.prefix);
			#endif

			mHasSignal = false;
		}
		if (mVideoSignal.signalStatus.state != MWCAP_VIDEO_SIGNAL_LOCKED)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Signal is not locked ({})", mLogData.prefix,
			             static_cast<int>(mVideoSignal.signalStatus.state));
			#endif

			mHasSignal = false;
		}
		if (mVideoSignal.inputStatus.hdmiStatus.byBitDepth == 0)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Reported bit depth is 0", mLogData.prefix);
			#endif

			mHasSignal = false;
		}

		video_format newVideoFormat;
		LoadFormat(&newVideoFormat, &mVideoSignal, &mUsbCaptureFormats);

		auto shouldResizeMetrics = newVideoFormat.frameInterval != mVideoFormat.frameInterval;

		auto onSignalResult = OnVideoSignal(newVideoFormat);

		if (onSignalResult != S_RECONNECTION_UNNECESSARY || (hadSignal && !mHasSignal))
		{
			mFilter->OnVideoSignalLoaded(&mVideoSignal);
		}

		if (FAILED(onSignalResult))
		{
			BACKOFF;
			continue;
		}

		if (shouldResizeMetrics)
		{
			ResizeMetrics(mVideoFormat.fps);
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
			if (proDevice)
			{
				// wait til we see a BUFFERING notification
				auto hr = MWGetNotifyStatus(hChannel, mNotify, &mStatusBits);
				if (hr != MW_SUCCEEDED)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] MWGetNotifyStatus failed {}", mLogData.prefix,
					             static_cast<int>(hr));
					#endif

					mFrameTs.reset();
					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Video signal change, retry after backoff", mLogData.prefix);
					#endif

					mFrameTs.reset();
					BACKOFF;
					continue;
				}
				if (mStatusBits & MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Video input source change, retry after backoff",
					             mLogData.prefix);
					#endif

					mFrameTs.reset();
					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING)
				{
					hasFrame = true;
				}

				if (!mHasSignal)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] No signal will be displayed ", mLogData.prefix);
					#endif

					hasFrame = true;
				}
			}
			else
			{
				// new frame is the only type of notification but only handle if frame length fits
				CAutoLock lck(&mCaptureCritSec);
				if (mVideoFormat.imageSize < mCapturedFrame.length)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger,
					             "[{}] Discarding frame larger than expected format size (vf: {}, cap: {}, reconnected: {:#08x})",
					             mLogData.prefix, mVideoFormat.imageSize, mCapturedFrame.length, onSignalResult);
					#endif
				}
				else
				{
					hasFrame = true;
				}
			}

			if (hasFrame)
			{
				GetReferenceTime(&now);
				mFrameTs.snap(now, WAIT_COMPLETE);
				retVal = video_capture_pin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
				GetReferenceTime(&now);
				mFrameTs.snap(now, BUFFER_ALLOCATED);

				if (!SUCCEEDED(retVal))
				{
					hasFrame = false;
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] Video frame buffered but unable to get delivery buffer, retry after backoff",
					            mLogData.prefix);
					#endif
				}
			}

			if (!hasFrame)
			{
				mFrameTs.reset();
				SHORT_BACKOFF;
			}
		}
		else
		{
			if (!mHasSignal && dwRet == STATUS_TIMEOUT)
			{
				mFrameTs.reset();

				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Timeout and no signal, get delivery buffer for no signal image",
				             mLogData.prefix);
				#endif

				retVal = video_capture_pin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);

				if (!SUCCEEDED(retVal))
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger, "[{}] Unable to get delivery buffer, retry after backoff",
					            mLogData.prefix);
					#endif
					SHORT_BACKOFF;
				}
				else
				{
					hasFrame = true;
				}
			}
			else if (dwRet == STATUS_TIMEOUT)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] No frame arrived within timeout", mLogData.prefix);
				#endif
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Wait for frame unexpected response ({:#08x})", mLogData.prefix,
				            dwRet);
				#endif
			}
		}
	}

	if (hasFrame && mFirst)
	{
		OnChangeMediaType();
	}

	return retVal;
}

HRESULT magewell_video_capture_pin::FillBuffer(IMediaSample* pms)
{
	video_frame_grabber vfg(this, mFilter->GetChannelHandle(), mFilter->GetDeviceType(), pms);
	auto retVal = vfg.grab();
	if (S_FALSE == HandleStreamStateChange(pms))
	{
		retVal = S_FALSE;
	}
	return retVal;
}

HRESULT magewell_video_capture_pin::OnThreadCreate()
{
	hdmi_video_capture_pin::OnThreadCreate();
	bool enabled;
	mFilter->IsHighThreadPriorityEnabled(&enabled);
	if (enabled)
	{
		BumpThreadPriority();
	}

	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] magewell_video_capture_pin::OnThreadCreate", mLogData.prefix);
	#endif

	UpdateDisplayStatus();

	mRateSwitcher.InitIfNecessary();

	auto hChannel = mFilter->GetChannelHandle();
	LoadSignal(&hChannel);

	mFilter->OnVideoSignalLoaded(&mVideoSignal);

	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == MW_PRO)
	{
		// start capture
		mCaptureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		auto hr = MWStartVideoCapture(hChannel, mCaptureEvent);
		#ifndef NO_QUILL
		if (hr != MW_SUCCEEDED)
		{
			LOG_ERROR(mLogData.logger, "[{}] Unable to MWStartVideoCapture", mLogData.prefix);
		}
		else
		{
			LOG_INFO(mLogData.logger, "[{}] MWStartVideoCapture started", mLogData.prefix);
		}
		#endif

		// register for signal change events & video buffering 
		mNotify = MWRegisterNotify(hChannel, mNotifyEvent,
		                           MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE |
		                           MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING |
		                           MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE);
		if (!mNotify)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to MWRegistryNotify", mLogData.prefix);
			#endif

			// TODO throw
		}
	}
	else
	{
		if (mVideoCapture) mVideoCapture.reset();
		mVideoCapture = std::make_unique<VideoCapture>(this, mFilter->GetChannelHandle());
	}
	return NOERROR;
}

void magewell_video_capture_pin::StopCapture()
{
	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == MW_PRO)
	{
		MWStopVideoCapture(mFilter->GetChannelHandle());
	}
	else if (mVideoCapture)
	{
		mVideoCapture.reset();
	}
}
