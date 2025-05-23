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
#include "bmcapture.h"
// linking side data GUIDs fails without this
#include <initguid.h>

#include <cmath>
// std::reverse
#include <algorithm>

#ifndef NO_QUILL
#include "quill/Backend.h"
#include "quill/sinks/FileSink.h"
#include <string_view>
#include <utility>
#include "quill/StopWatch.h"
#endif

// audio is limited to 48kHz and an audio packet is only delivered with a video frame
// lowest fps is 23.976 so the max no of samples should be 48000/(24000/1001) = 2002
// but there can be backlogs so allow for a few frames for safety
constexpr uint16_t maxSamplesPerFrame = 8192;
constexpr uint16_t minDisplayWidth = 4096;

//////////////////////////////////////////////////////////////////////////
// BlackmagicCaptureFilter
//////////////////////////////////////////////////////////////////////////
CUnknown* BlackmagicCaptureFilter::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{
	auto pNewObject = new BlackmagicCaptureFilter(punk, phr);

	if (pNewObject == nullptr)
	{
		if (phr)
			*phr = E_OUTOFMEMORY;
	}

	return pNewObject;
}

void BlackmagicCaptureFilter::LoadFormat(VIDEO_FORMAT* videoFormat, const VIDEO_SIGNAL* videoSignal)
{
	videoFormat->cx = videoSignal->cx;
	videoFormat->cy = videoSignal->cy;
	videoFormat->fps = static_cast<double>(videoSignal->frameDurationScale) / static_cast<double>(videoSignal->
		frameDuration);
	videoFormat->frameInterval = 10000000 / videoFormat->fps;
	videoFormat->quantisation = QUANTISATION_FULL;
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
	videoFormat->CalculateDimensions();
}

BlackmagicCaptureFilter::BlackmagicCaptureFilter(LPUNKNOWN punk, HRESULT* phr) :
	HdmiCaptureFilter(L"BlackmagicCaptureFilter", punk, phr, CLSID_BMCAPTURE_FILTER, "Blackmagic"),
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
		BlackmagicCaptureFilter::OnDeviceSelected();
		BlackmagicCaptureFilter::OnVideoSignalLoaded(&mVideoSignal);
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] No valid devices found", mLogData.prefix);
		#endif
	}

	mClock = new BMReferenceClock(phr, mDeckLinkInput);

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

	new BlackmagicVideoCapturePin(phr, this, false, mVideoFormat);
	new BlackmagicVideoCapturePin(phr, this, true, mVideoFormat);

	if (mDeviceInfo.audioChannelCount > 0)
	{
		mAudioSignal.bitDepth = 16;
		mAudioSignal.channelCount = mDeviceInfo.audioChannelCount;
		LoadFormat(&mAudioFormat, &mAudioSignal);
		OnAudioSignalLoaded(&mAudioSignal);
		OnAudioFormatLoaded(&mAudioFormat);

		new BlackmagicAudioCapturePin(phr, this, false);
		new BlackmagicAudioCapturePin(phr, this, true);
	}

	if (deckLinkIterator)
	{
		deckLinkIterator->Release();
	}
}

BlackmagicCaptureFilter::~BlackmagicCaptureFilter()
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

void BlackmagicCaptureFilter::LoadSignalFromDisplayMode(VIDEO_SIGNAL* newSignal, IDeckLinkDisplayMode* newDisplayMode)
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
}

HRESULT BlackmagicCaptureFilter::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents,
                                                         IDeckLinkDisplayMode* newDisplayMode,
                                                         BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	VIDEO_SIGNAL newSignal = mVideoSignal;
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
					#ifndef NO_QUILL
					LOG_INFO(mLogData.logger, "[{}] Restarted video capture on input format change", mLogData.prefix);
					#endif
				}
			}

			mVideoSignal = newSignal;
		}

		OnVideoSignalLoaded(&mVideoSignal);
	}

	return S_OK;
}

