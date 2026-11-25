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

#include "mw_capture_filter.h"
#include "mw_video_capture_pin.h"
#include "mw_audio_capture_pin.h"
#include "web_json.h"

#define REG_KEY_BASE L"MWCapture"
#define DEFAULT_METRICS_PORT 9464
// the installer's firewall rule opens this port
#define DEFAULT_WEB_PORT 9474

#if CAPTURE_NAME_SUFFIX == 1
#define LOG_PREFIX_NAME "MagewellTrace"
#define WLOG_PREFIX_NAME L"MagewellTrace"
#elif CAPTURE_NAME_SUFFIX == 2
#define LOG_PREFIX_NAME "MagewellWarn"
#define WLOG_PREFIX_NAME L"MagewellWarn"
#elif defined(_DEBUG)
#define LOG_PREFIX_NAME "MagewellDebug"
#define WLOG_PREFIX_NAME L"MagewellDebug"
#else
#define LOG_PREFIX_NAME "Magewell"
#define WLOG_PREFIX_NAME L"Magewell"
#endif


CUnknown* magewell_capture_filter::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{
	auto pNewObject = new magewell_capture_filter(punk, phr);

	if (pNewObject == nullptr)
	{
		if (phr)
			*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

magewell_capture_filter::magewell_capture_filter(LPUNKNOWN punk, HRESULT* phr) :
	hdmi_capture_filter(WLOG_PREFIX_NAME, punk, phr, CLSID_MWCAPTURE_FILTER, LOG_PREFIX_NAME, REG_KEY_BASE,
	                    DEFAULT_METRICS_PORT, DEFAULT_WEB_PORT)
{
	// Initialise the device and validate that it presents some form of data
	mInited = MWCaptureInitInstance();
	#ifndef NO_LOGGING
	if (!mInited)
	{
		LOG_ERROR(mLogData.logger, "[{}] Unable to init", mLogData.prefix);
	}
	#endif
	CAutoLock lck(&m_cStateLock);
	device_info* diToUse(nullptr);
	int channelCount = MWGetChannelCount();
	// TODO read HKEY_LOCAL_MACHINE L"Software\\mwcapture\\devicepath"
	for (int i = 0; i < channelCount; i++)
	{
		device_info di{};
		MWCAP_CHANNEL_INFO mci;
		MWGetChannelInfoByIndex(i, &mci);
		if (mci.wFamilyID == MW_FAMILY_ID_PRO_CAPTURE)
		{
			di.deviceType = MW_PRO;
			di.serialNo = std::string{ mci.szBoardSerialNo };
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
				di.deviceType = MW_USB_PLUS;
			}
			else if (0 == strcmp(mci.szProductName, "USB Capture HDMI 4K Pro"))
			{
				di.deviceType = MW_USB_PRO;
			}
			else
			{
				#ifndef NO_LOGGING
				LOG_WARNING(mLogData.logger, "[{}] Ignoring unknown USB device type {} with serial no {}",
					mLogData.prefix, mci.szProductName, di.serialNo);
				#endif
				continue;
			}
			di.serialNo = std::string{ mci.szBoardSerialNo };
			// TODO use MWCAP_DEVICE_NAME_MODE and MWUSBGetDeviceNameMode mode?
			MWCAP_USB_CAPTURE_INFO info{};
			const auto infoResult = MWGetFamilyInfoByIndex(i, &info, sizeof(MWCAP_USB_CAPTURE_INFO));
			di.linkSpeed = info.byUSBSpeed;
			#ifndef NO_LOGGING
			if (infoResult == MW_SUCCEEDED)
			{
				LOG_INFO(mLogData.logger,
					"[{}] {} device {} USB link: {} (raw: {}), memory {} / {} bytes free", mLogData.prefix,
					mci.szProductName, di.serialNo, usbSpeedName(info.byUSBSpeed),
					static_cast<int>(info.byUSBSpeed), info.cbFreeMemorySize, info.cbTotalMemorySize);
			}
			else
			{
				LOG_WARNING(mLogData.logger, "[{}] Unable to read USB link details for {} device {} ({})",
					mLogData.prefix, mci.szProductName, di.serialNo, static_cast<int>(infoResult));
			}
			#endif
		}

		MWGetDevicePath(i, di.devicePath);
		di.hChannel = MWOpenChannelByPath(di.devicePath);
		if (di.hChannel == nullptr)
		{
			#ifndef NO_LOGGING
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
			#ifndef NO_LOGGING
			LOG_WARNING(mLogData.logger, "[{}] Unable to detect video inputs on {} device {} at path {}, ignoring",
				mLogData.prefix,
				devicetype_to_name(di.deviceType), di.serialNo, std::wstring{ di.devicePath });
			#endif
			continue;
		}
		DWORD videoInputTypes[16] = { 0 };
		if (MW_SUCCEEDED != MWGetVideoInputSourceArray(di.hChannel, videoInputTypes, &videoInputTypeCount))
		{
			MWCloseChannel(di.hChannel);
			#ifndef NO_LOGGING
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
				#ifndef NO_LOGGING
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
			#ifndef NO_LOGGING
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
			#ifndef NO_LOGGING
			LOG_INFO(mLogData.logger, "[{}] Filter will use {} device {} at path {}", mLogData.prefix,
				devicetype_to_name(di.deviceType),
				di.serialNo, std::wstring{ di.devicePath });
			if (di.deviceType != MW_PRO)
			{
				// the speed cap set in the USB Capture Utility, the card will not use a faster link than this
				uint8_t currentMax{0};
				uint8_t speeds[8]{};
				int speedCount{0};
				const auto menuResult = MWUSBGetMaxSpeedMenu(di.hChannel, &currentMax, speeds, &speedCount);
				if (menuResult == MW_SUCCEEDED)
				{
					std::string allowed;
					for (int s = 0; s < std::min(speedCount, 8); ++s)
					{
						if (s != 0) allowed += ", ";
						allowed += std::format("{} (raw: {})", usbSpeedName(speeds[s]), static_cast<int>(speeds[s]));
					}
					LOG_INFO(mLogData.logger, "[{}] USB max speed setting: {} (raw: {}), available [{}]",
						mLogData.prefix, usbSpeedName(currentMax), static_cast<int>(currentMax), allowed);
				}
				else
				{
					LOG_WARNING(mLogData.logger, "[{}] Unable to read the USB max speed setting ({})",
						mLogData.prefix, static_cast<int>(menuResult));
				}
			}
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
			#ifndef NO_LOGGING
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
		#ifndef NO_LOGGING
		LOG_ERROR(mLogData.logger, "No valid channels found");
		#endif

		// TODO throw
	}
	else
	{
		magewell_capture_filter::OnDeviceUpdated();
	}

	mClock = new MWReferenceClock(phr, mDeviceInfo.hChannel, mDeviceInfo.deviceType == MW_PRO);
	mClock->AddRef();

	auto vp = new magewell_video_capture_pin(phr, this, false);
	vp->UpdateFrameWriterStrategy();
	vp = new magewell_video_capture_pin(phr, this, true);
	vp->UpdateFrameWriterStrategy();

	if (mAudioCaptureEnabled)
	{
		new magewell_audio_capture_pin(phr, this, false);
		new magewell_audio_capture_pin(phr, this, true);
	}

	StartWebApi();
}

std::string magewell_capture_filter::GetWebId() const
{
	auto serial = web_json::slug(mDeviceInfo.serialNo);
	std::erase(serial, '_');
	return serial.empty() ? "magewell" : "magewell-" + serial;
}

std::string magewell_capture_filter::GetWebName() const
{
	switch (mDeviceInfo.deviceType)
	{
	case MW_PRO: return "Magewell Pro Capture";
	case MW_USB_PRO: return "Magewell USB Capture 4K Pro";
	case MW_USB_PLUS: return "Magewell USB Capture 4K+";
	default: return "Magewell";
	}
}

magewell_capture_filter::~magewell_capture_filter()
{
	#ifndef NO_LOGGING
	// a host may kill the thread if teardown is slow (JRiver allows 2s), so time each step
	const auto t0 = std::chrono::steady_clock::now();
	auto elapsed = [t0] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
	LOG_INFO(mLogData.logger, "[{}] Tearing down filter", mLogData.prefix);
	#endif
	StopWebApi();
	// the pins (pinned buffers) and the clock (device time) use the channel so must go before it is closed
	ReleasePinsAndClock();
	if (mDeviceInfo.hChannel)
	{
		MWCloseChannel(mDeviceInfo.hChannel);
		mDeviceInfo.hChannel = nullptr;
	}
	#ifndef NO_LOGGING
	LOG_INFO(mLogData.logger, "[{}] Channel closed at {:.1f}ms", mLogData.prefix, elapsed());
	#endif
	if (mInited)
	{
		MWCaptureExitInstance();
	}
	#ifndef NO_LOGGING
	LOG_INFO(mLogData.logger, "[{}] SDK released at {:.1f}ms", mLogData.prefix, elapsed());
	#endif
}

void magewell_capture_filter::SnapHardwareDetails()
{
	if (mDeviceInfo.deviceType == MW_PRO)
	{
		uint32_t temp;
		MWGetTemperature(mDeviceInfo.hChannel, &temp);
		mDeviceInfo.temperature = temp;
	}
	else if (mDeviceInfo.deviceType == MW_USB_PLUS)
	{
		int16_t temp;
		MWUSBGetCoreTemperature(mDeviceInfo.hChannel, &temp);
		mDeviceInfo.temperature = temp;
	}
	else if (mDeviceInfo.deviceType == MW_USB_PRO)
	{
		int16_t val;
		MWUSBGetCoreTemperature(mDeviceInfo.hChannel, &val);
		mDeviceInfo.temperature = val;
		MWUSBGetFanSpeed(mDeviceInfo.hChannel, &val);
		mDeviceInfo.fanSpeed = val;
	}
}

void magewell_capture_filter::OnVideoSignalLoaded(video_signal* vs)
{
	mVideoInputStatus.inX = vs->signalStatus.cx;
	mVideoInputStatus.inY = vs->signalStatus.cy;
	mVideoInputStatus.inAspectX = vs->signalStatus.nAspectX;
	mVideoInputStatus.inAspectY = vs->signalStatus.nAspectY;
	mVideoInputStatus.inFps = vs->signalStatus.dwFrameDuration > 0
		? static_cast<double>(dshowTicksPerSecond) / vs->signalStatus.dwFrameDuration
		: 0.0;
	mVideoInputStatus.inFrameDuration = vs->signalStatus.dwFrameDuration;
	mVideoInputStatus.inTotalX = vs->signalStatus.cxTotal;
	mVideoInputStatus.inTotalY = vs->signalStatus.cyTotal;
	mVideoInputStatus.inPixelClockMhz = vs->signalStatus.cxTotal * static_cast<double>(vs->signalStatus.cyTotal)
		* mVideoInputStatus.inFps / 1000000.0;

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

	PublishVideoSignalMetrics();

	std::lock_guard infoCallbackLock{mInfoCallbackLock};
	for (auto* cb : InfoCallbacks())
	{
		cb->Reload(&mVideoInputStatus);
	}
}

void magewell_capture_filter::OnAudioSignalLoaded(audio_signal* as)
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

	PublishAudioSignalMetrics();

	std::lock_guard infoCallbackLock{mInfoCallbackLock};
	for (auto* cb : InfoCallbacks())
	{
		cb->Reload(&mAudioInputStatus);
	}
}

void magewell_capture_filter::OnDeviceUpdated()
{
	auto oldDesc = mDeviceStatus.deviceDesc;

	mDeviceStatus.deviceDesc = devicetype_to_name(mDeviceInfo.deviceType);
	mDeviceStatus.deviceDesc += " [";
	mDeviceStatus.deviceDesc += mDeviceInfo.serialNo;
	mDeviceStatus.deviceDesc += "]";
	mDeviceStatus.temperature = mDeviceInfo.temperature / 10.0;
	mDeviceStatus.linkSpeed = mDeviceInfo.linkSpeed;
	mDeviceStatus.fanSpeed = mDeviceInfo.fanSpeed;
	if (mDeviceInfo.deviceType == MW_PRO)
	{
		mDeviceStatus.linkWidth = mDeviceInfo.linkWidth;
		mDeviceStatus.maxReadRequestSize = mDeviceInfo.maxReadRequestSize;
		mDeviceStatus.maxPayloadSize = mDeviceInfo.maxPayloadSize;
	}
	else
	{
		mDeviceStatus.protocol = USB;
	}
	mDeviceStatus.bandwidth = GetCaptureBandwidth();

	#ifndef NO_LOGGING
	if (oldDesc != mDeviceStatus.deviceDesc)
	{
		LOG_INFO(mLogData.logger, "[{}] Recorded device description: {}", mLogData.prefix, mDeviceStatus.deviceDesc);
	}
	#endif

	std::lock_guard infoCallbackLock{mInfoCallbackLock};
	for (auto* cb : InfoCallbacks())
	{
		cb->Reload(&mDeviceStatus);
	}
}

HCHANNEL magewell_capture_filter::GetChannelHandle() const
{
	return mDeviceInfo.hChannel;
}

device_type magewell_capture_filter::GetDeviceType() const
{
	return mDeviceInfo.deviceType;
}

double magewell_capture_filter::GetCaptureBandwidth() const
{
	return mDeviceInfo.deviceType == MW_PRO
		       ? pcieCaptureBandwidth(mDeviceInfo.linkSpeed, mDeviceInfo.linkWidth)
		       : usbCaptureBandwidth(mDeviceInfo.linkSpeed);
}
