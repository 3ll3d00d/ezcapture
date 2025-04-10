/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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

#define NOMINMAX

#include <windows.h>
#include <process.h>
#include <DXVA.h>
#include <filesystem>
#include <utility>
#include "bmcapture.h"

#include <initguid.h>

#include <cmath>
// std::reverse
#include <algorithm>

#ifndef NO_QUILL
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"
#include <string_view>
#include <utility>
#include "quill/std/WideString.h"
#endif // !NO_QUILL

// audio is limited to 48kHz and an audio packet is only delivered with a video frame
// lowest fps is 23.976 so the max no of samples should be 48000/(24000/1001) = 2002
constexpr uint16_t maxSamplesPerFrame = 2048;

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
	videoFormat->frameInterval = videoSignal->frameDuration;
	auto fourccIdx = 0;
	switch (videoSignal->pixelFormat)
	{
	case bmdFormat8BitYUV:
		videoFormat->bitDepth = 8;
		videoFormat->pixelEncoding = YUV_422;
		break;
	case bmdFormat10BitYUV:
		videoFormat->bitDepth = 10;
		videoFormat->pixelEncoding = YUV_422;
		fourccIdx = 1;
		break;
	case bmdFormat10BitYUVA:
		videoFormat->bitDepth = 10;
		videoFormat->pixelEncoding = YUV_422;
		fourccIdx = 1;
		break;
	case bmdFormat8BitARGB:
		videoFormat->bitDepth = 8;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormat8BitBGRA:
		videoFormat->bitDepth = 8;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormat10BitRGB:
		videoFormat->bitDepth = 10;
		videoFormat->pixelEncoding = RGB_444;
		fourccIdx = -1;
		break;
	case bmdFormat12BitRGB:
		videoFormat->bitDepth = 12;
		fourccIdx = 2;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormat12BitRGBLE:
		videoFormat->bitDepth = 12;
		fourccIdx = 2;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormat10BitRGBXLE:
		videoFormat->bitDepth = 10;
		fourccIdx = 1;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormat10BitRGBX:
		videoFormat->bitDepth = 10;
		fourccIdx = 1;
		videoFormat->pixelEncoding = RGB_444;
		break;
	case bmdFormatUnspecified:
	case bmdFormatH265:
	case bmdFormatDNxHR:
		// unsupported
		videoFormat->bitDepth = 0;
		break;
	}
	if (fourccIdx != -1)
	{
		videoFormat->pixelStructure = fourcc[fourccIdx][videoFormat->pixelEncoding];
		videoFormat->pixelStructureName = fourccName[fourccIdx][videoFormat->pixelEncoding];
		videoFormat->bitCount = fourccBitCount[fourccIdx][videoFormat->pixelEncoding];
	}
	GetImageDimensions(videoFormat->pixelStructure, videoFormat->cx, videoFormat->cy, &videoFormat->lineLength,
	                   &videoFormat->imageSize);
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

			continue;
		}
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Found device at index {} : {}", mLogData.prefix, idx, deviceName);
		#endif

		int64_t duplexMode;
		if (deckLinkAttributes->GetInt(BMDDeckLinkDuplex, &duplexMode) == S_OK && duplexMode == bmdDuplexInactive)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "Ignoring device {} {}, no active connectors for current profile", idx,
			          deviceName);
			#endif

			continue;
		}

		int64_t videoIOSupport;
		result = deckLinkAttributes->GetInt(BMDDeckLinkVideoIOSupport, &videoIOSupport);
		if (result != S_OK)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger,
			          "Ignoring device {} {}, could not get BMDDeckLinkVideoIOSupport attribute ({:#08x})", idx,
			          deviceName, result);
			#endif

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
				LOG_WARNING(mLogData.logger, "Device {} {} does not support audio capture", idx, deviceName);
				#endif

				audioChannelCount = 0;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &inputFormatDetection);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "Ignoring device {} {} does not support input format detection", idx,
				            deviceName);
				#endif

				inputFormatDetection = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsHDRMetadata, &hdrMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "Device {} {} does not support HDR metadata", idx, deviceName);
				#endif

				hdrMetadata = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsColorspaceMetadata, &colourspaceMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "Device {} {} does not support colourspace metadata", idx, deviceName);
				#endif

				colourspaceMetadata = false;
			}

			result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportedDynamicRange, &dynamicRangeMetadata);
			if (result != S_OK)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "Device {} {} does not support dynamic range metadata", idx, deviceName);
				#endif

				dynamicRangeMetadata = false;
			}

			if (!inputFormatDetection)
			{
				continue;
			}

			if (mDeckLink)
			{
				deckLink->Release();
			}
			else
			{
				#ifndef NO_QUILL
				LOG_INFO(mLogData.logger, "[{}] Filter will use device {}", mLogData.prefix, deviceName);
				LOG_INFO(mLogData.logger, "[{}] Input Format Detection? {}", mLogData.prefix, inputFormatDetection);
				LOG_INFO(mLogData.logger, "[{}] Can Detect HDR Metadata? {}", mLogData.prefix, hdrMetadata);
				LOG_INFO(mLogData.logger, "[{}] Can Detect Colour space Metadata? {}", mLogData.prefix,
				         colourspaceMetadata);
				LOG_INFO(mLogData.logger, "[{}] Can Detect Dynamic Range Metadata? {}", mLogData.prefix,
				         dynamicRangeMetadata);
				LOG_INFO(mLogData.logger, "[{}] Audio Channels? {}", mLogData.prefix, audioChannelCount);
				#endif

				mDeckLink = deckLink;
				mDeckLinkInput = deckLink;
				mDeckLinkNotification = deckLink;
				mDeckLinkStatus = deckLink;
				mDeckLinkHDMIInputEDID = deckLink;

				mDeviceInfo.name = deviceName;
				mDeviceInfo.audioChannelCount = audioChannelCount;
				mDeviceInfo.inputFormatDetection = inputFormatDetection;
				mDeviceInfo.hdrMetadata = hdrMetadata;
				mDeviceInfo.colourspaceMetadata = colourspaceMetadata;
				mDeviceInfo.dynamicRangeMetadata = dynamicRangeMetadata;

				const LONGLONG all = bmdDynamicRangeSDR | bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG;
				result = mDeckLinkHDMIInputEDID->SetInt(bmdDeckLinkHDMIInputEDIDDynamicRange, all);
				if (SUCCEEDED(result))
				{
					result = mDeckLinkHDMIInputEDID->WriteToEDID();
					if (!SUCCEEDED(result))
					{
						#ifndef NO_QUILL
						LOG_ERROR(mLogData.logger, "[{}] Unable to WriteToEDID [{:#08x}]", mLogData.prefix, result);
						#endif
					}
				}
				else
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger, "[{}] Unable to set dynamic range flags [{:#08x}]", mLogData.prefix,
					          result);
					#endif
				}

				result = mDeckLinkNotification->Subscribe(bmdStatusChanged, this);
				if (!SUCCEEDED(result))
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger, "[{}] Unable to subscribe for status notifications [{:#08x}]",
					          mLogData.prefix, result);
					#endif
				}

				result = mDeckLinkInput->EnableVideoInput(bmdMode4K2160p2398, bmdFormat8BitYUV,
				                                          bmdVideoInputEnableFormatDetection);
				if (!SUCCEEDED(result))
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger, "[{}] Unable to EnableVideoInput [{:#08x}]", mLogData.prefix, result);
					#endif
				}
				if (mDeviceInfo.audioChannelCount > 0)
				{
					result = mDeckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, audioBitDepth,
					                                          mDeviceInfo.audioChannelCount);
					// NOLINT(clang-diagnostic-shorten-64-to-32) values will only be 0/2/8/16
					if (!SUCCEEDED(result))
					{
						#ifndef NO_QUILL
						LOG_ERROR(mLogData.logger, "[{}] Unable to EnableAudioInput [{:#08x}]", mLogData.prefix,
						          result);
						#endif
					}
				}

				result = mDeckLinkInput->SetCallback(this);
				if (!SUCCEEDED(result))
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger, "[{}] Unable to SetCallback [{:#08x}]", mLogData.prefix, result);
					#endif
				}
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
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] No valid devices found", mLogData.prefix);
		#endif
	}

	result = mDeckLinkFrameConverter.CoCreateInstance(CLSID_CDeckLinkVideoConversion, nullptr);
	if (SUCCEEDED(result))
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Created Frame Converter", mLogData.prefix);
		#endif
	}
	else
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Failed to create Frame Converter {:#08x}", mLogData.prefix, result);
		#endif
	}
	mClock = new BMReferenceClock(phr, mDeckLinkInput);

	// video pin must have a default format in order to ensure a renderer is present in the graph
	LoadFormat(&mVideoFormat, &mVideoSignal);

	new BlackmagicVideoCapturePin(phr, this, false, mVideoFormat);
	new BlackmagicVideoCapturePin(phr, this, true, mVideoFormat);

	if (mDeviceInfo.audioChannelCount > 0)
	{
		new BlackmagicAudioCapturePin(phr, this, false);
		new BlackmagicAudioCapturePin(phr, this, true);
	}
}