HRESULT BlackmagicCaptureFilter::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                                        IDeckLinkAudioInputPacket* audioPacket)
{
	REFERENCE_TIME now;
	GetReferenceTime(&now);

	if (videoFrame)
	{
		int64_t frameTime = 0;
		int64_t frameDuration = 0;
		auto result = videoFrame->GetStreamTime(&frameTime, &frameDuration, dshowTicksPerSecond);
		if (S_OK == result)
		{
			if ((videoFrame->GetFlags() & bmdFrameHasNoInputSource) != 0)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L2(mLogData.logger, "[{}] Signal is not locked at {}", mLogData.prefix, frameTime);
				#endif

				return E_FAIL;
			}

			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] Captured video frame at {} (duration: {})", mLogData.prefix, frameTime,
			             frameDuration);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Discarding video frame, unable to get reference timestamp {:#08x}",
			          mLogData.prefix, static_cast<unsigned long>(result));
			#endif

			return E_FAIL;
		}

		VIDEO_FORMAT newVideoFormat{};
		LoadFormat(&newVideoFormat, &mVideoSignal);

		if (mPreviousVideoFrameTime == invalidFrameTime)
		{
			mCurrentVideoFrameIndex++;
		}
		else
		{
			const auto framesSinceLast = (frameTime - mPreviousVideoFrameTime) / frameDuration;
			mCurrentVideoFrameIndex += framesSinceLast;
			#ifndef NO_QUILL
			if (auto missedFrames = std::max(framesSinceLast - 1, 0LL))
			{
				LOG_WARNING(mLogData.logger, "[{}] Video capture discontinuity detected, {} frames missed at frame {}",
				            mLogData.prefix, missedFrames, mCurrentVideoFrameIndex);
			}
			#endif
		}
		mPreviousVideoFrameTime = frameTime;

		// metadata
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
		HDR_META hdr = newVideoFormat.hdrMeta;
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
			// Primaries
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.b_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.b_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.r_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.r_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.g_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.g_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			// White point
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointX, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.whitepoint_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointY, &doubleValue);
			if (S_OK == result && isInCieRange(doubleValue))
			{
				newVideoFormat.hdrMeta.whitepoint_y = doubleValue;
			}
			else
			{
				// not present or invalid
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
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance,
			                                      &doubleValue);
			if (S_OK == result && std::fabs(doubleValue) > 0.000001)
			{
				newVideoFormat.hdrMeta.maxDML = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			// MaxCLL MaxFALL
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel, &doubleValue);
			if (S_OK == result && std::fabs(doubleValue) > 0.000001)
			{
				newVideoFormat.hdrMeta.maxCLL = std::lround(doubleValue);
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel,
			                                      &doubleValue);
			if (S_OK == result && std::fabs(doubleValue) > 0.000001)
			{
				newVideoFormat.hdrMeta.maxFALL = std::lround(doubleValue);
			}
			else
			{
				// not present or invalid
			}
			newVideoFormat.hdrMeta.exists = hdrMetaExists(&hdr);

			#ifndef NO_QUILL
			if (newVideoFormat.hdrMeta.exists)
			{
				logHdrMeta(hdr, mVideoFormat.hdrMeta, mLogData);
			}
			#endif
		}

		#ifndef NO_QUILL
		if (!newVideoFormat.hdrMeta.exists && mVideoFormat.hdrMeta.exists)
		{
			LOG_TRACE_L1(mLogData.logger, "[{}] HDR metadata has been removed", mLogData.prefix);
		}
		#endif

		{
			CAutoLock lock(&mFrameSec);
			mVideoFormat = newVideoFormat;
			mVideoFrame = std::make_shared<VideoFrame>(mLogData, newVideoFormat, now, frameTime, frameDuration,
			                                           mCurrentVideoFrameIndex, videoFrame);
		}

		// signal listeners
		if (!SetEvent(mVideoFrameEvent))
		{
			auto err = GetLastError();
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Failed to notify on video frame {:#08x}", mLogData.prefix, err);
			#endif
		}
	}

	if (audioPacket != nullptr)
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
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] Captured audio frame at {}", mLogData.prefix, frameTime);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Failed to get audioFrame bytes {:#08x})", mLogData.prefix,
			            static_cast<unsigned long>(result));
			#endif

			return E_FAIL;
		}

		{
			auto audioByteDepth = audioBitDepth / 8;
			auto audioFrameLength = audioPacket->GetSampleFrameCount() * mDeviceInfo.audioChannelCount * audioByteDepth;

			CAutoLock lock(&mFrameSec);
			mAudioFrame = std::make_shared<AudioFrame>(mLogData, now, frameTime, audioData, audioFrameLength,
			                                           mAudioFormat, ++mCurrentAudioFrameIndex, audioPacket);
		}

		// signal listeners
		if (!SetEvent(mAudioFrameEvent))
		{
			auto err = GetLastError();
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Failed to notify on audio frame {:#08x}", mLogData.prefix, err);
			#endif
		}
	}

	return S_OK;
}

