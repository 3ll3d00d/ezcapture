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
#pragma once

#define NOMINMAX // quill does not compile without this

#include <windows.h>
#include <process.h>
#include <DXVA.h>
#include <filesystem>
#include <utility>
// linking side data GUIDs fails without this
#include <initguid.h>

#ifndef NO_QUILL
#include "quill/Backend.h"
#include "quill/sinks/FileSink.h"
#include <quill/StopWatch.h>
#endif

#include "mwcapture.h"

#include <cmath>
#include <algorithm>
#include <ranges>

#include "StraightThrough.h"

#define S_PARTIAL_DATABURST    ((HRESULT)2L)
#define S_POSSIBLE_BITSTREAM    ((HRESULT)3L)
#define S_NO_CHANNELS    ((HRESULT)2L)

#if CAPTURE_NAME_SUFFIX == 1
#define LOG_PREFIX_NAME "MagewellCaptureFilterTrace"
#define WLOG_PREFIX_NAME L"MagewellCaptureFilterTrace"
#elif CAPTURE_NAME_SUFFIX == 2
#define LOG_PREFIX_NAME "MagewellCaptureFilterWarn"
#define WLOG_PREFIX_NAME L"MagewellCaptureFilterWarn"
#else
#define LOG_PREFIX_NAME "MagewellCaptureFilter"
#define WLOG_PREFIX_NAME L"MagewellCaptureFilter"
#endif

constexpr auto bitstreamDetectionWindowSecs = 0.075;
constexpr auto bitstreamDetectionRetryAfter = 1.0 / bitstreamDetectionWindowSecs;
constexpr auto bitstreamBufferSize = 6144;

#ifdef __AVX__
const __m256i endianSwap = _mm256_set_epi8(
	12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3,
	12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
);
#endif

//////////////////////////////////////////////////////////////////////////
// MagewellCaptureFilter
//////////////////////////////////////////////////////////////////////////
CUnknown* MagewellCaptureFilter::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{
	auto pNewObject = new MagewellCaptureFilter(punk, phr);

	if (pNewObject == nullptr)
	{
		if (phr)
			*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

MagewellCaptureFilter::MagewellCaptureFilter(LPUNKNOWN punk, HRESULT* phr) :
	HdmiCaptureFilter(WLOG_PREFIX_NAME, punk, phr, CLSID_MWCAPTURE_FILTER, LOG_PREFIX_NAME)
{
	// Initialise the device and validate that it presents some form of data
	mInited = MWCaptureInitInstance();
	#ifndef NO_QUILL
	if (!mInited)
	{
		LOG_ERROR(mLogData.logger, "[{}] Unable to init", mLogData.prefix);
	}
	#endif
	CAutoLock lck(&m_cStateLock);
	DEVICE_INFO* diToUse(nullptr);
	int channelCount = MWGetChannelCount();
	// TODO read HKEY_LOCAL_MACHINE L"Software\\mwcapture\\devicepath"
	for (int i = 0; i < channelCount; i++)
	{
		DEVICE_INFO di{};
		MWCAP_CHANNEL_INFO mci;
		MWGetChannelInfoByIndex(i, &mci);
		if (mci.wFamilyID == MW_FAMILY_ID_PRO_CAPTURE)
		{
			di.deviceType = PRO;
			di.serialNo = std::string{mci.szBoardSerialNo};
			MWCAP_PRO_CAPTURE_INFO info;
			MWGetFamilyInfoByIndex(i, &info, sizeof(MWCAP_PRO_CAPTURE_INFO));
			di.linkSpeed = info.byLinkType;
			di.linkWidth = info.byLinkWidth;
			di.maxPayloadSize = info.wMaxPayloadSize;
			di.maxReadRequestSize = info.wMaxReadRequestSize;
		}
		else if (mci.wFamilyID == MW_FAMILY_ID_USB_CAPTURE)
		{
			if (0 == strcmp(mci.szProductName, "USB Capture HDMI 4K+"))
			{
				di.deviceType = USB_PLUS;
			}
			else if (0 == strcmp(mci.szProductName, "USB Capture HDMI 4K Pro"))
			{
				di.deviceType = USB_PRO;
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Ignoring unknown USB device type {} with serial no {}",
				            mLogData.prefix, mci.szProductName, di.serialNo);
				#endif
				continue;
			}
			di.serialNo = std::string{mci.szBoardSerialNo};
			// TODO use MWCAP_DEVICE_NAME_MODE and MWUSBGetDeviceNameMode mode?
			MWCAP_USB_CAPTURE_INFO info;
			MWGetFamilyInfoByIndex(i, &info, sizeof(MWCAP_USB_CAPTURE_INFO));
			di.linkSpeed = info.byUSBSpeed;
		}

		MWGetDevicePath(i, di.devicePath);
		di.hChannel = MWOpenChannelByPath(di.devicePath);
		if (di.hChannel == nullptr)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to open channel on {} device {} at path {}, ignoring",
			            mLogData.prefix,
			            devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
			#endif
			continue;
		}
		DWORD videoInputTypeCount = 0;
		if (MW_SUCCEEDED != MWGetVideoInputSourceArray(di.hChannel, nullptr, &videoInputTypeCount))
		{
			MWCloseChannel(di.hChannel);
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to detect video inputs on {} device {} at path {}, ignoring",
			            mLogData.prefix,
			            devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
			#endif
			continue;
		}
		DWORD videoInputTypes[16] = {0};
		if (MW_SUCCEEDED != MWGetVideoInputSourceArray(di.hChannel, videoInputTypes, &videoInputTypeCount))
		{
			MWCloseChannel(di.hChannel);
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Unable to load supported video input types on {} device {} at path {}, ignoring",
			            mLogData.prefix,
			            devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
			#endif
			continue;
		}
		bool hdmiFound = false;
		for (auto j = 0; j < videoInputTypeCount && !hdmiFound; j++)
		{
			if (INPUT_TYPE(videoInputTypes[j]) == MWCAP_VIDEO_INPUT_TYPE_HDMI)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
				            "[{}] Found HDMI input at position {} on {} device {} at path {}, ignoring",
				            mLogData.prefix,
				            j, devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
				#endif
				hdmiFound = true;
			}
		}
		if (!hdmiFound)
		{
			MWCloseChannel(di.hChannel);
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Found device but no HDMI input available on {} device {} at path {}, ignoring",
			            mLogData.prefix,
			            devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
			#endif
			continue;
		}

		// TODO match against targetPath
		if (diToUse == nullptr) // && di.devicePath == targetPath || targetPath == nullptr
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Filter will use {} device {} at path {}", mLogData.prefix,
			         devicetype_to_name(di.deviceType),
			         di.serialNo, std::wstring{ di.devicePath });
			#endif

			diToUse = &di;
			MWGetDevicePath(i, mDeviceInfo.devicePath);
			mDeviceInfo.serialNo += diToUse->serialNo;
			mDeviceInfo.deviceType = diToUse->deviceType;
			mDeviceInfo.hChannel = diToUse->hChannel;
			mDeviceInfo.linkSpeed = diToUse->linkSpeed;
			mDeviceInfo.linkWidth = diToUse->linkWidth;
			mDeviceInfo.maxPayloadSize = diToUse->maxPayloadSize;
			mDeviceInfo.maxReadRequestSize = diToUse->maxReadRequestSize;
			SnapHardwareDetails();
		}
		else
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Ignoring usable {} device {} at path {}", mLogData.prefix,
			         devicetype_to_name(di.deviceType),
			         di.serialNo, std::wstring{ di.devicePath });
			#endif

			MWCloseChannel(di.hChannel);
			di.hChannel = nullptr;
		}
	}


	if (diToUse == nullptr)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "No valid channels found");
		#endif

		// TODO throw
	}
	else
	{
		MagewellCaptureFilter::OnDeviceUpdated();
	}

	mClock = new MWReferenceClock(phr, mDeviceInfo.hChannel, mDeviceInfo.deviceType == PRO);

	auto pin = new MagewellVideoCapturePin(phr, this, false);
	pin->UpdateFrameWriterStrategy();
	pin = new MagewellVideoCapturePin(phr, this, true);
	pin->UpdateFrameWriterStrategy();

	new MagewellAudioCapturePin(phr, this, false);
	new MagewellAudioCapturePin(phr, this, true);
}

