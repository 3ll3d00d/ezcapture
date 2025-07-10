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
#include "bm_capture_filter.h"
#include "bm_audio_capture_pin.h"
#include "bm_video_capture_pin.h"

#define REG_KEY_BASE L"BMCapture"

#if CAPTURE_NAME_SUFFIX == 1
#define LOG_PREFIX_NAME "BlackmagicTrace"
#define WLOG_PREFIX_NAME L"BlackmagicTrace"
#elif CAPTURE_NAME_SUFFIX == 2
#define LOG_PREFIX_NAME "BlackmagicWarn"
#define WLOG_PREFIX_NAME L"BlackmagicWarn"
#else
#define LOG_PREFIX_NAME "Blackmagic"
#define WLOG_PREFIX_NAME L"Blackmagic"
#endif

constexpr auto blockFilterRegKey = L"BlockFilterOnRR";

constexpr uint16_t minDisplayWidth = 4096;
using mics = std::chrono::microseconds;
constexpr BMDTimeScale referenceTimescale = mics(std::chrono::seconds(1)).count();

CUnknown* blackmagic_capture_filter::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{
	auto pNewObject = new blackmagic_capture_filter(punk, phr);

	if (pNewObject == nullptr)
	{
		if (phr)
			*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

void blackmagic_capture_filter::LoadFormat(video_format* videoFormat, const video_signal* videoSignal)
{
	videoFormat->cx = videoSignal->cx;
	videoFormat->cy = videoSignal->cy;
	videoFormat->fps = static_cast<double>(videoSignal->frameDurationScale) / static_cast<double>(videoSignal->
		frameDuration);
	videoFormat->frameInterval = dshowTicksPerSecond / videoFormat->fps;
	videoFormat->saturation = SATURATION_FULL;
	switch (videoSignal->pixelFormat)
	{
	case bmdFormat8BitYUV:
		videoFormat->pixelFormat = YUV2;
		break;
	case bmdFormat10BitYUV:
		videoFormat->pixelFormat = V210;
		break;
	case bmdFormat10BitYUVA:
		// unusual format, Ultrastudio 4k mini only
		videoFormat->pixelFormat = AY10;
		break;
	case bmdFormat8BitARGB:
		videoFormat->pixelFormat = RGBA; // seems dubious but appears to work in practice
		break;
	case bmdFormat8BitBGRA:
		videoFormat->pixelFormat = RGBA; // seems dubious but appears to work in practice
		break;
	case bmdFormat10BitRGB:
		videoFormat->pixelFormat = R210;
		break;
	case bmdFormat12BitRGB:
		videoFormat->pixelFormat = R12B;
		break;
	case bmdFormat12BitRGBLE:
		videoFormat->pixelFormat = R12L;
		break;
	case bmdFormat10BitRGBXLE:
		videoFormat->pixelFormat = R10L;
		break;
	case bmdFormat10BitRGBX:
		videoFormat->pixelFormat = R10B;
		break;
	case bmdFormatUnspecified:
	case bmdFormatH265:
	case bmdFormatDNxHR:
		// unsupported
		break;
	}
	videoFormat->quantisation = videoFormat->pixelFormat.rgb ? QUANTISATION_FULL : QUANTISATION_UNKNOWN;
	videoFormat->CalculateDimensions();
}

blackmagic_capture_filter::blackmagic_capture_filter(LPUNKNOWN punk, HRESULT* phr) :
	hdmi_capture_filter(WLOG_PREFIX_NAME, punk, phr, CLSID_BMCAPTURE_FILTER, LOG_PREFIX_NAME, REG_KEY_BASE),
	mVideoFrameEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	mAudioFrameEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr))
{
	// load the API
	IDeckLinkIterator* deckLinkIterator = nullptr;
	HRESULT result = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator,
	                                  reinterpret_cast<void**>(&deckLinkIterator));

	// Initialise the device and validate that it presents some form of data
	#ifndef NO_QUILL
	if (result != S_OK)
	{
		LOG_ERROR(mLogData.logger, "Unable to get DecklinkIterator");
	}
	#endif

	IDeckLinkAPIInformation* deckLinkApiInfo = nullptr;
	result = deckLinkIterator->QueryInterface(IID_IDeckLinkAPIInformation, reinterpret_cast<void**>(&deckLinkApiInfo));
	if (result == S_OK)
	{
		int64_t deckLinkVersion;
		deckLinkApiInfo->GetInt(BMDDeckLinkAPIVersion, &deckLinkVersion);
		mDeviceInfo.apiVersion[0] = (deckLinkVersion & 0xFF000000) >> 24;
		mDeviceInfo.apiVersion[1] = (deckLinkVersion & 0x00FF0000) >> 16;
		mDeviceInfo.apiVersion[2] = (deckLinkVersion & 0x0000FF00) >> 8;
		deckLinkApiInfo->Release();
	}

	auto idx = 0;
	IDeckLink* deckLink = nullptr;

	while (deckLinkIterator->Next(&deckLink) == S_OK)
	{
		BSTR deckLinkName;
		std::string deviceName;
		IDeckLinkProfileAttributes* deckLinkAttributes = nullptr;

		result = deckLink->GetDisplayName(&deckLinkName);
		if (result == S_OK)
		{
			deviceName = BSTRToStdString(deckLinkName);
			DeleteString(deckLinkName);
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to get device name for device at index {}", mLogData.prefix, idx);
			#endif

			deckLink->Release();
			continue;
		}

		result = deckLink->QueryInterface(IID_IDeckLinkProfileAttributes,
		                                  reinterpret_cast<void**>(&deckLinkAttributes));
		if (result != S_OK)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Ignoring device {} {}, unable to query for profile attributes",
			          mLogData.prefix, idx, deviceName);
			#endif

			deckLink->Release();
			continue;
		}
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Found device at index {} : {}", mLogData.prefix, idx, deviceName);
		#endif

		int64_t duplexMode;
		if (deckLinkAttributes->GetInt(BMDDeckLinkDuplex, &duplexMode) == S_OK && duplexMode == bmdDuplexInactive)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Ignoring device {} {}, no active connectors for current profile", idx,
			          mLogData.prefix, deviceName);
			#endif

			deckLink->Release();
			continue;
		}

		int64_t videoIOSupport;
		result = deckLinkAttributes->GetInt(BMDDeckLinkVideoIOSupport, &videoIOSupport);
		if (result != S_OK)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger,
			          "[{}] Ignoring device {} {}, could not get BMDDeckLinkVideoIOSupport attribute ({:#08x})",
			          mLogData.prefix, idx, deviceName, static_cast<unsigned long>(result));
			#endif

			deckLink->Release();
			continue;
		}

		if (videoIOSupport & bmdDeviceSupportsCapture)
		{
			int64_t audioChannelCount;
			BOOL inputFormatDetection{false};
			BOOL hdrMetadata{false};
			BOOL colourspaceMetadata{false};
			BOOL dynamicRangeMetadata{false};

			result = deckLinkAttributes->GetInt(BMDDeckLinkMaximumHDMIAudioChannels, &audioChannelCount);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Device {} {} does not support audio capture",
				            mLogData.prefix, idx, deviceName);
				#endif

				audioChannelCount = 0;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &inputFormatDetection);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Ignoring device {} {} does not support input format detection",
				            mLogData.prefix, idx, deviceName);
				#endif

				inputFormatDetection = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsHDRMetadata, &hdrMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Device {} {} does not support HDR metadata",
				            mLogData.prefix, idx, deviceName);
				#endif

				hdrMetadata = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsColorspaceMetadata, &colourspaceMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Device {} {} does not support colourspace metadata",
				            mLogData.prefix, idx, deviceName);
				#endif

				colourspaceMetadata = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportedDynamicRange, &dynamicRangeMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Device {} {} does not support dynamic range metadata",
				            mLogData.prefix, idx, deviceName);
				#endif

				dynamicRangeMetadata = false;
			}

			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Device capabilities - {}", mLogData.prefix, deviceName);
			LOG_INFO(mLogData.logger, "[{}] Input Format Detection? {}", mLogData.prefix, inputFormatDetection);
			LOG_INFO(mLogData.logger, "[{}] Can Detect HDR Metadata? {}", mLogData.prefix, hdrMetadata);
			LOG_INFO(mLogData.logger, "[{}] Can Detect Colour space Metadata? {}", mLogData.prefix,
			         colourspaceMetadata);
			LOG_INFO(mLogData.logger, "[{}] Can Detect Dynamic Range Metadata? {}", mLogData.prefix,
			         dynamicRangeMetadata);
			LOG_INFO(mLogData.logger, "[{}] Audio Channels? {}", mLogData.prefix, audioChannelCount);
			#endif

			if (!inputFormatDetection)
			{
				deckLink->Release();
				continue;
			}

			if (mDeckLink)
			{
				#ifndef NO_QUILL
				LOG_INFO(mLogData.logger, "[{}] Ignoring detected device {}", mLogData.prefix, deviceName);
				#endif

				deckLink->Release();
				continue;
			}

			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Selected device {}", mLogData.prefix, deviceName);
			#endif

			mDeckLink = deckLink;
			mDeckLinkInput = deckLink;
			mDeckLinkNotification = deckLink;
			mDeckLinkStatus = deckLink;
			mDeckLinkHDMIInputEDID = deckLink;

			result = mDeckLinkInput->SetCallback(this);
			if (S_OK != result)
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to SetCallback [{:#08x}], ignoring device", mLogData.prefix,
				          static_cast<unsigned long>(result));
				#endif

				mDeckLinkNotification = nullptr;
				mDeckLinkStatus = nullptr;
				mDeckLinkHDMIInputEDID = nullptr;
				mDeckLinkInput = nullptr;
				mDeckLink.Release();
				mDeckLink = nullptr;

				continue;
			}

			CComPtr<IDeckLinkDisplayModeIterator> displayModeIterator;

			result = mDeckLinkInput->GetDisplayModeIterator(&displayModeIterator);
			if (S_OK != result)
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to GetDisplayModeIterator [{:#08x}], ignoring device",
				          mLogData.prefix, static_cast<unsigned long>(result));
				#endif

				mDeckLinkNotification = nullptr;
				mDeckLinkStatus = nullptr;
				mDeckLinkHDMIInputEDID = nullptr;
				mDeckLinkInput = nullptr;
				mDeckLink.Release();
				mDeckLink = nullptr;

				continue;
			}

			long width = 0;
			double fps = 0.0;
			while (true)
			{
				IDeckLinkDisplayMode* displayMode;
				if (S_OK == displayModeIterator->Next(&displayMode))
				{
					BMDTimeScale ts;
					BMDTimeValue tv;
					displayMode->GetFrameRate(&tv, &ts);
					auto dmFps = static_cast<double>(ts) / static_cast<double>(tv);
					auto dmWidth = displayMode->GetWidth();
					auto dm = displayMode->GetDisplayMode();
					if (dmWidth > width || std::isgreater(dmFps, fps))
					{
						if (width >= minDisplayWidth)
						{
							#ifndef NO_QUILL
							LOG_INFO(mLogData.logger,
							         "[{}] Ignoring superior DisplayMode [{:#08x}] with width {} fps {:.3f}",
							         mLogData.prefix, static_cast<int>(dm), dmWidth, dmFps);
							#endif
						}
						else
						{
							#ifndef NO_QUILL
							LOG_INFO(mLogData.logger,
							         "[{}] Found superior DisplayMode [{:#08x}] with width {} fps {:.3f}",
							         mLogData.prefix, static_cast<int>(dm), dmWidth, dmFps);
							#endif

							LoadSignalFromDisplayMode(&mVideoSignal, displayMode);
							width = dmWidth;
							fps = dmFps;
						}
					}
					else
					{
						#ifndef NO_QUILL
						LOG_INFO(mLogData.logger,
						         "[{}] Ignoring inferior DisplayMode [{:#08x}] with width {} fps {:.3f}",
						         mLogData.prefix, static_cast<int>(dm), dmWidth, dmFps);
						#endif
					}
				}
				else
				{
					break;
				}
			}

			if (width == 0)
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to find a DisplayMode, ignoring device",
				          mLogData.prefix, result);
				#endif

				mDeckLinkNotification = nullptr;
				mDeckLinkStatus = nullptr;
				mDeckLinkHDMIInputEDID = nullptr;
				mDeckLinkInput = nullptr;
				mDeckLink.Release();
				mDeckLink = nullptr;
			}

			if (mDeckLink == nullptr)
			{
				continue;
			}

			mDeviceInfo.name = deviceName;
			mDeviceInfo.audioChannelCount = audioChannelCount;
			mDeviceInfo.inputFormatDetection = inputFormatDetection;
			mDeviceInfo.hdrMetadata = hdrMetadata;
			mDeviceInfo.colourspaceMetadata = colourspaceMetadata;
			mDeviceInfo.dynamicRangeMetadata = dynamicRangeMetadata;

			const LONGLONG all = bmdDynamicRangeSDR | bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG;
			result = mDeckLinkHDMIInputEDID->SetInt(bmdDeckLinkHDMIInputEDIDDynamicRange, all);
			if (S_OK == result)
			{
				result = mDeckLinkHDMIInputEDID->WriteToEDID();
				if (S_OK != result)
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger, "[{}] Unable to WriteToEDID [{:#08x}]", mLogData.prefix,
					          static_cast<unsigned long>(result));
					#endif
				}
			}
			else
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to set dynamic range flags [{:#08x}]", mLogData.prefix,
				          static_cast<unsigned long>(result));
				#endif
			}
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "Ignoring device {} {}, does not support capture", idx, deviceName);
			#endif
		}
	}

	if (mDeckLink)
	{
		int64_t val;
		if (SUCCEEDED(mDeckLinkStatus->GetInt(bmdDeckLinkStatusPCIExpressLinkWidth, &val)))
		{
			mDeviceInfo.linkWidth = val;
		}
		if (SUCCEEDED(mDeckLinkStatus->GetInt(bmdDeckLinkStatusPCIExpressLinkSpeed, &val)))
		{
			mDeviceInfo.linkSpeed = val;
		}
		if (SUCCEEDED(mDeckLinkStatus->GetInt(bmdDeckLinkStatusDeviceTemperature, &val)))
		{
			mDeviceInfo.temperature = val;
		}

		blackmagic_capture_filter::OnDeviceUpdated();
		blackmagic_capture_filter::OnVideoSignalLoaded(&mVideoSignal);
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] No valid devices found", mLogData.prefix);
		#endif
	}

	mClock = new BMReferenceClock(phr);

	// video pin must have a default format in order to ensure a renderer is present in the graph
	LoadFormat(&mVideoFormat, &mVideoSignal);

	// compatibility bodge for renderers (madvr) that don't resize buffers correctly at all times
	if (mVideoSignal.pixelFormat == bmdFormat8BitARGB)
	{
		mVideoFormat.pixelFormat = RGBA;
		mVideoFormat.CalculateDimensions();
	}

	#ifndef NO_QUILL
	LOG_WARNING(
		mLogData.logger,
		"[{}] Initialised video format {} x {} ({}:{}) @ {:.3f} Hz in {} bits ({} {} tf: {}) size {} bytes",
		mLogData.prefix, mVideoFormat.cx, mVideoFormat.cy, mVideoFormat.aspectX, mVideoFormat.aspectY, mVideoFormat.fps,
		mVideoFormat.pixelFormat.bitDepth, mVideoFormat.pixelFormat.name, mVideoFormat.colourFormatName,
		mVideoFormat.hdrMeta.transferFunction, mVideoFormat.imageSize);
	#endif

	auto vp = new blackmagic_video_capture_pin(phr, this, false, mVideoFormat);
	vp->UpdateFrameWriterStrategy();
	vp->ResizeMetrics(mVideoFormat.fps);

	vp = new blackmagic_video_capture_pin(phr, this, true, mVideoFormat);
	vp->UpdateFrameWriterStrategy();
	vp->ResizeMetrics(mVideoFormat.fps);

	if (mDeviceInfo.audioChannelCount > 0)
	{
		mAudioSignal.bitDepth = 16;
		mAudioSignal.channelCount = mDeviceInfo.audioChannelCount;
		LoadFormat(&mAudioFormat, &mAudioSignal);
		OnAudioSignalLoaded(&mAudioSignal);
		OnAudioFormatLoaded(&mAudioFormat);

		auto ap = new blackmagic_audio_capture_pin(phr, this, false);
		ap->ResizeMetrics(mVideoFormat.fps);

		ap = new blackmagic_audio_capture_pin(phr, this, true);
		ap->ResizeMetrics(mVideoFormat.fps);
	}

	if (deckLinkIterator)
	{
		deckLinkIterator->Release();
	}
}