void BlackmagicCaptureFilter::LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal)
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

HRESULT BlackmagicCaptureFilter::Notify(BMDNotifications topic, ULONGLONG param1, ULONGLONG param2)
{
	// only interested in status changes
	if (topic != bmdStatusChanged)
	{
		return S_OK;
	}

	int64_t intVal = -1;
	BOOL boolVal = -1;
	HRESULT hr = S_OK;
	std::string desc{""};
	const BMDDeckLinkStatusID statusId = static_cast<BMDDeckLinkStatusID>(param1);
	switch (statusId)
	{
	case bmdDeckLinkStatusPCIExpressLinkWidth:
		hr = mDeckLinkStatus->GetInt(statusId, &intVal);
		if (intVal != -1) mDeviceInfo.pcieLinkWidth = intVal;
		desc = "PCIe Link Width";
		break;
	case bmdDeckLinkStatusPCIExpressLinkSpeed:
		hr = mDeckLinkStatus->GetInt(statusId, &intVal);
		if (intVal != -1) mDeviceInfo.pcieLinkSpeed = intVal;
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
	#ifndef NO_QUILL
	if (!desc.empty())
	{
		if (hr == S_OK)
		{
			LOG_TRACE_L1(mLogData.logger, "[{}] {} is {}", mLogData.prefix, desc, intVal > -1 ? intVal : boolVal);
		}
		else
		{
			LOG_TRACE_L1(mLogData.logger, "[{}] Failed to read {} {:#08x}", mLogData.prefix, desc,
				static_cast<unsigned long>(hr));
		}
	}
	#endif
	return S_OK;
}

HRESULT BlackmagicCaptureFilter::QueryInterface(const IID& riid, void** ppvObject)
{
	if (riid == _uuidof(IDeckLinkInputCallback))
	{
		return GetInterface(static_cast<IDeckLinkInputCallback*>(this), ppvObject);
	}
	if (riid == _uuidof(IDeckLinkNotificationCallback))
	{
		return GetInterface(static_cast<IDeckLinkNotificationCallback*>(this), ppvObject);
	}

	return HdmiCaptureFilter::NonDelegatingQueryInterface(riid, ppvObject);
}

void BlackmagicCaptureFilter::OnVideoSignalLoaded(VIDEO_SIGNAL* vs)
{
	mVideoInputStatus.inX = vs->cx;
	mVideoInputStatus.inY = vs->cy;
	mVideoInputStatus.inAspectX = vs->aspectX;
	mVideoInputStatus.inAspectY = vs->aspectY;
	mVideoInputStatus.inFps = static_cast<double>(vs->frameDurationScale) / static_cast<double>(vs->frameDuration);
	mVideoInputStatus.inFrameDuration = 10000000 / mVideoInputStatus.inFps;
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

void BlackmagicCaptureFilter::OnAudioSignalLoaded(AUDIO_SIGNAL* as)
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

void BlackmagicCaptureFilter::OnDeviceSelected()
{
	mDeviceStatus.deviceDesc = std::format("{0} [{1}.{2}.{3}]", mDeviceInfo.name, mDeviceInfo.apiVersion[0],
	                                       mDeviceInfo.apiVersion[1], mDeviceInfo.apiVersion[2]);

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Recorded device description: {}", mLogData.prefix, mDeviceStatus.deviceDesc);
	#endif

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mDeviceStatus);
	}
}