MagewellCaptureFilter::~MagewellCaptureFilter()
{
	if (mInited)
	{
		MWCaptureExitInstance();
	}
}

void MagewellCaptureFilter::SnapHardwareDetails()
{
	if (mDeviceInfo.deviceType == PRO)
	{
		uint32_t temp;
		MWGetTemperature(mDeviceInfo.hChannel, &temp);
		mDeviceInfo.temperature = temp;
	}
	else if (mDeviceInfo.deviceType == USB_PLUS)
	{
		int16_t temp;
		MWUSBGetCoreTemperature(mDeviceInfo.hChannel, &temp);
		mDeviceInfo.temperature = temp;
	}
	else if (mDeviceInfo.deviceType == USB_PRO)
	{
		int16_t val;
		MWUSBGetCoreTemperature(mDeviceInfo.hChannel, &val);
		mDeviceInfo.temperature = val;
		MWUSBGetFanSpeed(mDeviceInfo.hChannel, &val);
		mDeviceInfo.fanSpeed = val;
	}
}

void MagewellCaptureFilter::OnVideoSignalLoaded(VIDEO_SIGNAL* vs)
{
	mVideoInputStatus.inX = vs->signalStatus.cx;
	mVideoInputStatus.inY = vs->signalStatus.cy;
	mVideoInputStatus.inAspectX = vs->signalStatus.nAspectX;
	mVideoInputStatus.inAspectY = vs->signalStatus.nAspectY;
	mVideoInputStatus.inFps = vs->signalStatus.dwFrameDuration > 0
		                          ? static_cast<double>(dshowTicksPerSecond) / vs->signalStatus.dwFrameDuration
		                          : 0.0;
	mVideoInputStatus.inFrameDuration = vs->signalStatus.dwFrameDuration;

	switch (vs->signalStatus.state)
	{
	case MWCAP_VIDEO_SIGNAL_NONE:
		mVideoInputStatus.signalStatus = "No Signal";
		break;
	case MWCAP_VIDEO_SIGNAL_UNSUPPORTED:
		mVideoInputStatus.signalStatus = "Unsupported Signal";
		break;
	case MWCAP_VIDEO_SIGNAL_LOCKING:
		mVideoInputStatus.signalStatus = "Locking";
		break;
	case MWCAP_VIDEO_SIGNAL_LOCKED:
		mVideoInputStatus.signalStatus = "Locked";
		break;
	}

	switch (vs->signalStatus.colorFormat)
	{
	case MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN:
		mVideoInputStatus.inColourFormat = "?";
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_RGB:
		mVideoInputStatus.inColourFormat = "RGB";
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV601:
		mVideoInputStatus.inColourFormat = "REC601";
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV709:
		mVideoInputStatus.inColourFormat = "REC709";
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020:
		mVideoInputStatus.inColourFormat = "BT2020";
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020C:
		mVideoInputStatus.inColourFormat = "BT2020C";
		break;
	}

	switch (vs->signalStatus.quantRange)
	{
	case MWCAP_VIDEO_QUANTIZATION_UNKNOWN:
		mVideoInputStatus.inQuantisation = "?";
		break;
	case MWCAP_VIDEO_QUANTIZATION_LIMITED:
		mVideoInputStatus.inQuantisation = "Limited";
		break;
	case MWCAP_VIDEO_QUANTIZATION_FULL:
		mVideoInputStatus.inQuantisation = "Full";
		break;
	}

	switch (vs->signalStatus.satRange)
	{
	case MWCAP_VIDEO_SATURATION_UNKNOWN:
		mVideoInputStatus.inSaturation = "?";
		break;
	case MWCAP_VIDEO_SATURATION_LIMITED:
		mVideoInputStatus.inSaturation = "Limited";
		break;
	case MWCAP_VIDEO_SATURATION_FULL:
		mVideoInputStatus.inSaturation = "Full";
		break;
	case MWCAP_VIDEO_SATURATION_EXTENDED_GAMUT:
		mVideoInputStatus.inSaturation = "Extended";
		break;
	}

	mVideoInputStatus.validSignal = vs->inputStatus.bValid;
	mVideoInputStatus.inBitDepth = vs->inputStatus.hdmiStatus.byBitDepth;

	switch (vs->inputStatus.hdmiStatus.pixelEncoding)
	{
	case HDMI_ENCODING_YUV_420:
		mVideoInputStatus.inPixelLayout = "YUV 4:2:0";
		break;
	case HDMI_ENCODING_YUV_422:
		mVideoInputStatus.inPixelLayout = "YUV 4:2:2";
		break;
	case HDMI_ENCODING_YUV_444:
		mVideoInputStatus.inPixelLayout = "YUV 4:4:4";
		break;
	case HDMI_ENCODING_RGB_444:
		mVideoInputStatus.inPixelLayout = "RGB 4:4:4";
		break;
	}

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mVideoInputStatus);
	}
}