blackmagic_capture_filter::~blackmagic_capture_filter()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Tearing down filter", mLogData.prefix);
	#endif
	if (mDeckLinkNotification)
	{
		mDeckLinkNotification->Unsubscribe(bmdStatusChanged, this);
	}
	CloseHandle(mVideoFrameEvent);
}

void blackmagic_capture_filter::LoadSignalFromDisplayMode(video_signal* newSignal, IDeckLinkDisplayMode* newDisplayMode)
{
	newSignal->displayMode = newDisplayMode->GetDisplayMode();

	BMDTimeValue frameDuration;
	BMDTimeScale frameDurationScale;
	newDisplayMode->GetFrameRate(&frameDuration, &frameDurationScale);

	newSignal->frameDuration = static_cast<uint32_t>(frameDuration);
	newSignal->frameDurationScale = static_cast<uint16_t>(frameDurationScale);
	newSignal->cx = newDisplayMode->GetWidth(); // NOLINT(clang-diagnostic-implicit-int-conversion)
	newSignal->cy = newDisplayMode->GetHeight(); // NOLINT(clang-diagnostic-implicit-int-conversion)

	BSTR displayModeStr = nullptr;
	if (newDisplayMode->GetName(&displayModeStr) == S_OK)
	{
		newSignal->displayModeName = BSTRToStdString(displayModeStr);
		DeleteString(displayModeStr);
	}
	newSignal->calc_derived_values();
}

