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

#include <functional>
#include <utility>
#include <atlcomcli.h>
#include <map>

#include "capture.h"
#include "bmdomain.h"
#include "VideoFrameWriter.h"

#include "any_rgb.h"
#include "r210_rgb48.h"
#include "StraightThrough.h"
#include "v210_p210.h"
#include "yuv2_yv16.h"

#ifdef NO_QUILL
#include <memory>
#endif

EXTERN_C const GUID CLSID_BMCAPTURE_FILTER;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;
EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

const std::function DeleteString = SysFreeString;

const std::function BSTRToStdString = [](BSTR dl_str) -> std::string
{
	int wlen = ::SysStringLen(dl_str);
	int mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, nullptr, 0, nullptr, nullptr);

	std::string ret_str(mblen, '\0');
	mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, &ret_str[0], mblen, nullptr, nullptr);

	return ret_str;
};

const std::function StdStringToBSTR = [](std::string std_str) -> BSTR
{
	int wlen = MultiByteToWideChar(CP_ACP, 0, std_str.data(), static_cast<int>(std_str.length()), nullptr, 0);

	BSTR ret_str = ::SysAllocStringLen(nullptr, wlen);
	MultiByteToWideChar(CP_ACP, 0, std_str.data(), static_cast<int>(std_str.length()), ret_str, wlen);

	return ret_str;
};

inline bool isInCieRange(double value)
{
	return value >= 0 && value <= 1.1;
}

enum frame_writer_strategy :uint8_t
{
	ANY_RGB,
	YUV2_YV16,
	V210_P210,
	R210_BGR48,
	STRAIGHT_THROUGH
};

inline const char* to_string(frame_writer_strategy e)
{
	switch (e)
	{
	case ANY_RGB: return "ANY_RGB";
	case YUV2_YV16: return "YUV2_YV16";
	case V210_P210: return "V210_P210";
	case R210_BGR48: return "R210_BGR48";
	case STRAIGHT_THROUGH: return "STRAIGHT_THROUGH";
	default: return "unknown";
	}
}

typedef std::map<pixel_format, std::pair<pixel_format, frame_writer_strategy>> pixel_format_fallbacks;
typedef std::map<pixel_format, frame_writer_strategy> pixel_conversion_strategies;
const pixel_conversion_strategies pixelConverters{
	{RGBA, STRAIGHT_THROUGH},
};
const pixel_format_fallbacks pixelFormatFallbacks{
	// standard consumer formats
	{YUV2, {YV16, YUV2_YV16}},
	{V210, {P210, V210_P210}},   // supported natively by madvr
	{R210, {RGB48, R210_BGR48}}, // supported natively by jrvr >= MC34
	// unlikely to be seen in the wild so just fallback to RGB using decklink sdk
	{AY10, {RGBA, ANY_RGB}},
	{R12B, {RGBA, ANY_RGB}},
	{R12L, {RGBA, ANY_RGB}},
	{R10B, {RGBA, ANY_RGB}},
	{R10L, {RGBA, ANY_RGB}},
};

constexpr int64_t invalidFrameTime = std::numeric_limits<int64_t>::lowest();
constexpr BMDAudioSampleType audioBitDepth = bmdAudioSampleType16bitInteger;

class BMReferenceClock final :
	public CBaseReferenceClock
{
public:
	BMReferenceClock(HRESULT* phr, const CComQIPtr<IDeckLinkInput>& pInput)
		: CBaseReferenceClock(L"BMReferenceClock", nullptr, phr, nullptr),
		  mInput(pInput)
	{
	}

	REFERENCE_TIME GetPrivateTime() override
	{
		BMDTimeValue tv, tif, tpf;
		mInput->GetHardwareReferenceClock(dshowTicksPerSecond, &tv, &tif, &tpf);
		return tv;
	}

private:
	CComQIPtr<IDeckLinkInput> mInput;
};

/**
 * DirectShow filter which can uses the Blackmagic SDK to receive video and audio from a connected HDMI capture card.
 * Can inject HDR/WCG data if found on the incoming HDMI stream.
 */
class BlackmagicCaptureFilter final :
	public HdmiCaptureFilter<DEVICE_INFO, VIDEO_SIGNAL, AUDIO_SIGNAL>,
	public IDeckLinkInputCallback,
	public IDeckLinkNotificationCallback
{
public:
	// Provide the way for COM to create a Filter object
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

	// Callbacks to update the prop page data
	void OnVideoSignalLoaded(VIDEO_SIGNAL* vs) override;
	void OnAudioSignalLoaded(AUDIO_SIGNAL* as) override;
	void OnDeviceUpdated() override;

	HRESULT Reload() override;

	// filter <-> pin communication
	HRESULT PinThreadCreated();
	HRESULT PinThreadDestroyed();

	//////////////////////////////////////////////////////////////////////////
	//  IDeckLinkInputCallback
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents,
	                                                  IDeckLinkDisplayMode* newDisplayMode,
	                                                  BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;

	HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
	                                                 IDeckLinkAudioInputPacket* audioPacket) override;

	//////////////////////////////////////////////////////////////////////////
	//  IDeckLinkNotificationCallback
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE Notify(BMDNotifications topic, ULONGLONG param1, ULONGLONG param2);

	//////////////////////////////////////////////////////////////////////////
	//  IUnknown
	//////////////////////////////////////////////////////////////////////////
	HRESULT QueryInterface(const IID& riid, void** ppvObject) override;

	ULONG AddRef() override
	{
		return CaptureFilter::AddRef();
	}

	ULONG Release() override
	{
		return CaptureFilter::Release();
	}

	HANDLE GetVideoFrameHandle() const
	{
		return mVideoFrameEvent;
	}

	std::shared_ptr<VideoFrame> GetVideoFrame()
	{
		return mVideoFrame;
	}

	HANDLE GetAudioFrameHandle() const
	{
		return mAudioFrameEvent;
	}

	std::shared_ptr<AudioFrame> GetAudioFrame()
	{
		return mAudioFrame;
	}