void MagewellCaptureFilter::OnAudioSignalLoaded(AUDIO_SIGNAL* as)
{
	// TODO always false, is it a bug in SDK?
	// mStatusInfo.audioInStatus = as->signalStatus.bChannelStatusValid;
	mAudioInputStatus.audioInStatus = as->signalStatus.cBitsPerSample > 0;
	mAudioInputStatus.audioInIsPcm = as->signalStatus.bLPCM;
	mAudioInputStatus.audioInBitDepth = as->signalStatus.cBitsPerSample;
	mAudioInputStatus.audioInFs = as->signalStatus.dwSampleRate;
	mAudioInputStatus.audioInChannelPairs = as->signalStatus.wChannelValid;
	mAudioInputStatus.audioInChannelMap = as->audioInfo.byChannelAllocation;
	mAudioInputStatus.audioInLfeLevel = as->audioInfo.byLFEPlaybackLevel;

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mAudioInputStatus);
	}
}

void MagewellCaptureFilter::OnDeviceUpdated()
{
	mDeviceStatus.deviceDesc = devicetype_to_name(mDeviceInfo.deviceType);
	mDeviceStatus.deviceDesc += " [";
	mDeviceStatus.deviceDesc += mDeviceInfo.serialNo;
	mDeviceStatus.deviceDesc += "]";
	mDeviceStatus.temperature = mDeviceInfo.temperature / 10.0;
	mDeviceStatus.linkSpeed = mDeviceInfo.linkSpeed;
	mDeviceStatus.fanSpeed = mDeviceInfo.fanSpeed;
	if (mDeviceInfo.deviceType == PRO)
	{
		mDeviceStatus.linkWidth = mDeviceInfo.linkWidth;
		mDeviceStatus.maxReadRequestSize = mDeviceInfo.maxReadRequestSize;
		mDeviceStatus.maxPayloadSize = mDeviceInfo.maxPayloadSize;
	}
	else
	{
		mDeviceStatus.protocol = USB;
	}

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Recorded device description: {}", mLogData.prefix, mDeviceStatus.deviceDesc);
	#endif

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mDeviceStatus);
	}
}

HCHANNEL MagewellCaptureFilter::GetChannelHandle() const
{
	return mDeviceInfo.hChannel;
}

DeviceType MagewellCaptureFilter::GetDeviceType() const
{
	return mDeviceInfo.deviceType;
}

//////////////////////////////////////////////////////////////////////////
//  MagewellVideoCapturePin::VideoCapture
//////////////////////////////////////////////////////////////////////////
MagewellVideoCapturePin::VideoCapture::VideoCapture(MagewellVideoCapturePin* pin, HCHANNEL hChannel) :
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

MagewellVideoCapturePin::VideoCapture::~VideoCapture()
{
	if (mEvent != nullptr)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] ~VideoCapture", mLogData.prefix);
		#endif

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
//  MagewellVideoCapturePin::VideoFrameGrabber
//////////////////////////////////////////////////////////////////////////
MagewellVideoCapturePin::VideoFrameGrabber::VideoFrameGrabber(MagewellVideoCapturePin* pin,
                                                              HCHANNEL hChannel, DeviceType deviceType,
                                                              IMediaSample* pms) :
	mLogData(pin->mLogData),
	hChannel(hChannel),
	deviceType(deviceType),
	pin(pin),
	pms(pms)
{
	this->pms->GetPointer(&pmsData);

	if (deviceType == PRO)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] Pinning {} bytes", this->mLogData.prefix, this->pms->GetSize());
		#endif
		MWPinVideoBuffer(hChannel, pmsData, this->pms->GetSize());
	}
}