HRESULT blackmagic_capture_filter::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents,
                                                           IDeckLinkDisplayMode* newDisplayMode,
                                                           BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	video_signal newSignal = mVideoSignal;
	if (notificationEvents & bmdVideoInputColorspaceChanged)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] VideoInputFormatChanged::bmdVideoInputColorspaceChanged {}",
		             mLogData.prefix, detectedSignalFlags);
		#endif

		if (detectedSignalFlags & bmdDetectedVideoInputYCbCr422)
		{
			newSignal.colourFormat = "YUV 4:2:2";
			if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
			{
				newSignal.pixelLayout = "2VUY";
				newSignal.pixelFormat = bmdFormat8BitYUV;
				newSignal.bitDepth = 8;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
			{
				newSignal.pixelLayout = "V210";
				newSignal.pixelFormat = bmdFormat10BitYUV;
				newSignal.bitDepth = 10;
			}
			else
			{
				return E_FAIL;
			}
		}
		else if (detectedSignalFlags & bmdDetectedVideoInputRGB444)
		{
			newSignal.colourFormat = "RGB 4:4:4";
			if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
			{
				newSignal.pixelLayout = "ARGB";
				newSignal.pixelFormat = bmdFormat8BitARGB;
				newSignal.bitDepth = 8;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
			{
				newSignal.pixelLayout = "R210";
				newSignal.pixelFormat = bmdFormat10BitRGB;
				newSignal.bitDepth = 10;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput12BitDepth)
			{
				newSignal.pixelLayout = "R12B";
				newSignal.pixelFormat = bmdFormat12BitRGB;
				newSignal.bitDepth = 12;
			}
			else
			{
				return E_FAIL;
			}
		}
	}

	if (notificationEvents & bmdVideoInputDisplayModeChanged)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] VideoInputFormatChanged::bmdVideoInputDisplayModeChanged", mLogData.prefix);
		#endif

		LoadSignalFromDisplayMode(&newSignal, newDisplayMode);
	}

	if (notificationEvents & (bmdVideoInputDisplayModeChanged | bmdVideoInputColorspaceChanged))
	{
		{
			CAutoLock lck(&mDeckLinkSec);

			if (mRunningPins > 0)
			{
				#ifndef NO_QUILL
				LOG_INFO(mLogData.logger, "[{}] Restarting video capture on input format change", mLogData.prefix);
				#endif

				REFERENCE_TIME t1;
				GetReferenceTime(&t1);

				// Pause the stream, update the capture format, flush and start streams
				auto result = mDeckLinkInput->PauseStreams();
				if (S_OK != result)
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger, "[{}] Failed to pause streams on input format change ({:#08x})",
					            mLogData.prefix, static_cast<unsigned long>(result));
					#endif
				}

				result = mDeckLinkInput->EnableVideoInput(newSignal.displayMode, newSignal.pixelFormat,
				                                          bmdVideoInputEnableFormatDetection);
				if (S_OK == result)
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] Enabled video input on input format change (displayMode: {:#08x} {} pixelFormat: {:#08x} {})",
					            mLogData.prefix, static_cast<int>(newDisplayMode->GetDisplayMode()),
					            to_string(newDisplayMode->GetDisplayMode()), static_cast<int>(newSignal.pixelFormat),
					            to_string(newSignal.pixelFormat));
					#endif
				}
				else
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger, "[{}] Failed to enable video input on input format change ({:#08x})",
					            mLogData.prefix, static_cast<unsigned long>(result));
					#endif
				}

				result = mDeckLinkInput->FlushStreams();
				#ifndef NO_QUILL
				if (S_OK != result)
				{
					LOG_WARNING(mLogData.logger, "[{}] Failed to flush streams on input format change ({:#08x})",
					            mLogData.prefix, static_cast<unsigned long>(result));
				}
				else
				{
					LOG_INFO(mLogData.logger, "[{}] Flushed streams on input format change", mLogData.prefix);
				}
				#endif

				mPreviousVideoFrameTime = invalidFrameTime;
				mPreviousAudioFrameTime = invalidFrameTime;

				result = mDeckLinkInput->StartStreams();
				if (S_OK != result)
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger, "[{}] Failed to start streams on input format change ({:#08x})",
					            mLogData.prefix, static_cast<unsigned long>(result));
					#endif
				}
				else
				{
					REFERENCE_TIME t2;
					GetReferenceTime(&t2);

					auto delta = t2 - t1;
					mVideoFrameTime += delta;
					mAudioFrameTime += delta;

					#ifndef NO_QUILL
					LOG_INFO(mLogData.logger,
					         "[{}] Restarted video capture on input format change at {}, frame time increased by {} to compensate",
					         mLogData.prefix, t2, delta);
					#endif
				}
			}

			mVideoSignal = newSignal;
		}

		OnVideoSignalLoaded(&mVideoSignal);
	}

	return S_OK;
}

