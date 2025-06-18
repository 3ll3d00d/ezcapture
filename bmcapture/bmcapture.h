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
#ifndef BMCAPTURE_HEADER
#define BMCAPTURE_HEADER

#define NOMINMAX // quill does not compile without this
#define WIN32_LEAN_AND_MEAN

#include "capture.h"
#include "bmdomain.h"
#include "VideoFrameWriter.h"

#include "any_rgb.h"
#include "StraightThrough.h"

#include <functional>

#ifdef NO_QUILL
#include <memory>
#endif

EXTERN_C const GUID CLSID_BMCAPTURE_FILTER;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;
EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

const std::function DeleteString = SysFreeString;

const std::function BSTRToStdString = [](const BSTR dl_str) -> std::string
{
	int wlen = ::SysStringLen(dl_str);
	int mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, nullptr, 0, nullptr, nullptr);

	std::string ret_str(mblen, '\0');
	mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, ret_str.data(), mblen, nullptr, nullptr);

	return ret_str;
};

const std::function StdStringToBSTR = [](const std::string& std_str) -> BSTR
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

constexpr int64_t invalidFrameTime = std::numeric_limits<int64_t>::lowest();
constexpr BMDAudioSampleType audioBitDepth = bmdAudioSampleType16bitInteger;

class BMReferenceClock final :
	public CBaseReferenceClock
{
public:
	BMReferenceClock(HRESULT* phr) : CBaseReferenceClock(L"BMReferenceClock", nullptr, phr, nullptr)
	{
	}

	REFERENCE_TIME GetPrivateTime() override
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now().time_since_epoch()).count() * 10;
	}
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

	HRESULT processVideoFrame(IDeckLinkVideoInputFrame* videoFrame);

	HRESULT processAudioPacket(IDeckLinkAudioInputPacket* audioPacket, const REFERENCE_TIME& now);

protected:
	static void LoadFormat(VIDEO_FORMAT* videoFormat, const VIDEO_SIGNAL* videoSignal);
	static void LoadSignalFromDisplayMode(VIDEO_SIGNAL* newSignal, IDeckLinkDisplayMode* newDisplayMode);
	static void LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal);

	STDMETHODIMP Run(REFERENCE_TIME tStart) override;

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
	int64_t mVideoFrameTime{0};
	uint64_t mCurrentVideoFrameIndex{0};
	std::shared_ptr<VideoFrame> mVideoFrame;
	HANDLE mVideoFrameEvent;

	AUDIO_SIGNAL mAudioSignal{};
	AUDIO_FORMAT mAudioFormat{};
	int64_t mPreviousAudioFrameTime{invalidFrameTime};
	int64_t mAudioFrameTime{0};
	uint64_t mCurrentAudioFrameIndex{0};
	std::shared_ptr<AudioFrame> mAudioFrame;
	HANDLE mAudioFrameEvent;

	bool mBlockFilterOnRefreshRateChange{false};
	std::unique_ptr<std::string> mInvalidHdrMetaDataItems;
};

/**
 * A video stream flowing from the capture device to an output pin.
 */
class BlackmagicVideoCapturePin final :
	public HdmiVideoCapturePin<BlackmagicCaptureFilter, VideoFrame>
{
public:
	BlackmagicVideoCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview, VIDEO_FORMAT pVideoFormat,
	                          bool pDoRefreshRateSwitches);
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

protected:
	void DoThreadDestroy() override;
	void OnChangeMediaType() override;
	void DoChangeRefreshRate() override;

	std::shared_ptr<VideoFrame> mCurrentFrame;

private:
	void OnFrameWriterStrategyUpdated() override
	{
		switch (mFrameWriterStrategy)
		{
		case ANY_RGB:
			mFrameWriter = std::make_unique<any_rgb>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case STRAIGHT_THROUGH:
			mFrameWriter = std::make_unique<StraightThrough>(mLogData, mVideoFormat.cx, mVideoFormat.cy,
			                                                 &mVideoFormat.pixelFormat);
			break;
		case YUY2_YV16:
		case Y210_P210:
		case UYVY_YV16:
		case BGR10_BGR48:
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Conversion strategy {} is not supported by bmcapture",
			          mLogData.prefix, to_string(mFrameWriterStrategy));
			#endif
			break;
		default:
			HdmiVideoCapturePin::OnFrameWriterStrategyUpdated();
		}
	}

	AsyncModeSwitcher mRateSwitcher;
	bool mDoRefreshRateSwitches{true};
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
#endif