BlackmagicCaptureFilter::~BlackmagicCaptureFilter()
{
	if (mDeckLinkNotification)
	{
		mDeckLinkNotification->Unsubscribe(bmdStatusChanged, this);
	}
	CloseHandle(mVideoFrameEvent);
}

HRESULT BlackmagicCaptureFilter::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents,
                                                         IDeckLinkDisplayMode* newDisplayMode,
                                                         BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	VIDEO_SIGNAL newSignal{};
	if (notificationEvents & bmdVideoInputColorspaceChanged)
	{
		if (detectedSignalFlags & bmdDetectedVideoInputYCbCr422)
		{
			newSignal.colourFormat = "YCbCr";
			if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
			{
				newSignal.pixelFormat = bmdFormat8BitYUV;
				newSignal.bitDepth = 8;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
			{
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
			newSignal.colourFormat = "RGB";
			if (detectedSignalFlags & bmdDetectedVideoInput8BitDepth)
			{
				newSignal.pixelFormat = bmdFormat8BitARGB;
				newSignal.bitDepth = 8;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput10BitDepth)
			{
				newSignal.pixelFormat = bmdFormat10BitRGB;
				newSignal.bitDepth = 10;
			}
			else if (detectedSignalFlags & bmdDetectedVideoInput12BitDepth)
			{
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
		BSTR displayModeStr = nullptr;
		if (newDisplayMode->GetName(&displayModeStr) == S_OK)
		{
			newSignal.displayModeName = BSTRToStdString(displayModeStr);
			DeleteString(displayModeStr);

			BMDTimeValue frameDuration;
			BMDTimeScale frameDurationScale;
			newDisplayMode->GetFrameRate(&frameDuration, &frameDurationScale);

			newSignal.frameDuration = static_cast<uint32_t>(frameDuration);
			newSignal.frameDurationScale = static_cast<uint16_t>(frameDurationScale);
			newSignal.cx = newDisplayMode->GetWidth(); // NOLINT(clang-diagnostic-implicit-int-conversion)
			newSignal.cy = newDisplayMode->GetHeight(); // NOLINT(clang-diagnostic-implicit-int-conversion)
		}
	}

	if (notificationEvents & (bmdVideoInputDisplayModeChanged | bmdVideoInputColorspaceChanged))
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Restarting video capture on input format change", mLogData.prefix);
		#endif

		{
			CAutoLock lck(&mDeckLinkSec);

			// Pause the stream, update the capture format, flush and start streams
			auto result = mDeckLinkInput->PauseStreams();
			if (!SUCCEEDED(result))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Failed to pause streams on input format change ({:#08x})",
				            mLogData.prefix, result);
				#endif
			}

			result = mDeckLinkInput->EnableVideoInput(newDisplayMode->GetDisplayMode(), newSignal.pixelFormat,
			                                          bmdVideoInputEnableFormatDetection);
			if (SUCCEEDED(result))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger,
				            "[{}] Enabled video input on input format change (displayMode: {:#08x} pixelFormat: {:#08x})",
				            mLogData.prefix, static_cast<int>(newDisplayMode->GetDisplayMode()),
				            static_cast<int>(newSignal.pixelFormat));
				#endif
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Failed to enable video input on input format change ({:#08x})",
				            mLogData.prefix, result);
				#endif
			}

			result = mDeckLinkInput->FlushStreams();
			if (!SUCCEEDED(result))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Failed to flush streams on input format change ({:#08x})",
				            mLogData.prefix, result);
				#endif
			}

			result = mDeckLinkInput->StartStreams();
			if (!SUCCEEDED(result))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Failed to start streams on input format change ({:#08x})",
				            mLogData.prefix, result);
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
		OnVideoSignalLoaded(&mVideoSignal);
	}

	return S_OK;
}

HRESULT BlackmagicCaptureFilter::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
                                                        IDeckLinkAudioInputPacket* audioPacket)
{
	if (videoFrame)
	{
		IDeckLinkVideoFrame* frameToProcess = videoFrame;

		int64_t frameTime = 0;
		auto result = videoFrame->GetHardwareReferenceTimestamp(dshowTicksPerSecond, &frameTime, nullptr);
		if (!SUCCEEDED(result))
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Discarding video frame, unable to get reference tiemstamp {:#08x}",
			          mLogData.prefix, result);
			#endif

			return E_FAIL;
		}

		if ((videoFrame->GetFlags() & bmdFrameHasNoInputSource) != 0)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Signal is not locked at {}", mLogData.prefix, frameTime);
			#endif

			return E_FAIL;
		}

		VIDEO_FORMAT newVideoFormat{};
		LoadFormat(&newVideoFormat, &mVideoSignal);

		IDeckLinkVideoFrame* convertedFrame;
		result = mDeckLinkFrameConverter->ConvertNewFrame(videoFrame, bmdFormat8BitBGRA, bmdColorspaceUnknown, nullptr,
		                                                  &convertedFrame);
		if (SUCCEEDED(result))
		{
			#ifndef NO_QUILL
			LOG_TRACE_L3(mLogData.logger, "[{}] Converted frame to BGRA", mLogData.prefix);
			#endif

			newVideoFormat.pixelEncoding = RGB_444;
			newVideoFormat.bitCount = BITS_RGBA;
			newVideoFormat.pixelStructure = FOURCC_RGBA;
			newVideoFormat.pixelStructureName = "RGBA";
			GetImageDimensions(newVideoFormat.pixelStructure, newVideoFormat.cx, newVideoFormat.cy,
			                   &newVideoFormat.lineLength, &newVideoFormat.imageSize);

			frameToProcess = convertedFrame;
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Failed to convert frame to BGRA {:#08x}", mLogData.prefix, result);
			#endif
		}

		// detect discontinuity
		if (mPreviousVideoFrameTime != invalidFrameTime)
		{
			const auto ticksPerFrame = std::round((1.0 / newVideoFormat.fps) * dshowTicksPerSecond);
			const auto framesSinceLast = static_cast<int>(round(
				static_cast<double>(frameTime - mPreviousVideoFrameTime) / ticksPerFrame));

			mCapturedVideoFrameCount += framesSinceLast;
			#ifndef NO_QUILL
			if (auto missedFrames = std::max((framesSinceLast - 1), 0))
			{
				LOG_WARNING(mLogData.logger, "[{}] Video capture discontinuity detected, {} frames missed at frame {}",
				            mLogData.prefix, missedFrames, mCapturedVideoFrameCount);
			}
			#endif
		}
		else
		{
			mCapturedVideoFrameCount++;
		}
		mPreviousVideoFrameTime = frameTime;

		// metadata
		auto doubleValue = 0.0;
		auto intValue = 0LL;
		CComQIPtr<IDeckLinkVideoFrameMetadataExtensions> metadataExtensions(videoFrame);

		result = metadataExtensions->GetInt(bmdDeckLinkFrameMetadataColorspace, &intValue);
		if (SUCCEEDED(result))
		{
			switch (static_cast<BMDColorspace>(intValue))
			{
			case bmdColorspaceRec601:
				newVideoFormat.colourFormat = YUV601;
				break;
			case bmdColorspaceRec709:
				newVideoFormat.colourFormat = YUV709;
				break;
			case bmdColorspaceRec2020:
				newVideoFormat.colourFormat = YUV2020;
				break;
			case bmdColorspaceP3D65:
				newVideoFormat.colourFormat = P3D65;
				break;
			case bmdColorspaceDolbyVisionNative:
				newVideoFormat.colourFormat = COLOUR_FORMAT_UNKNOWN;
				break;
			case bmdColorspaceUnknown:
				newVideoFormat.colourFormat = COLOUR_FORMAT_UNKNOWN;
				break;
			}
		}

		// HDR meta data
		HDR_META hdr = newVideoFormat.hdrMeta;
		result = metadataExtensions->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &intValue);
		if (SUCCEEDED(result))
		{
			// EOTF in range 0-7 as per CEA 861.3 aka A2016 HDR STATIC METADATA EXTENSIONS.
			// 0=SDR, 1=HDR, 2=PQ, 3=HLG, 4-7=future use
			switch (intValue)
			{
			case 0:
				hdr.transferFunction = 4;
				break;
			case 2:
				hdr.transferFunction = 15;
				break;
			default:
				hdr.transferFunction = 4;
			}
		}
		else
		{
			if (newVideoFormat.colourFormat == YUV2020)
			{
				hdr.transferFunction = 15;
			}
		}

		if (videoFrame->GetFlags() & bmdFrameContainsHDRMetadata)
		{
			// Primaries
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.b_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.b_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.r_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.r_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.g_primary_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.g_primary_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			// White point
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointX, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.whitepoint_x = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointY, &doubleValue);
			if (SUCCEEDED(result) && isInCieRange(doubleValue))
			{
				hdr.whitepoint_y = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			// DML
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance,
			                                      &doubleValue);
			if (SUCCEEDED(result) && std::fabs(doubleValue) > 0.000001)
			{
				hdr.minDML = doubleValue;
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance,
			                                      &doubleValue);
			if (SUCCEEDED(result) && std::fabs(doubleValue) > 0.000001)
			{
				hdr.maxDML = doubleValue;
			}
			else
			{
				// not present or invalid
			}

			// MaxCLL MaxFALL
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel, &doubleValue);
			if (SUCCEEDED(result) && std::fabs(doubleValue) > 0.000001)
			{
				hdr.maxCLL = std::lround(doubleValue);
			}
			else
			{
				// not present or invalid
			}
			result = metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel,
			                                      &doubleValue);
			if (SUCCEEDED(result) && std::fabs(doubleValue) > 0.000001)
			{
				hdr.maxFALL = std::lround(doubleValue);
			}
			else
			{
				// not present or invalid
			}
			hdr.exists = hdrMetaExists(&hdr);

			#ifndef NO_QUILL
			if (hdr.exists)
			{
				logHdrMeta(hdr, mVideoFormat.hdrMeta, mLogData);
			}
			#endif
		}

		#ifndef NO_QUILL
		if (!hdr.exists && mVideoFormat.hdrMeta.exists)
		{
			LOG_TRACE_L1(mLogData.logger, "[{}] HDR metadata has been removed", mLogData.prefix);
		}
		#endif

		{
			CAutoLock lock(&mFrameSec);
			mVideoFormat = newVideoFormat;
			CComQIPtr<IDeckLinkVideoBuffer> buf(frameToProcess);
			mVideoFrame = std::make_shared<VideoFrame>(newVideoFormat, frameTime, frameToProcess->GetRowBytes(),
			                                           mCapturedVideoFrameCount, buf);
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
		audioPacket->GetBytes(&audioData);

		BMDTimeValue frameTime;
		audioPacket->GetPacketTime(&frameTime, dshowTicksPerSecond);

		AUDIO_FORMAT newAudioFormat{};
		newAudioFormat.inputChannelCount = mDeviceInfo.audioChannelCount;
		newAudioFormat.outputChannelCount = mDeviceInfo.audioChannelCount;

		{
			auto audioFrameLength = audioPacket->GetSampleFrameCount() * mDeviceInfo.audioChannelCount * (audioBitDepth
				/ 8);

			CAutoLock lock(&mFrameSec);
			mAudioFrame = std::make_shared<AudioFrame>(frameTime, audioData, audioFrameLength, newAudioFormat);
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

HRESULT BlackmagicCaptureFilter::Notify(BMDNotifications topic, ULONGLONG param1, ULONGLONG param2)
{
	// only interested in status changes
	if (topic != bmdStatusChanged)
	{
		return S_OK;
	}

	switch (BMDDeckLinkStatusID statusId = static_cast<BMDDeckLinkStatusID>(param1))
	{
	case bmdDeckLinkStatusPCIExpressLinkWidth:
		// TODO update device
		// result = deckLinkStatus->GetInt(statusId, &intVal);
		break;
	case bmdDeckLinkStatusPCIExpressLinkSpeed:
		// TODO update device
		// result = deckLinkStatus->GetInt(statusId, &intVal);
		break;
	case bmdDeckLinkStatusDeviceTemperature:
		// TODO update device
		// result = deckLinkStatus->GetInt(statusId, &intVal);
		break;
	case bmdDeckLinkStatusVideoInputSignalLocked:
		// TODO update signal
		// result = deckLinkStatus->GetFlag(statusId, &boolVal);
		break;
	default:
		break;
	}
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
	mVideoInputStatus.inBitDepth = vs->bitDepth;
	// TODO No Signal | Unsupported Signal | Locking | Locked
	mVideoInputStatus.signalStatus = vs->displayModeName;
	mVideoInputStatus.inColourFormat = vs->colourFormat;
	mVideoInputStatus.inQuantisation = "?";
	mVideoInputStatus.inSaturation = "?";
	mVideoInputStatus.inPixelLayout = vs->colourFormat;
	mVideoInputStatus.validSignal = true;

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

		result = mDeckLinkInput->StartStreams();
		if (SUCCEEDED(result))
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Input streams started successfully", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to start input streams (result {:#08x})", mLogData.prefix,
			            result);
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
		if (SUCCEEDED(result))
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Input streams stopped successfully", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to stop input streams (result {:#08x})", mLogData.prefix, result);
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
	)
{
	mVideoFormat = pVideoFormat;
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
			mCurrentFrame = mFilter->GetVideoFrame();
			auto newVideoFormat = mCurrentFrame->GetVideoFormat();
			hasFrame = true;

			#ifndef NO_QUILL
			LogHdrMetaIfPresent(&newVideoFormat);
			#endif

			if (ShouldChangeMediaType(&newVideoFormat))
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] VideoFormat changed! Attempting to reconnect", mLogData.prefix);
				#endif

				CMediaType proposedMediaType(m_mt);
				VideoFormatToMediaType(&proposedMediaType, &newVideoFormat);

				auto hr = DoChangeMediaType(&proposedMediaType, &newVideoFormat);

				if (FAILED(hr))
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger,
					          "[{}] VideoFormat changed but not able to reconnect! retry after backoff [Result: {:#08x}]",
					          mLogData.prefix, hr);
					#endif

					mCurrentFrame.reset();
					// TODO show OSD to say we need to change
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

	auto endTime = mCurrentFrame->GetFrameTime() - mStreamStartTime;
	auto startTime = endTime - mVideoFormat.frameInterval;
	pms->SetTime(&startTime, &endTime);
	pms->SetSyncPoint(TRUE);
	auto gap = mCurrentFrame->GetFrameIndex() - mFrameCounter;
	pms->SetDiscontinuity(gap != 1);

	BYTE* out = nullptr;
	auto hr = pms->GetPointer(&out);
	if (FAILED(hr))
		return hr;
	if (mCurrentFrame->GetLength() > pms->GetSize())
		return S_FALSE;

	memcpy(out, mCurrentFrame->GetData(), mCurrentFrame->GetLength());

	mFrameCounter = mCurrentFrame->GetFrameIndex();
	mCurrentFrame.reset();

	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] Captured video frame {} at {}", mLogData.prefix,
	             mFrameCounter, endTime);
	#endif

	if (mSendMediaType)
	{
		CMediaType cmt(m_mt);
		AM_MEDIA_TYPE* sendMediaType = CreateMediaType(&cmt);
		pms->SetMediaType(sendMediaType);
		DeleteMediaType(sendMediaType);
		mSendMediaType = FALSE;
	}
	AppendHdrSideDataIfNecessary(pms, endTime);

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

	return mFilter->PinThreadCreated();
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
	mFilter->NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(mVideoFormat.cx, mVideoFormat.cy), 0);
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
		auto frameCopied = false;
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
					            mLogData.prefix, hr);
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
	memcpy(pmsData, mCurrentFrame->GetData(), actualSize);

	auto endTime = mCurrentFrame->GetFrameTime();
	auto sampleLength = mAudioFormat.bitDepthInBytes * mAudioFormat.outputChannelCount;
	auto sampleCount = size / sampleLength;
	auto frameDuration = mAudioFormat.sampleInterval * sampleCount;
	auto startTime = endTime - static_cast<int64_t>(frameDuration);

	mCurrentFrame.reset();

	#ifndef NO_QUILL
	LOG_TRACE_L2(mLogData.logger, "[{}] Audio frame {} : time {} size {} bytes",
	             mLogData.prefix, mFrameCounter, endTime, actualSize);
	#endif

	pms->SetTime(&startTime, &endTime);
	pms->SetSyncPoint(true);
	pms->SetDiscontinuity(false);
	pms->SetActualDataLength(actualSize);
	if (mSendMediaType)
	{
		CMediaType cmt(m_mt);
		AM_MEDIA_TYPE* sendMediaType = CreateMediaType(&cmt);
		pms->SetMediaType(sendMediaType);
		DeleteMediaType(sendMediaType);
		mSendMediaType = FALSE;
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