HRESULT blackmagic_capture_filter::processVideoFrame(IDeckLinkVideoInputFrame* videoFrame,
                                                     const int64_t& frameNotificationTime)
{
	int64_t frameTime = 0;
	int64_t frameDuration = 0;
	bool locked = true;
	auto result = videoFrame->GetStreamTime(&frameTime, &frameDuration, dshowTicksPerSecond);
	if (S_OK == result)
	{
		if ((videoFrame->GetFlags() & bmdFrameHasNoInputSource) != 0)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Signal is not locked at {}", mLogData.prefix, frameTime);
			#endif

			locked = false;
		}
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Discarding video frame, unable to get stream time {:#08x}",
		          mLogData.prefix, static_cast<unsigned long>(result));
		#endif

		return E_FAIL;
	}

	auto frameIndexIncrement = 1LL;
	if (mPreviousVideoFrameTime != invalidFrameTime && locked)
	{
		const auto framesSinceLast = (frameTime - mPreviousVideoFrameTime) / frameDuration;
		auto missedFrames = std::max(framesSinceLast - 1, 0LL);
		if (missedFrames > 0LL)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Video capture discontinuity detected, {} frames missed at frame {} ({} - {} / {}), increasing frame time to compensate",
			            mLogData.prefix, missedFrames, mCurrentVideoFrameIndex,
			            frameTime, mPreviousVideoFrameTime, frameDuration);
			#endif
			frameIndexIncrement += missedFrames;
		}
	}

	mVideoFrameTime += frameIndexIncrement * frameDuration;
	mCurrentVideoFrameIndex += frameIndexIncrement;
	mPreviousVideoFrameTime = frameTime;

	#ifndef NO_QUILL
	LOG_TRACE_L2(mLogData.logger, "[{}] Captured video frame {} at {} (locked: {})", mLogData.prefix,
	             mCurrentVideoFrameIndex, frameTime, locked);
	#endif

	auto wasLocked = mVideoSignal.locked;
	mVideoSignal.locked = locked;
	if (wasLocked != locked)
	{
		OnVideoSignalLoaded(&mVideoSignal);
	}
	if (!locked)
	{
		return E_FAIL;
	}

	int64_t referenceFrameTime;
	int64_t referenceFrameDuration;

	// Get the captured timestamp for the incoming frame
	if (videoFrame->GetHardwareReferenceTimestamp(referenceTimescale, &referenceFrameTime, &referenceFrameDuration)
		!= S_OK)
		return E_FAIL;

	// metadata
	video_format newVideoFormat{};
	LoadFormat(&newVideoFormat, &mVideoSignal);

	auto doubleValue = 0.0;
	auto intValue = 0LL;
	CComQIPtr<IDeckLinkVideoFrameMetadataExtensions> metadataExtensions(videoFrame);

	result = metadataExtensions->GetInt(bmdDeckLinkFrameMetadataColorspace, &intValue);
	if (S_OK == result)
	{
		switch (static_cast<BMDColorspace>(intValue))
		{
		case bmdColorspaceRec601:
			newVideoFormat.colourFormat = REC601;
			newVideoFormat.colourFormatName = "REC601";
			break;
		case bmdColorspaceRec709:
			newVideoFormat.colourFormat = REC709;
			newVideoFormat.colourFormatName = "REC709";
			break;
		case bmdColorspaceRec2020:
			newVideoFormat.colourFormat = BT2020;
			newVideoFormat.colourFormatName = "BT2020";
			break;
		case bmdColorspaceP3D65:
			newVideoFormat.colourFormat = P3D65;
			newVideoFormat.colourFormatName = "P3D65";
			break;
		case bmdColorspaceDolbyVisionNative:
			newVideoFormat.colourFormat = COLOUR_FORMAT_UNKNOWN;
			newVideoFormat.colourFormatName = "?";
			break;
		case bmdColorspaceUnknown:
			newVideoFormat.colourFormat = COLOUR_FORMAT_UNKNOWN;
			newVideoFormat.colourFormatName = "?";
			break;
		}
	}

	// HDR meta data
	result = metadataExtensions->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &intValue);
	if (S_OK == result)
	{
		// EOTF in range 0-7 as per CEA 861.3 aka A2016 HDR STATIC METADATA EXTENSIONS.
		// 0=SDR, 1=HDR, 2=PQ, 3=HLG, 4-7=future use
		switch (intValue)
		{
		case 0:
			newVideoFormat.hdrMeta.transferFunction = 4;
			break;
		case 2:
			newVideoFormat.hdrMeta.transferFunction = 15;
			break;
		case 3:
			newVideoFormat.hdrMeta.transferFunction = 16;
			break;
		default:
			newVideoFormat.hdrMeta.transferFunction = 4;
		}
	}
	else if (newVideoFormat.colourFormat == BT2020)
	{
		newVideoFormat.hdrMeta.transferFunction = 15;
	}

	if (videoFrame->GetFlags() & bmdFrameContainsHDRMetadata)
	{
		#ifndef NO_QUILL
		std::string invalids{};
		#endif
		// Primaries
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.b_primary_x = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.b_primary_y = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY);
			#endif
		}

		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.r_primary_x = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.r_primary_y = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY);
			#endif
		}

		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.g_primary_x = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.g_primary_y = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY);
			#endif
		}

		// White point
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointX, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.whitepoint_x = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRWhitePointX);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointY, &doubleValue);
		if (S_OK == result && isInCieRange(doubleValue))
		{
			newVideoFormat.hdrMeta.whitepoint_y = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRWhitePointY);
			#endif
		}

		// DML
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance,
		                                      &doubleValue);
		if (S_OK == result && std::fabs(doubleValue) > 0.000001)
		{
			newVideoFormat.hdrMeta.minDML = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance,
		                                      &doubleValue);
		if (S_OK == result && std::fabs(doubleValue) > 0.000001)
		{
			newVideoFormat.hdrMeta.maxDML = doubleValue;
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance);
			#endif
		}

		// MaxCLL MaxFALL
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel, &doubleValue);
		if (S_OK == result && std::fabs(doubleValue) > 0.000001)
		{
			newVideoFormat.hdrMeta.maxCLL = std::lround(doubleValue);
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? ", " : "";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel);
			#endif
		}
		result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel,
		                                      &doubleValue);
		if (S_OK == result && std::fabs(doubleValue) > 0.000001)
		{
			newVideoFormat.hdrMeta.maxFALL = std::lround(doubleValue);
		}
		else
		{
			#ifndef NO_QUILL
			invalids += invalids.empty() ? "" : ", ";
			invalids += to_string(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel);
			#endif
		}

		#ifndef NO_QUILL
		if (!mInvalidHdrMetaDataItems || (mInvalidHdrMetaDataItems && invalids != *mInvalidHdrMetaDataItems))
		{
			if (invalids.empty())
			{
				LOG_TRACE_L1(mLogData.logger, "[{}] All HDR metadata is present", mLogData.prefix);
			}
			else
			{
				LOG_TRACE_L1(mLogData.logger, "[{}] Invalid HDR metadata detected in {}", mLogData.prefix,
				             invalids);
			}
			mInvalidHdrMetaDataItems = std::make_unique<std::string>(std::move(invalids));
		}
		#endif
	}
	else if (!newVideoFormat.hdrMeta.exists() && mVideoFormat.hdrMeta.exists())
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] HDR metadata has been removed", mLogData.prefix);
		#endif
	}

	{
		CAutoLock lock(&mFrameSec);

		if (newVideoFormat.frameInterval != mVideoFormat.frameInterval)
		{
			for (auto i = 0; i < m_iPins; i++)
			{
				auto pin = dynamic_cast<capture_pin*>(m_paStreams[i]);
				pin->ResizeMetrics(newVideoFormat.fps);
			}
		}

		mVideoFormat = newVideoFormat;
		mVideoFrame = std::make_shared<video_frame>(mLogData, newVideoFormat, frameNotificationTime, mVideoFrameTime,
		                                            frameDuration, mCurrentVideoFrameIndex, videoFrame);
	}

	// signal listeners
	if (!SetEvent(mVideoFrameEvent))
	{
		auto err = GetLastError();
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Failed to notify on video frame {:#08x}", mLogData.prefix, err);
		#endif
	}

	return S_OK;
}