HRESULT BlackmagicCaptureFilter::Reload()
{
	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mAudioInputStatus);
		mInfoCallback->Reload(&mAudioOutputStatus);
		mInfoCallback->Reload(&mVideoInputStatus);
		mInfoCallback->Reload(&mVideoOutputStatus);
		mInfoCallback->Reload(&mHdrStatus);
		mInfoCallback->Reload(&mDeviceStatus);
		return S_OK;
	}
	return E_FAIL;
}

HRESULT BlackmagicCaptureFilter::PinThreadCreated()
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

HRESULT BlackmagicCaptureFilter::PinThreadDestroyed()
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


///////////////////////////////////////////////////////////
// BlackmagicVideoCapturePin
///////////////////////////////////////////////////////////
BlackmagicVideoCapturePin::BlackmagicVideoCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview,
                                                     VIDEO_FORMAT pVideoFormat):
	HdmiVideoCapturePin(
		phr,
		pParent,
		pPreview ? "VideoPreview" : "VideoCapture",
		pPreview ? L"Preview" : L"Capture",
		pPreview ? "VideoPreview" : "VideoCapture"
	),
	mSignalledFormat(pVideoFormat.pixelFormat)
{
	mVideoFormat = std::move(pVideoFormat);
	auto search = pixelConverters.find(mVideoFormat.pixelFormat);
	UpdateFrameWriter(search == pixelConverters.end() ? STRAIGHT_THROUGH : search->second, mVideoFormat.pixelFormat);
}

BlackmagicVideoCapturePin::~BlackmagicVideoCapturePin()
{
	mCurrentFrame.reset();
}

HRESULT BlackmagicVideoCapturePin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
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
		if (mStreamStartTime < 0)
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

			#ifndef NO_QUILL
			LogHdrMetaIfPresent(&newVideoFormat);
			#endif

			mHasSignal = true;

			if (ShouldChangeMediaType(&newVideoFormat, IsFallbackActive(&newVideoFormat)))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] VideoFormat changed! Attempting to reconnect", mLogData.prefix);
				#endif

				CMediaType proposedMediaType(m_mt);
				VideoFormatToMediaType(&proposedMediaType, &newVideoFormat);

				auto hr = DoChangeMediaType(&proposedMediaType, &newVideoFormat);
				auto reconnected = SUCCEEDED(hr);
				auto signalledFormat = newVideoFormat.pixelFormat;
				if (reconnected)
				{
					auto search = pixelConverters.find(mVideoFormat.pixelFormat);
					UpdateFrameWriter(search == pixelConverters.end() ? STRAIGHT_THROUGH : search->second,
					                  signalledFormat);
				}
				else
				{
					auto search = pixelFormatFallbacks.find(newVideoFormat.pixelFormat);
					if (search != pixelFormatFallbacks.end())
					{
						auto fallbackPixelFormat = search->second.first;

						#ifndef NO_QUILL
						LOG_WARNING(mLogData.logger,
						            "[{}] VideoFormat changed but not able to reconnect! [Result: {:#08x}] Attempting fallback format {}",
						            mLogData.prefix, static_cast<unsigned long>(hr), fallbackPixelFormat.name);
						#endif

						auto fallbackVideoFormat = std::move(newVideoFormat);
						fallbackVideoFormat.pixelFormat = std::move(fallbackPixelFormat);
						fallbackVideoFormat.CalculateDimensions();

						CMediaType fallbackMediaType(m_mt);
						VideoFormatToMediaType(&fallbackMediaType, &fallbackVideoFormat);

						hr = DoChangeMediaType(&fallbackMediaType, &fallbackVideoFormat);
						reconnected = SUCCEEDED(hr);
						if (reconnected)
						{
							auto strategy = search->second.second;

							#ifndef NO_QUILL
							LOG_WARNING(mLogData.logger,
							            "[{}] VideoFormat changed and fallback format {} required to reconnect, updating frame conversion strategy to {}",
							            mLogData.prefix, fallbackVideoFormat.pixelFormat.name, to_string(strategy));
							#endif

							UpdateFrameWriter(strategy, signalledFormat);
						}
						else
						{
							#ifndef NO_QUILL
							LOG_WARNING(mLogData.logger,
							            "[{}] VideoFormat changed but fallback format also not able to reconnect! Will retry after backoff [Result: {:#08x}]",
							            mLogData.prefix, static_cast<unsigned long>(hr),
							            fallbackVideoFormat.pixelFormat.name);
							#endif
						}
					}
					else
					{
						#ifndef NO_QUILL
						LOG_ERROR(mLogData.logger,
						          "[{}] VideoFormat changed but not able to reconnect! Will retry after backoff [Result: {:#08x}]",
						          mLogData.prefix, static_cast<unsigned long>(hr));
						#endif
					}
				}

				if (!reconnected)
				{
					mCurrentFrame.reset();
					BACKOFF;

					continue;
				}

				mFilter->OnVideoFormatLoaded(&mVideoFormat);
			}

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

			if (!hasFrame)
			{
				mCurrentFrame.reset();
				SHORT_BACKOFF;
			}
		}
	}
	return retVal;
}