MagewellVideoCapturePin::VideoFrameGrabber::~VideoFrameGrabber()
{
	if (deviceType == PRO)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] Unpinning {} bytes, captured {} bytes", mLogData.prefix, pms->GetSize(),
		             pms->GetActualDataLength());
		#endif

		MWUnpinVideoBuffer(hChannel, pmsData);
	}
}

HRESULT MagewellVideoCapturePin::VideoFrameGrabber::grab() const
{
	auto retVal = S_OK;
	auto hasFrame = false;
	auto proDevice = deviceType == PRO;
	auto mustExit = false;
	LONGLONG frameTime = 0LL;
	uint64_t convLat = 0LL;
	while (!hasFrame && !mustExit)
	{
		if (proDevice)
		{
			pin->mLastMwResult = MWGetVideoBufferInfo(hChannel, &pin->mVideoSignal.bufferInfo);
			if (pin->mLastMwResult != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Can't get VideoBufferInfo ({})", mLogData.prefix,
				             static_cast<int>(pin->mLastMwResult));
				#endif

				continue;
			}

			pin->mLastMwResult = MWGetVideoFrameInfo(hChannel, pin->mVideoSignal.bufferInfo.iNewestBuffered,
			                                         &pin->mVideoSignal.frameInfo);
			if (pin->mLastMwResult != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Can't get VideoFrameInfo ({})", mLogData.prefix,
				             static_cast<int>(pin->mLastMwResult));
				#endif

				continue;
			}

			frameTime = pin->mVideoSignal.frameInfo.allFieldBufferedTimes[0];

			uint8_t* writeBuffer = pin->mFrameWriterStrategy == STRAIGHT_THROUGH ? pmsData : pin->mCapturedFrame.data;

			pin->mLastMwResult = MWCaptureVideoFrameToVirtualAddressEx(
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
			if (pin->mLastMwResult != MW_SUCCEEDED)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
				            "[{}] Unexpected failed call to MWCaptureVideoFrameToVirtualAddressEx ({})",
				            mLogData.prefix,
				            static_cast<int>(pin->mLastMwResult));
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
					LOG_TRACE_L1(mLogData.logger, "[{}] Unexpected capture event ({:#08x})", mLogData.prefix,
					             static_cast<unsigned long>(dwRet));
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

				pin->mLastMwResult = MWGetVideoCaptureStatus(hChannel, &pin->mVideoSignal.captureStatus);

				#ifndef NO_QUILL
				if (pin->mLastMwResult != MW_SUCCEEDED)
				{
					LOG_TRACE_L1(mLogData.logger, "[{}] MWGetVideoCaptureStatus failed ({})", mLogData.prefix,
					             static_cast<int>(pin->mLastMwResult));
				}
				#endif

				hasFrame = pin->mVideoSignal.captureStatus.bFrameCompleted;
			}
			while (pin->mLastMwResult == MW_SUCCEEDED && !hasFrame);

			if (hasFrame)
			{
				pin->SnapCaptureTime();

				auto t1 = std::chrono::high_resolution_clock::now();
				if (pin->mFrameWriterStrategy != STRAIGHT_THROUGH)
				{
					VideoSampleBuffer buffer{
						.index = pin->mFrameCounter,
						.data = pin->mCapturedFrame.data,
						.width = pin->mVideoFormat.cx,
						.height = pin->mVideoFormat.cy
					};
					pin->mFrameWriter->WriteTo(&buffer, pms);
				}
				auto t2 = std::chrono::high_resolution_clock::now();
				convLat = duration_cast<std::chrono::microseconds>(t2 - t1).count();
			}
		}
		else
		{
			frameTime = pin->mCapturedFrame.ts;
			CAutoLock lck(&pin->mCaptureCritSec);

			auto t1 = std::chrono::high_resolution_clock::now();
			if (pin->mFrameWriterStrategy == STRAIGHT_THROUGH)
			{
				memcpy(pmsData, pin->mCapturedFrame.data, pin->mCapturedFrame.length);
			}
			else
			{
				VideoSampleBuffer buffer{
					.index = pin->mFrameCounter,
					.data = pin->mCapturedFrame.data,
					.width = pin->mVideoFormat.cx,
					.height = pin->mVideoFormat.cy
				};
				pin->mFrameWriter->WriteTo(&buffer, pms);
			}
			auto t2 = std::chrono::high_resolution_clock::now();
			convLat = duration_cast<std::chrono::microseconds>(t2 - t1).count();
			hasFrame = true;
		}
	}
	if (hasFrame)
	{
		// in place byteswap so no need for frame conversion buffer
		if (pin->mVideoFormat.pixelFormat.format == pixel_format::AYUV)
		{
			auto t1 = std::chrono::high_resolution_clock::now();

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

			auto t2 = std::chrono::high_resolution_clock::now();
			auto execTime = duration_cast<std::chrono::microseconds>(t2 - t1).count();
			convLat += execTime;

			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Converted VUYA to AYUV in {:.3f} ms", mLogData.prefix,
			             execTime / 1000.0);
			#endif
		}

		pin->mPreviousFrameTime = pin->mCurrentFrameTime;
		pin->mCurrentFrameTime = frameTime;
		auto endTime = pin->mCurrentFrameTime - pin->mStreamStartTime;
		auto startTime = endTime - pin->mVideoFormat.frameInterval;
		auto missedFrame = frameTime - pin->mPreviousFrameTime >= pin->mVideoFormat.frameInterval * 2;

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

		REFERENCE_TIME now;
		pin->GetReferenceTime(&now);
		auto capLat = now - pin->mCaptureTime;

		pin->RecordLatency(convLat, capLat);
		pin->SnapTemperatureIfNecessary(endTime);

		#ifndef NO_QUILL
		if (pin->mFrameCounter == 1)
		{
			LOG_TRACE_L1(mLogData.logger,
			             "[{}] Captured video frame H|f_idx,cap_lat,conv_lat,ptime,ctime,a_int,delta,v_int,len,missed",
			             mLogData.prefix);
		}
		auto frameInterval = pin->mCurrentFrameTime - pin->mPreviousFrameTime;
		auto sz = pms->GetSize();

		if (pin->mVideoFormat.imageSize != sz)
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] Video format size mismatch (format: {} buffer: {})", mLogData.prefix,
			             pin->mVideoFormat.imageSize, sz);
		}
		LOG_TRACE_L1(mLogData.logger, "[{}] Captured video frame D|{},{:.3f},{:.3f},{},{},{},{},{},{},{}",
		             mLogData.prefix,
		             pin->mFrameCounter, static_cast<double>(capLat) / 1000.0, static_cast<double>(convLat) / 1000.0,
		             pin->mPreviousFrameTime, pin->mCurrentFrameTime, frameInterval,
		             frameInterval - pin->mVideoFormat.frameInterval, pin->mVideoFormat.frameInterval,
		             pin->mVideoFormat.imageSize, missedFrame);
		#endif
	}
	else
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] No frame loaded", mLogData.prefix, static_cast<int>(pin->mLastMwResult));
		#endif
	}
	return retVal;
}