HRESULT blackmagic_capture_filter::processAudioPacket(IDeckLinkAudioInputPacket* audioPacket,
                                                      const int64_t& frameNotificationTime)
{
	void* audioData = nullptr;
	auto result = audioPacket->GetBytes(&audioData);
	if (S_OK != result)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Failed to get audioFrame bytes {:#08x})", mLogData.prefix,
		            static_cast<unsigned long>(result));
		#endif

		return E_FAIL;
	}

	int64_t frameTime;
	result = audioPacket->GetPacketTime(&frameTime, dshowTicksPerSecond);
	if (S_OK != result)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Failed to get audioFrame bytes {:#08x})", mLogData.prefix,
		            static_cast<unsigned long>(result));
		#endif

		return E_FAIL;
	}

	auto frameIndexIncrement = 1LL;
	if (mPreviousAudioFrameTime != invalidFrameTime && mVideoSignal.locked)
	{
		const auto framesSinceLast = (frameTime - mPreviousAudioFrameTime) / mVideoSignal.frameInterval;
		auto missedFrames = std::max(framesSinceLast - 1, 0LL);
		if (missedFrames > 0LL)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
			            "[{}] Audio capture discontinuity detected, {} frames missed at frame {} ({} - {} / {}), increasing frame time to compensate",
			            mLogData.prefix, missedFrames, mCurrentAudioFrameIndex,
			            frameTime, mPreviousAudioFrameTime, mVideoSignal.frameInterval);
			#endif
			frameIndexIncrement += missedFrames;
		}
	}
	mAudioFrameTime += mVideoSignal.frameInterval * frameIndexIncrement;
	mCurrentAudioFrameIndex += frameIndexIncrement;
	mPreviousAudioFrameTime = frameTime;

	#ifndef NO_QUILL
	LOG_TRACE_L2(mLogData.logger, "[{}] Captured audio frame {} at {} (locked: {})", mLogData.prefix,
	             mCurrentAudioFrameIndex, frameTime, mVideoSignal.locked);
	#endif

	if (!mVideoSignal.locked)
	{
		return E_FAIL;
	}

	{
		auto audioByteDepth = audioBitDepth / 8;
		auto audioFrameLength = audioPacket->GetSampleFrameCount() * mDeviceInfo.audioChannelCount * audioByteDepth;

		CAutoLock lock(&mFrameSec);
		mAudioFrame = std::make_shared<audio_frame>(mLogData, frameNotificationTime, mAudioFrameTime, audioData,
		                                            audioFrameLength,
		                                            mAudioFormat, mCurrentAudioFrameIndex, audioPacket);
	}

	// signal listeners
	if (!SetEvent(mAudioFrameEvent))
	{
		auto err = GetLastError();
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Failed to notify on audio frame {:#08x}", mLogData.prefix, err);
		#endif
	}
	return S_OK;
}

