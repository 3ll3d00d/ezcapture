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
#ifndef BM_CAPTURE_FILTER_HEADER
#define BM_CAPTURE_FILTER_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "capture_filter.h"
#include <atlcomcli.h>
#include "bm_domain.h"
#include "video_frame.h"
#include <functional>
#include <chrono>

EXTERN_C const GUID CLSID_BMCAPTURE_FILTER;
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

inline constexpr int64_t invalidFrameTime = std::numeric_limits<int64_t>::lowest();
inline constexpr BMDAudioSampleType audioBitDepth = bmdAudioSampleType16bitInteger;

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
class blackmagic_capture_filter final :
	public hdmi_capture_filter<device_info, video_signal, audio_signal>,
	public IDeckLinkInputCallback,
	public IDeckLinkNotificationCallback
{
public:
	// Provide the way for COM to create a Filter object
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

	// Callbacks to update the prop page data
	void OnVideoSignalLoaded(video_signal* vs) override;
	void OnAudioSignalLoaded(audio_signal* as) override;
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
		return capture_filter::AddRef();
	}

	ULONG Release() override
	{
		return capture_filter::Release();
	}

	HANDLE GetVideoFrameHandle() const
	{
		return mVideoFrameEvent;
	}

	std::shared_ptr<video_frame> GetVideoFrame()
	{
		return mVideoFrame;
	}

	HANDLE GetAudioFrameHandle() const
	{
		return mAudioFrameEvent;
	}

	std::shared_ptr<audio_frame> GetAudioFrame()
	{
		return mAudioFrame;
	}

	HRESULT processVideoFrame(IDeckLinkVideoInputFrame* videoFrame);

	HRESULT processAudioPacket(IDeckLinkAudioInputPacket* audioPacket, const REFERENCE_TIME& now);

protected:
	static void LoadFormat(video_format* videoFormat, const video_signal* videoSignal);
	static void LoadSignalFromDisplayMode(video_signal* newSignal, IDeckLinkDisplayMode* newDisplayMode);
	static void LoadFormat(audio_format* audioFormat, const audio_signal* audioSignal);

	STDMETHODIMP Run(REFERENCE_TIME tStart) override;

private:
	// Constructor
	blackmagic_capture_filter(LPUNKNOWN punk, HRESULT* phr);
	~blackmagic_capture_filter() override;

	CComPtr<IDeckLink> mDeckLink;
	CComQIPtr<IDeckLinkInput> mDeckLinkInput;
	CComQIPtr<IDeckLinkNotification> mDeckLinkNotification;
	CComQIPtr<IDeckLinkStatus> mDeckLinkStatus;
	CComQIPtr<IDeckLinkHDMIInputEDID> mDeckLinkHDMIInputEDID;
	CCritSec mFrameSec;
	CCritSec mDeckLinkSec;

	uint8_t mRunningPins{ 0 };
	video_signal mVideoSignal{};
	video_format mVideoFormat{};

	int64_t mPreviousVideoFrameTime{ invalidFrameTime };
	int64_t mVideoFrameTime{ 0 };
	uint64_t mCurrentVideoFrameIndex{ 0 };
	std::shared_ptr<video_frame> mVideoFrame;
	HANDLE mVideoFrameEvent;

	audio_signal mAudioSignal{};
	audio_format mAudioFormat{};
	int64_t mPreviousAudioFrameTime{ invalidFrameTime };
	int64_t mAudioFrameTime{ 0 };
	uint64_t mCurrentAudioFrameIndex{ 0 };
	std::shared_ptr<audio_frame> mAudioFrame;
	HANDLE mAudioFrameEvent;

	std::unique_ptr<std::string> mInvalidHdrMetaDataItems;
};

#endif