//////////////////////////////////////////////////////////////////////////
// MagewellVideoCapturePin
//////////////////////////////////////////////////////////////////////////
MagewellVideoCapturePin::MagewellVideoCapturePin(HRESULT* phr, MagewellCaptureFilter* pParent, bool pPreview) :
	HdmiVideoCapturePin(
		phr,
		pParent,
		pPreview ? "VideoPreview" : "VideoCapture",
		pPreview ? L"Preview" : L"Capture",
		pPreview ? "VideoPreview" : "VideoCapture",
		VIDEO_FORMAT{},
		{
			{UYVY, {YV16, UYVY_YV16}},
			{YUY2, {YV16, YUY2_YV16}},
			{V210, {P210, V210_P210}},
			{BGR10, {RGB48, BGR10_BGR48}},
		}
	),
	mNotify(nullptr),
	mCaptureEvent(nullptr),
	mNotifyEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	mLastMwResult()
{
	auto hChannel = mFilter->GetChannelHandle();

	if (mFilter->GetDeviceType() != PRO)
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
						LOG_INFO(mLogData.logger, "[{}] USB frame intervals {}", mLogData.prefix, tmp);
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

	mCapturedFrame.data = new uint8_t[mVideoFormat.imageSize];
}

MagewellVideoCapturePin::~MagewellVideoCapturePin()
{
	CloseHandle(mNotifyEvent);
}

void MagewellVideoCapturePin::DoThreadDestroy()
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

void MagewellVideoCapturePin::LoadFormat(VIDEO_FORMAT* videoFormat, VIDEO_SIGNAL* videoSignal,
                                         const USB_CAPTURE_FORMATS* captureFormats)
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
	auto pixelFormats = deviceType == PRO
		                    ? proPixelFormats
		                    : deviceType == USB_PLUS
		                    ? usbPlusPixelFormats
		                    : usbProPixelFormats;
	videoFormat->bottomUpDib = deviceType == PRO;
	videoFormat->pixelFormat = pixelFormats[pfIdx][subsampling];
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

void MagewellVideoCapturePin::LogHdrMetaIfPresent(const VIDEO_FORMAT* newVideoFormat)
{
	#ifndef NO_QUILL
	auto hdrIf = mVideoSignal.hdrInfo;
	if (hdrIf.byEOTF || hdrIf.byMetadataDescriptorID
		|| hdrIf.display_primaries_lsb_x0 || hdrIf.display_primaries_lsb_x1 || hdrIf.display_primaries_lsb_x2
		|| hdrIf.display_primaries_msb_x0 || hdrIf.display_primaries_msb_x1 || hdrIf.display_primaries_msb_x2
		|| hdrIf.display_primaries_lsb_y0 || hdrIf.display_primaries_lsb_y1 || hdrIf.display_primaries_lsb_y2
		|| hdrIf.display_primaries_msb_y0 || hdrIf.display_primaries_msb_y1 || hdrIf.display_primaries_msb_y2
		|| hdrIf.white_point_msb_x || hdrIf.white_point_msb_y || hdrIf.white_point_lsb_x || hdrIf.white_point_lsb_y
		|| hdrIf.max_display_mastering_lsb_luminance || hdrIf.max_display_mastering_msb_luminance
		|| hdrIf.min_display_mastering_lsb_luminance || hdrIf.min_display_mastering_msb_luminance
		|| hdrIf.maximum_content_light_level_lsb || hdrIf.maximum_content_light_level_msb
		|| hdrIf.maximum_frame_average_light_level_lsb || hdrIf.maximum_frame_average_light_level_msb)
	{
		logHdrMeta(newVideoFormat->hdrMeta, mVideoFormat.hdrMeta, mLogData);
	}
	if (!newVideoFormat->hdrMeta.exists && mVideoFormat.hdrMeta.exists)
	{
		LOG_TRACE_L1(mLogData.logger, "[{}] HDR metadata has been removed", mLogData.prefix);
	}
	#endif
}