HRESULT blackmagic_capture_filter::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                                          IDeckLinkAudioInputPacket* audioPacket)
{
	int64_t frameArrivalTime;
	GetReferenceTime(&frameArrivalTime);

	auto hasVideo = videoFrame != nullptr;
	auto hasAudio = audioPacket != nullptr;

	#ifndef NO_QUILL
	LOG_TRACE_L2(mLogData.logger, "[{}] Frame arrived at {} (V/A : {}/{})", mLogData.prefix, frameArrivalTime,
	             hasVideo, hasAudio);
	#endif

	auto retVal = S_OK;

	if (hasVideo)
	{
		retVal = processVideoFrame(videoFrame, frameArrivalTime);
	}

	if (hasAudio)
	{
		auto hr = processAudioPacket(audioPacket, frameArrivalTime);
		if (retVal != S_OK)
		{
			retVal = hr;
		}
	}

	return retVal;
}

void blackmagic_capture_filter::LoadFormat(audio_format* audioFormat, const audio_signal* audioSignal)
{
	auto audioIn = *audioSignal;
	// https://ia903006.us.archive.org/11/items/CEA-861-E/CEA-861-E.pdf
	switch (audioIn.channelCount)
	{
	case 2:
		audioFormat->inputChannelCount = 2;
		audioFormat->outputChannelCount = 2;
		audioFormat->channelMask = KSAUDIO_SPEAKER_STEREO;
		audioFormat->channelOffsets.fill(not_present);
		audioFormat->channelOffsets[0] = 0;
		audioFormat->channelOffsets[1] = 0;
		audioFormat->lfeChannelIndex = not_present;
		audioFormat->channelLayout = "FL FR";
		break;
	case 4:
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
		break;
	case 6:
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
		break;
	case 8:
		audioFormat->inputChannelCount = 8;
		audioFormat->outputChannelCount = 8;
		audioFormat->channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		audioFormat->channelOffsets.fill(0);
		// swap LFE and FC
		audioFormat->channelOffsets[2] = 1;
		audioFormat->channelOffsets[3] = -1;
		audioFormat->channelOffsets[4] = 2;
		audioFormat->channelOffsets[5] = 2;
		audioFormat->channelOffsets[6] = -2;
		audioFormat->channelOffsets[7] = -2;
		audioFormat->lfeChannelIndex = 2;
		audioFormat->channelLayout = "FL FR FC LFE BL BR SL SR";
		break;
	default:
		audioFormat->inputChannelCount = 0;
		audioFormat->outputChannelCount = 0;
		audioFormat->channelOffsets.fill(not_present);
		audioFormat->lfeChannelIndex = not_present;
	}
}