protected:
	static void LoadFormat(VIDEO_FORMAT* videoFormat, const VIDEO_SIGNAL* videoSignal);
	static void LoadSignalFromDisplayMode(VIDEO_SIGNAL* newSignal, IDeckLinkDisplayMode* newDisplayMode);
	static void LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal);

private:
	// Constructor
	BlackmagicCaptureFilter(LPUNKNOWN punk, HRESULT* phr);
	~BlackmagicCaptureFilter() override;

	CComPtr<IDeckLink> mDeckLink;
	CComQIPtr<IDeckLinkInput> mDeckLinkInput;
	CComQIPtr<IDeckLinkNotification> mDeckLinkNotification;
	CComQIPtr<IDeckLinkStatus> mDeckLinkStatus;
	CComQIPtr<IDeckLinkHDMIInputEDID> mDeckLinkHDMIInputEDID;
	CCritSec mFrameSec;
	CCritSec mDeckLinkSec;

	uint8_t mRunningPins{0};
	VIDEO_SIGNAL mVideoSignal{};
	VIDEO_FORMAT mVideoFormat{};

	int64_t mPreviousVideoFrameTime{invalidFrameTime};
	uint64_t mCurrentVideoFrameIndex{0};
	std::shared_ptr<VideoFrame> mVideoFrame;
	HANDLE mVideoFrameEvent;

	AUDIO_SIGNAL mAudioSignal{};
	AUDIO_FORMAT mAudioFormat{};
	uint64_t mCurrentAudioFrameIndex{0};
	std::shared_ptr<AudioFrame> mAudioFrame;
	HANDLE mAudioFrameEvent;
};


/**
 * A video stream flowing from the capture device to an output pin.
 */
class BlackmagicVideoCapturePin final :
	public HdmiVideoCapturePin<BlackmagicCaptureFilter>
{
public:
	BlackmagicVideoCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview, VIDEO_FORMAT pVideoFormat);
	~BlackmagicVideoCapturePin() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
	                          __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT FillBuffer(IMediaSample* pms) override;
	HRESULT OnThreadCreate(void) override;
	HRESULT CheckMediaType(const CMediaType*) override;

protected:
	void DoThreadDestroy() override;

	void LogHdrMetaIfPresent(VIDEO_FORMAT* newVideoFormat);
	void OnChangeMediaType() override;

	std::shared_ptr<VideoFrame> mCurrentFrame;

private:
	boolean IsFallbackActive(const VIDEO_FORMAT* newVideoFormat) const
	{
		if (newVideoFormat->pixelFormat.format != mVideoFormat.pixelFormat.format)
		{
			auto search = pixelFormatFallbacks.find(newVideoFormat->pixelFormat);
			if (search != pixelFormatFallbacks.end())
			{
				auto fallbackPixelFormat = search->second.first;
				if (fallbackPixelFormat.format == mVideoFormat.pixelFormat.format)
				{
					if (newVideoFormat->cx == mVideoFormat.cx && newVideoFormat->cy == mVideoFormat.cy)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	void UpdateFrameWriter(frame_writer_strategy strategy, const pixel_format& signalledFormat)
	{
		mSignalledFormat = signalledFormat;
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] Updating conversion strategy from {} to {}", mLogData.prefix,
		             to_string(mFrameWriterStrategy), to_string(strategy));
		#endif

		mFrameWriterStrategy = strategy;
		switch (mFrameWriterStrategy)
		{
		case ANY_RGB:
			mFrameWriter = std::make_unique<any_rgb>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case YUV2_YV16:
			mFrameWriter = std::make_unique<yuv2_yv16>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case V210_P210:
			mFrameWriter = std::make_unique<v210_p210>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case R210_BGR48:
			mFrameWriter = std::make_unique<r210_rgb48>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case STRAIGHT_THROUGH:
			mFrameWriter = std::make_unique<StraightThrough>(mLogData, mVideoFormat.cx, mVideoFormat.cy,
			                                                 &mVideoFormat.pixelFormat);
		}
	}

	frame_writer_strategy mFrameWriterStrategy;
	std::unique_ptr<IVideoFrameWriter> mFrameWriter;
	pixel_format mSignalledFormat;
};

/**
 * An audio stream flowing from the capture device to an output pin.
 */
class BlackmagicAudioCapturePin final :
	public HdmiAudioCapturePin<BlackmagicCaptureFilter>
{
public:
	BlackmagicAudioCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview);
	~BlackmagicAudioCapturePin() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
	                          __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT OnThreadCreate() override;
	HRESULT FillBuffer(IMediaSample* pms) override;

protected:
	HRESULT DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat);
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
	void DoThreadDestroy() override;

	std::shared_ptr<AudioFrame> mCurrentFrame;
};