HRESULT MagewellVideoCapturePin::LoadSignal(HCHANNEL* pChannel)
{
	mLastMwResult = MWGetVideoSignalStatus(*pChannel, &mVideoSignal.signalStatus);
	auto retVal = S_OK;
	if (mLastMwResult != MW_SUCCEEDED)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] LoadSignal MWGetVideoSignalStatus failed", mLogData.prefix);
		#endif

		mVideoSignal.signalStatus.state = MWCAP_VIDEO_SIGNAL_NONE;

		retVal = S_FALSE;
	}
	mLastMwResult = MWGetInputSpecificStatus(*pChannel, &mVideoSignal.inputStatus);
	if (mLastMwResult != MW_SUCCEEDED)
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

void MagewellVideoCapturePin::OnFrameWriterStrategyUpdated()
{
	switch (mFrameWriterStrategy)
	{
	case STRAIGHT_THROUGH:
		mFrameWriter = std::make_unique<StraightThrough>(mLogData, mVideoFormat.cx, mVideoFormat.cy,
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
		HdmiVideoCapturePin::OnFrameWriterStrategyUpdated();
	}
	// has to happen after signalledFormat has been updated
	auto resetVideoCapture = mFilter->GetDeviceType() != PRO && !mFirst;
	if (resetVideoCapture && mVideoCapture)
		mVideoCapture.reset();

	if (mVideoFormat.imageSize > mVideoFormat.imageSize)
	{
		CAutoLock lck(&mCaptureCritSec);
		delete mCapturedFrame.data;
		mCapturedFrame.data = new uint8_t[mVideoFormat.imageSize];
	}

	if (resetVideoCapture)
	{
		mVideoCapture = std::make_unique<VideoCapture>(this, mFilter->GetChannelHandle());
	}
}

void MagewellVideoCapturePin::OnChangeMediaType()
{
	VideoCapturePin::OnChangeMediaType();

	mFilter->NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(mVideoFormat.cx, mVideoFormat.cy), 0);
}

void MagewellVideoCapturePin::CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam)
{
	MagewellVideoCapturePin* pin = static_cast<MagewellVideoCapturePin*>(pParam);
	pin->SnapCaptureTime();

	CAutoLock lck(&pin->mCaptureCritSec);
	memcpy(pin->mCapturedFrame.data, pbFrame, cbFrame);
	pin->mCapturedFrame.length = cbFrame;
	pin->mCapturedFrame.ts = u64TimeStamp;
	if (!SetEvent(pin->mNotifyEvent))
	{
		auto err = GetLastError();
		#ifndef NO_QUILL
		LOG_ERROR(pin->mLogData.logger, "[{}] Failed to notify on frame {:#08x}", pin->mLogData.prefix, err);
		#endif
	}
}

// loops til we have a frame to process, dealing with any mediatype changes as we go and then grabs a buffer once it's time to go
HRESULT MagewellVideoCapturePin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                                   REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hasFrame = false;
	auto retVal = S_FALSE;
	auto proDevice = mFilter->GetDeviceType() == PRO;
	auto hChannel = mFilter->GetChannelHandle();

	while (!hasFrame)
	{
		if (CheckStreamState(nullptr) == STREAM_DISCARDING)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Stream is discarding", mLogData.prefix);
			#endif

			break;
		}
		if (mStreamStartTime < 0)
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

		VIDEO_FORMAT newVideoFormat;
		LoadFormat(&newVideoFormat, &mVideoSignal, &mUsbCaptureFormats);

		hr = OnVideoSignal(newVideoFormat);

		if (hr != S_RECONNECTION_UNNECESSARY || (hadSignal && !mHasSignal))
		{
			mFilter->OnVideoSignalLoaded(&mVideoSignal);
		}

		if (FAILED(hr))
		{
			BACKOFF;
			continue;
		}

		// grab next frame 
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
				mLastMwResult = MWGetNotifyStatus(hChannel, mNotify, &mStatusBits);
				if (mLastMwResult != MW_SUCCEEDED)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] MWGetNotifyStatus failed {}", mLogData.prefix,
					             static_cast<int>(mLastMwResult));
					#endif

					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Video signal change, retry after backoff", mLogData.prefix);
					#endif

					BACKOFF;
					continue;
				}
				if (mStatusBits & MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Video input source change, retry after backoff",
					             mLogData.prefix);
					#endif

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
				// new frame is the only type of notification
				hasFrame = true;
			}

			if (hasFrame)
			{
				retVal = VideoCapturePin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
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
				SHORT_BACKOFF;
		}
		else
		{
			if (!mHasSignal && dwRet == STATUS_TIMEOUT)
			{
				SnapCaptureTime();

				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] Timeout and no signal, get delivery buffer for no signal image",
				             mLogData.prefix);
				#endif
				retVal = VideoCapturePin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
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
				            static_cast<unsigned long>(dwRet));
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

HRESULT MagewellVideoCapturePin::FillBuffer(IMediaSample* pms)
{
	VideoFrameGrabber vfg(this, mFilter->GetChannelHandle(), mFilter->GetDeviceType(), pms);
	auto retVal = vfg.grab();
	if (S_FALSE == HandleStreamStateChange(pms))
	{
		retVal = S_FALSE;
	}
	return retVal;
}