HRESULT blackmagic_capture_filter::Run(REFERENCE_TIME tStart)
{
	mVideoFrameTime = 0;
	mAudioFrameTime = 0;
	return hdmi_capture_filter::Run(tStart);
}

HRESULT blackmagic_capture_filter::Notify(BMDNotifications topic, ULONGLONG param1, ULONGLONG param2)
{
	// only interested in status changes
	if (topic != bmdStatusChanged)
	{
		return S_OK;
	}

	int64_t intVal = -1;
	BOOL boolVal = -1;
	HRESULT hr = S_OK;
	std::string desc;
	const BMDDeckLinkStatusID statusId = static_cast<BMDDeckLinkStatusID>(param1);
	switch (statusId)
	{
	case bmdDeckLinkStatusPCIExpressLinkWidth:
		hr = mDeckLinkStatus->GetInt(statusId, &intVal);
		if (intVal != -1) mDeviceInfo.linkWidth = intVal;
		desc = "PCIe Link Width";
		break;
	case bmdDeckLinkStatusPCIExpressLinkSpeed:
		hr = mDeckLinkStatus->GetInt(statusId, &intVal);
		if (intVal != -1) mDeviceInfo.linkSpeed = intVal;
		desc = "PCIe Link Speed";
		break;
	case bmdDeckLinkStatusDeviceTemperature:
		hr = mDeckLinkStatus->GetInt(statusId, &intVal);
		if (intVal != -1) mDeviceInfo.temperature = intVal;
		desc = "Device Temperature";
		break;
	case bmdDeckLinkStatusVideoInputSignalLocked:
		hr = mDeckLinkStatus->GetFlag(statusId, &boolVal);
		if (boolVal != -1)
		{
			bool isLocked = boolVal == TRUE;
			if (isLocked != mVideoSignal.locked)
			{
				mVideoSignal.locked = isLocked;
				OnVideoSignalLoaded(&mVideoSignal);
			}
		}
		desc = "Signal Locked";
		break;
	default:
		break;
	}
	if (!desc.empty())
	{
		if (hr == S_OK)
		{
			OnDeviceUpdated();

			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] {} is {}", mLogData.prefix, desc, intVal > -1 ? intVal : boolVal);
			#endif
		}
		#ifndef NO_QUILL
		else
		{
			LOG_TRACE_L1(mLogData.logger, "[{}] Failed to read {} {:#08x}", mLogData.prefix, desc,
			             static_cast<unsigned long>(hr));
		}
		#endif
	}
	return S_OK;
}

HRESULT blackmagic_capture_filter::QueryInterface(const IID& riid, void** ppvObject)
{
	if (riid == _uuidof(IDeckLinkInputCallback))
	{
		return GetInterface(static_cast<IDeckLinkInputCallback*>(this), ppvObject);
	}
	if (riid == _uuidof(IDeckLinkNotificationCallback))
	{
		return GetInterface(static_cast<IDeckLinkNotificationCallback*>(this), ppvObject);
	}

	return hdmi_capture_filter::NonDelegatingQueryInterface(riid, ppvObject);
}