HRESULT BlackmagicVideoCapturePin::FillBuffer(IMediaSample* pms)
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

	#ifndef NO_QUILL
	REFERENCE_TIME now;
	mFilter->GetReferenceTime(&now);
	#endif

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	if (mFrameWriter->WriteTo(mCurrentFrame.get(), pms) != S_OK)
	{
		return S_FALSE;
	}

	#ifndef NO_QUILL
	auto execMicros = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	if (mFrameCounter == 1)
	{
		LOG_TRACE_L1(mLogData.logger,
		             "[{}] Captured video frame H|f_idx,cap_lat,conv_lat,ft_0,ft_1,ft_d,ft_o,dur,s_sz,missed",
		             mLogData.prefix);
	}
	auto frameInterval = mCurrentFrameTime - mPreviousFrameTime;
	LOG_TRACE_L1(mLogData.logger, "[{}] Captured video frame D|{},{},{:.3f},{},{},{},{},{},{},{}", mLogData.prefix,
	             mFrameCounter, now - mCurrentFrame->GetCaptureTime(), execMicros,
	             mPreviousFrameTime, mCurrentFrameTime, frameInterval,
	             frameInterval - mCurrentFrame->GetFrameDuration(), mCurrentFrame->GetFrameDuration(),
	             mCurrentFrame->GetLength(), gap!=1);
	#endif

	mCurrentFrame.reset();
	mCurrentFrame = nullptr;

	if (S_FALSE == HandleStreamStateChange(pms))
	{
		retVal = S_FALSE;
	}

	return retVal;
}

HRESULT BlackmagicVideoCapturePin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] BlackmagicVideoCapturePin::OnThreadCreate", mLogData.prefix);
	#endif

	UpdateResolution();

	return mFilter->PinThreadCreated();
}

HRESULT BlackmagicVideoCapturePin::CheckMediaType(const CMediaType* media)
{
	HRESULT hr = HdmiVideoCapturePin::CheckMediaType(media);
	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] CheckMediaType (res: {:#08x}, sz: {})", mLogData.prefix,
	             static_cast<unsigned long>(hr), media->GetSampleSize());
	#endif
	return hr;
}

void BlackmagicVideoCapturePin::DoThreadDestroy()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] BlackmagicVideoCapturePin::DoThreadDestroy", mLogData.prefix);
	#endif

	mFilter->PinThreadDestroyed();
}