HRESULT MagewellVideoCapturePin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] MagewellVideoCapturePin::OnThreadCreate", mLogData.prefix);
	#endif

	UpdateDisplayStatus();

	auto hChannel = mFilter->GetChannelHandle();
	LoadSignal(&hChannel);

	mFilter->OnVideoSignalLoaded(&mVideoSignal);

	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == PRO)
	{
		// start capture
		mCaptureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		mLastMwResult = MWStartVideoCapture(hChannel, mCaptureEvent);
		#ifndef NO_QUILL
		if (mLastMwResult != MW_SUCCEEDED)
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

void MagewellVideoCapturePin::StopCapture()
{
	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == PRO)
	{
		MWStopVideoCapture(mFilter->GetChannelHandle());
	}
	else if (mVideoCapture)
	{
		mVideoCapture.reset();
	}
}

//////////////////////////////////////////////////////////////////////////
//  MagewellAudioCapturePin::AudioCapture
//////////////////////////////////////////////////////////////////////////
MagewellAudioCapturePin::AudioCapture::AudioCapture(MagewellAudioCapturePin* pin, HCHANNEL hChannel) :
	pin(pin),
	mLogData(pin->mLogData)
{
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

MagewellAudioCapturePin::AudioCapture::~AudioCapture()
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
// MagewellAudioCapturePin
//////////////////////////////////////////////////////////////////////////
MagewellAudioCapturePin::MagewellAudioCapturePin(HRESULT* phr, MagewellCaptureFilter* pParent, bool pPreview) :
	HdmiAudioCapturePin(
		phr,
		pParent,
		pPreview ? "AudioPreview" : "AudioCapture",
		pPreview ? L"AudioPreview" : L"AudioCapture",
		pPreview ? "AudioPreview" : "AudioCapture"
	),
	mNotify(nullptr),
	mCaptureEvent(nullptr),
	mNotifyEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	mLastMwResult(),
	mDataBurstBuffer(bitstreamBufferSize)
// initialise to a reasonable default size that is not wastefully large but also is unlikely to need to be expanded very often
{
	mCapturedFrame.data = new uint8_t[maxFrameLengthInBytes];
	mCapturedFrame.length = maxFrameLengthInBytes;

	mDataBurstBuffer.assign(bitstreamBufferSize, 0);
	DWORD dwInputCount = 0;
	auto hChannel = pParent->GetChannelHandle();
	mLastMwResult = MWGetAudioInputSourceArray(hChannel, nullptr, &dwInputCount);
	if (mLastMwResult != MW_SUCCEEDED)
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

MagewellAudioCapturePin::~MagewellAudioCapturePin()
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

void MagewellAudioCapturePin::DoThreadDestroy()
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

void MagewellAudioCapturePin::LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal) const
{
	auto audioIn = *audioSignal;
	auto currentChannelAlloc = audioFormat->channelAllocation;
	auto currentChannelMask = audioFormat->channelValidityMask;
	if (mFilter->GetDeviceType() != PRO)
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

HRESULT MagewellAudioCapturePin::LoadSignal(HCHANNEL* hChannel)
{
	mLastMwResult = MWGetAudioSignalStatus(*hChannel, &mAudioSignal.signalStatus);
	if (MW_SUCCEEDED != mLastMwResult)
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] LoadSignal MWGetAudioSignalStatus", mLogData.prefix);
		#endif
		return S_FALSE;
	}

	MWCAP_INPUT_SPECIFIC_STATUS status;
	mLastMwResult = MWGetInputSpecificStatus(*hChannel, &status);
	if (mLastMwResult == MW_SUCCEEDED)
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
		else if (MW_SUCCEEDED != MWGetHDMIInfoFrameValidFlag(*hChannel, &tPdwValidFlag))
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Unable to detect HDMI InfoFrame", mLogData.prefix);
			#endif
		}
		if (tPdwValidFlag & MWCAP_HDMI_INFOFRAME_MASK_AUDIO)
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

void MagewellAudioCapturePin::CaptureFrame(const BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam)
{
	MagewellAudioCapturePin* pin = static_cast<MagewellAudioCapturePin*>(pParam);
	CAutoLock lck(&pin->mCaptureCritSec);
	memcpy(pin->mCapturedFrame.data, pbFrame, cbFrame);
	pin->mCapturedFrame.length = cbFrame;
	pin->mCapturedFrame.ts = u64TimeStamp;
	if (!SetEvent(pin->mNotifyEvent))
	{
		auto err = GetLastError();
		#ifndef NO_QUILL
		LOG_ERROR(pin->mLogData.logger, "[{}] Failed to notify on frame {:#08x}", pin->mLogData.prefix, err);
		#endif
	}
}

HRESULT MagewellAudioCapturePin::FillBuffer(IMediaSample* pms)
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
	}
	else
	{
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

	auto endTime = mCurrentFrameTime - mStreamStartTime;
	auto startTime = endTime - static_cast<long>(mAudioFormat.sampleInterval * MWCAP_AUDIO_SAMPLES_PER_FRAME);

	REFERENCE_TIME now;
	mFilter->GetReferenceTime(&now);
	auto capLat = now - mCurrentFrameTime;
	RecordLatency(capLat);

	#ifndef NO_QUILL
	if (mFrameCounter == 1)
	{
		LOG_TRACE_L1(mLogData.logger, "[{}] Captured audio frame H|codec,since,f_idx,lat,ptime,ctime,delta,len,count",
		             mLogData.prefix);
	}
	LOG_TRACE_L1(mLogData.logger, "[{}] Captured audio frame D|{},{},{},{},{},{},{},{},{}",
	             mLogData.prefix, codecNames[mAudioFormat.codec], mSinceCodecChange,
	             mFrameCounter, capLat, mPreviousFrameTime, mCurrentFrameTime,
	             mCurrentFrameTime - mPreviousFrameTime, bytesCaptured, samplesCaptured);
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

HRESULT MagewellAudioCapturePin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] MagewellAudioCapturePin::OnThreadCreate", mLogData.prefix);
	#endif

	memset(mCompressedBuffer, 0, sizeof(mCompressedBuffer));

	auto hChannel = mFilter->GetChannelHandle();
	LoadSignal(&hChannel);
	mFilter->OnAudioSignalLoaded(&mAudioSignal);

	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == PRO)
	{
		// start capture
		mLastMwResult = MWStartAudioCapture(hChannel);
		if (mLastMwResult != MW_SUCCEEDED)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] MagewellAudioCapturePin::OnThreadCreate Unable to MWStartAudioCapture",
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
			LOG_ERROR(mLogData.logger, "[{}] MagewellAudioCapturePin::OnThreadCreate Unable to MWRegistryNotify",
			          mLogData.prefix);
			#endif
		}
	}
	else
	{
		mAudioCapture = std::make_unique<AudioCapture>(this, mFilter->GetChannelHandle());
	}
	return NOERROR;
}