void blackmagic_capture_filter::OnVideoSignalLoaded(video_signal* vs)
{
	mVideoInputStatus.inX = vs->cx;
	mVideoInputStatus.inY = vs->cy;
	mVideoInputStatus.inAspectX = vs->aspectX;
	mVideoInputStatus.inAspectY = vs->aspectY;
	mVideoInputStatus.inFps = vs->fps;
	mVideoInputStatus.inFrameDuration = vs->frameInterval;
	mVideoInputStatus.inBitDepth = vs->bitDepth;
	mVideoInputStatus.signalStatus = vs->locked ? "Locked" : "No Signal";
	mVideoInputStatus.inColourFormat = vs->colourFormat;
	mVideoInputStatus.inQuantisation = "Full";
	mVideoInputStatus.inSaturation = "Full";
	mVideoInputStatus.inPixelLayout = vs->pixelLayout;
	mVideoInputStatus.validSignal = vs->locked;

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mVideoInputStatus);
	}
}

void blackmagic_capture_filter::OnAudioSignalLoaded(audio_signal* as)
{
	mAudioInputStatus.audioInStatus = true;
	mAudioInputStatus.audioInIsPcm = true;
	mAudioInputStatus.audioInBitDepth = as->bitDepth;
	mAudioInputStatus.audioInFs = 48000;
	mAudioInputStatus.audioInChannelPairs = 0;
	mAudioInputStatus.audioInChannelMap = as->channelCount;
	mAudioInputStatus.audioInLfeLevel = 0;

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mAudioInputStatus);
	}
}

void blackmagic_capture_filter::OnDeviceUpdated()
{
	auto prev = mDeviceStatus.deviceDesc;
	mDeviceStatus.deviceDesc = std::format("{0} [{1}.{2}.{3}]", mDeviceInfo.name, mDeviceInfo.apiVersion[0],
	                                       mDeviceInfo.apiVersion[1], mDeviceInfo.apiVersion[2]);
	mDeviceStatus.linkSpeed = mDeviceInfo.linkSpeed;
	mDeviceStatus.linkWidth = mDeviceInfo.linkWidth;
	mDeviceStatus.temperature = mDeviceInfo.temperature;

	#ifndef NO_QUILL
	if (prev != mDeviceStatus.deviceDesc)
	{
		LOG_INFO(mLogData.logger, "[{}] Recorded device description: {}", mLogData.prefix, mDeviceStatus.deviceDesc);
	}
	#endif

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mDeviceStatus);
	}
}

HRESULT blackmagic_capture_filter::PinThreadCreated()
{
	HRESULT result = S_OK;
	CAutoLock lock(&mDeckLinkSec);

	if (++mRunningPins == 1)
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] First pin started, starting streams", mLogData.prefix);
		#endif

		result = mDeckLinkNotification->Subscribe(bmdStatusChanged, this);
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Subscribed for status notifications", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to subscribe for status notifications [{:#08x}]",
			          mLogData.prefix, static_cast<unsigned long>(result));
			#endif
		}

		result = mDeckLinkInput->EnableVideoInput(mVideoSignal.displayMode, mVideoSignal.pixelFormat,
		                                          bmdVideoInputEnableFormatDetection);
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Enabled Video Input in display mode {}", mLogData.prefix,
			         mVideoSignal.displayModeName);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to EnableVideoInput [{:#08x}]", mLogData.prefix,
			          static_cast<unsigned long>(result));
			#endif
		}

		if (mDeviceInfo.audioChannelCount > 0)
		{
			result = mDeckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, audioBitDepth,
			                                          mDeviceInfo.audioChannelCount);
			// NOLINT(clang-diagnostic-shorten-64-to-32) values will only be 0/2/8/16
			if (S_OK == result)
			{
				#ifndef NO_QUILL
				LOG_INFO(mLogData.logger, "[{}] Enabled Audio Input for {} channels", mLogData.prefix,
				         mDeviceInfo.audioChannelCount);
				#endif
			}
			else
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to EnableAudioInput [{:#08x}]", mLogData.prefix,
				          static_cast<unsigned long>(result));
				#endif
			}
		}

		result = mDeckLinkInput->StartStreams();
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Input streams started successfully", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to start input streams (result {:#08x})", mLogData.prefix,
			            static_cast<unsigned long>(result));
			#endif
		}
	}
	else
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] {} pins are running", mLogData.prefix, mRunningPins);
		#endif
	}
	return S_OK;
}

HRESULT blackmagic_capture_filter::PinThreadDestroyed()
{
	HRESULT result = S_OK;
	CAutoLock lock(&mDeckLinkSec);

	if (--mRunningPins == 0)
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Last pin stopped, stopping streams", mLogData.prefix);
		#endif


		result = mDeckLinkInput->StopStreams();
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Input streams stopped successfully", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to stop input streams (result {:#08x})", mLogData.prefix,
			            static_cast<unsigned long>(result));
			#endif
		}

		if (mDeviceInfo.audioChannelCount > 0)
		{
			result = mDeckLinkInput->DisableAudioInput();
			if (S_OK == result)
			{
				#ifndef NO_QUILL
				LOG_INFO(mLogData.logger, "[{}] Disabled Audio Input", mLogData.prefix);
				#endif
			}
			else
			{
				#ifndef NO_QUILL
				LOG_ERROR(mLogData.logger, "[{}] Unable to DisableAudioInput [{:#08x}]", mLogData.prefix,
				          static_cast<unsigned long>(result));
				#endif
			}
		}

		result = mDeckLinkInput->DisableVideoInput();
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Disabled Video Input", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to DisableVideoInput [{:#08x}]", mLogData.prefix,
			          static_cast<unsigned long>(result));
			#endif
		}

		result = mDeckLinkNotification->Unsubscribe(bmdStatusChanged, this);
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Unsubscribed from status notifications", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Unable to unsubscribe from status notifications [{:#08x}]",
			          mLogData.prefix, static_cast<unsigned long>(result));
			#endif
		}
	}
	else
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Pin stopped, {} pins are still running", mLogData.prefix, mRunningPins);
		#endif
	}
	return result;
}