void BlackmagicVideoCapturePin::LogHdrMetaIfPresent(VIDEO_FORMAT* newVideoFormat)
{
	#ifndef NO_QUILL
	if (newVideoFormat->hdrMeta.exists && !mVideoFormat.hdrMeta.exists)
	{
		logHdrMeta(newVideoFormat->hdrMeta, mVideoFormat.hdrMeta, mLogData);
	}
	if (!newVideoFormat->hdrMeta.exists && mVideoFormat.hdrMeta.exists)
	{
		LOG_TRACE_L1(mLogData.logger, "[{}] HDR metadata has been removed", mLogData.prefix);
	}
	#endif
}

void BlackmagicVideoCapturePin::OnChangeMediaType()
{
	VideoCapturePin::OnChangeMediaType();

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

///////////////////////////////////////////////////////////
// BlackmagicAudioCapturePin
///////////////////////////////////////////////////////////
BlackmagicAudioCapturePin::BlackmagicAudioCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview):
	HdmiAudioCapturePin(
		phr,
		pParent,
		pPreview ? "AudioPreview" : "AudioCapture",
		pPreview ? L"AudioPreview" : L"AudioCapture",
		pPreview ? "AudioPreview" : "AudioCapture"
	)
{
}

BlackmagicAudioCapturePin::~BlackmagicAudioCapturePin()
= default;

HRESULT BlackmagicAudioCapturePin::GetDeliveryBuffer(IMediaSample** ppSample, REFERENCE_TIME* pStartTime,
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

		if (mStreamStartTime < 0)
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

			retVal = AudioCapturePin::GetDeliveryBuffer(ppSample, pStartTime, pEndTime, dwFlags);
			if (SUCCEEDED(retVal))
			{
				hasFrame = true;
				mFrameCounter++;
				#ifndef NO_QUILL
				LOG_TRACE_L2(mLogData.logger, "[{}] Reading frame {}", mLogData.prefix, mFrameCounter);
				#endif
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

HRESULT BlackmagicAudioCapturePin::OnThreadCreate()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] BlackmagicAudioCapturePin::OnThreadCreate", mLogData.prefix);
	#endif

	return mFilter->PinThreadCreated();
}

HRESULT BlackmagicAudioCapturePin::FillBuffer(IMediaSample* pms)
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
	auto startTime = endTime - static_cast<int64_t>(frameDuration);

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
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(outputSamples+i), shuffled);
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

	#ifndef NO_QUILL
	REFERENCE_TIME now;
	mFilter->GetReferenceTime(&now);
	if (mFrameCounter == 1)
	{
		LOG_TRACE_L1(mLogData.logger, "[{}] Captured audio frame H|f_idx,lat,ft_0,ft_1,ft_d,s_sz,s_ct",
		             mLogData.prefix);
	}
	LOG_TRACE_L1(mLogData.logger, "[{}] Captured audio frame D|{},{},{},{},{},{},{}",
	             mLogData.prefix, codecNames[mAudioFormat.codec],
	             mFrameCounter, now - mCurrentFrameTime, mPreviousFrameTime, mCurrentFrameTime,
	             mCurrentFrameTime - mPreviousFrameTime, bytesCaptured, samplesCaptured);
	#endif

	pms->SetTime(&startTime, &endTime);
	pms->SetSyncPoint(true);
	pms->SetDiscontinuity(false);
	pms->SetActualDataLength(actualSize);

	mPreviousFrameTime = mCurrentFrameTime;
	mCurrentFrameTime = endTime;

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

HRESULT BlackmagicAudioCapturePin::DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat)
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

bool BlackmagicAudioCapturePin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
{
	pProperties->cbBuffer = maxSamplesPerFrame * mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	if (pProperties->cBuffers < 1)
	{
		pProperties->cBuffers = 16;
		return false;
	}
	return true;
}

void BlackmagicAudioCapturePin::DoThreadDestroy()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] BlackmagicAudioCapturePin::DoThreadDestroy", mLogData.prefix);
	#endif

	mFilter->PinThreadDestroyed();
}