HRESULT MagewellAudioCapturePin::DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat)
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
		if (mFilter->GetDeviceType() != PRO)
		{
			mAudioCapture = std::make_unique<AudioCapture>(this, mFilter->GetChannelHandle());
		}
	}
	return retVal;
}

void MagewellAudioCapturePin::StopCapture()
{
	auto deviceType = mFilter->GetDeviceType();
	if (deviceType == PRO)
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

bool MagewellAudioCapturePin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
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

// loops til we have a frame to process, dealing with any mediatype changes as we go and then grabs a buffer once it's time to go
HRESULT MagewellAudioCapturePin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
                                                   REFERENCE_TIME* pEndTime, DWORD dwFlags)
{
	auto hChannel = mFilter->GetChannelHandle();
	auto proDevice = mFilter->GetDeviceType() == PRO;
	auto hasFrame = false;
	auto retVal = S_FALSE;
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

		if (mStreamStartTime < 0)
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
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Unable to load signal, retry after backoff", mLogData.prefix);
			#endif

			if (mSinceCodecChange > 0)
				mFilter->OnAudioSignalLoaded(&mAudioSignal);

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

		AUDIO_FORMAT newAudioFormat(mAudioFormat);
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
				mLastMwResult = MWGetNotifyStatus(hChannel, mNotify, &mStatusBits);
				if (mStatusBits & MWCAP_NOTIFY_AUDIO_SIGNAL_CHANGE)
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Audio signal change, retry after backoff", mLogData.prefix);
					#endif

					if (mSinceCodecChange > 0)
						mFilter->OnAudioSignalLoaded(&mAudioSignal);

					mSinceLast = 0;
					mSinceCodecChange = 0;

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
					BACKOFF;
					continue;
				}

				if (mStatusBits & MWCAP_NOTIFY_AUDIO_FRAME_BUFFERED)
				{
					mLastMwResult = MWCaptureAudioFrame(hChannel, &mAudioSignal.frameInfo);
					if (MW_SUCCEEDED == mLastMwResult)
					{
						frameCopied = true;
						mPreviousFrameTime = mCurrentFrameTime;
						GetReferenceTime(&mCurrentFrameTime);

						#ifndef NO_QUILL
						LOG_TRACE_L3(mLogData.logger, "[{}] Audio frame buffered and captured at {}", mLogData.prefix,
						             mCurrentFrameTime);
						#endif

						memcpy(mFrameBuffer, mAudioSignal.frameInfo.adwSamples, maxFrameLengthInBytes);
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
							            static_cast<int>(mLastMwResult), mDataBurstRead);
							#endif
						}
						else
						{
							#ifndef NO_QUILL
							LOG_WARNING(mLogData.logger, "[{}] Audio frame buffered but capture failed ({}), retrying",
							            mLogData.prefix, static_cast<int>(mLastMwResult));
							#endif
						}
						continue;
					}
				}
			}
			else
			{
				CAutoLock lck(&mCaptureCritSec);

				frameCopied = true;
				mPreviousFrameTime = mCurrentFrameTime;
				GetReferenceTime(&mCurrentFrameTime);

				#ifndef NO_QUILL
				LOG_TRACE_L3(mLogData.logger, "[{}] Audio frame buffered and captured at {}", mLogData.prefix,
				             mCurrentFrameTime);
				#endif

				memcpy(mFrameBuffer, mCapturedFrame.data, mCapturedFrame.length);
			}
		}

		if (frameCopied)
		{
			mFrameCounter++;
			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Reading frame {}", mLogData.prefix, mFrameCounter);
			#endif

			#ifdef RECORD_RAW
			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] raw,{},{}", mLogData.prefix, mFrameCounter, maxFrameLengthInBytes);
			#endif
			fwrite(mFrameBuffer, maxFrameLengthInBytes, 1, mRawFile);
			#endif

			Codec* detectedCodec = &newAudioFormat.codec;
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
				retVal = AudioCapturePin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
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
			SHORT_BACKOFF;
	}
	return retVal;
}

// copies the inbound byte stream into a format that can be probed
void MagewellAudioCapturePin::CopyToBitstreamBuffer(BYTE* buf)
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
HRESULT MagewellAudioCapturePin::ParseBitstreamBuffer(uint16_t bufSize, enum Codec** codec)
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
HRESULT MagewellAudioCapturePin::GetCodecFromIEC61937Preamble(const IEC61937DataType dataType, uint16_t* burstSize,
                                                              Codec* codec)
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
