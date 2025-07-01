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
#ifndef CAPTURE_FILTER_HEADER
#define CAPTURE_FILTER_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "logging.h"
#include "metric.h"
#include "signalinfo.h"
#include "modeswitcher.h"

#include <Windows.h>
#include "ISpecifyPropertyPages2.h"
#include "lavfilters_side_data.h"
#include <set>

#ifndef NO_QUILL
#include <quill/Backend.h>
#endif

EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

inline constexpr auto hdrProfileRegKey = L"hdrProfile";
inline constexpr auto sdrProfileRegKey = L"sdrProfile";
inline constexpr auto hdrProfileSwitchEnabledRegKey = L"hdrProfileSwitchEnabled";
inline constexpr auto refreshRateSwitchEnabledRegKey = L"refreshRateSwitchEnabled";

 // Non template parts of the filter impl
class capture_filter :
	public IReferenceClock,
	public IAMFilterMiscFlags,
	public ISpecifyPropertyPages2,
	public ISignalInfo,
	public CSource
{
public:
	DECLARE_IUNKNOWN;

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

	void GetReferenceTime(REFERENCE_TIME* rt) const;

	//////////////////////////////////////////////////////////////////////////
	//  IReferenceClock
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetTime(REFERENCE_TIME* pTime) override;
	HRESULT AdviseTime(REFERENCE_TIME baseTime, REFERENCE_TIME streamTime, HEVENT hEvent,
		DWORD_PTR* pdwAdviseCookie) override;
	HRESULT AdvisePeriodic(REFERENCE_TIME startTime, REFERENCE_TIME periodTime, HSEMAPHORE hSemaphore,
		DWORD_PTR* pdwAdviseCookie) override;
	HRESULT Unadvise(DWORD_PTR dwAdviseCookie) override;

	//////////////////////////////////////////////////////////////////////////
	//  IAMFilterMiscFlags
	//////////////////////////////////////////////////////////////////////////
	ULONG GetMiscFlags() override;

	//////////////////////////////////////////////////////////////////////////
	//  IMediaFilter
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP GetState(DWORD dw, FILTER_STATE* pState) override;
	STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
	STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
	STDMETHODIMP Run(REFERENCE_TIME tStart) override;
	STDMETHODIMP Pause() override;
	STDMETHODIMP Stop() override;

	//////////////////////////////////////////////////////////////////////////
	//  ISignalInfo
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP Reload() override
	{
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->Reload(&mAudioInputStatus);
			mInfoCallback->Reload(&mAudioOutputStatus);
			mInfoCallback->Reload(&mVideoInputStatus);
			mInfoCallback->Reload(&mVideoOutputStatus);
			mInfoCallback->Reload(&mHdrStatus);
			mInfoCallback->Reload(&mDeviceStatus);
			mInfoCallback->Reload(&mDisplayStatus);
			mInfoCallback->ReloadV1(&mVideoCaptureLatencyStatus);
			mInfoCallback->ReloadV2(&mVideoConversionLatencyStatus);
			mInfoCallback->ReloadV3(&mVideoAllocatorLatencyStatus);
			mInfoCallback->ReloadA1(&mAudioCaptureLatencyStatus);
			mInfoCallback->ReloadA2(&mAudioAllocatorLatencyStatus);
			mInfoCallback->ReloadProfiles(mRefreshRateSwitchEnabled, mHdrProfileSwitchEnabled, mHdrProfile,
				mSdrProfile);
			return S_OK;
		}
		return E_FAIL;
	}

	STDMETHODIMP SetCallback(ISignalInfoCB* cb) override
	{
		mInfoCallback = cb;
		return S_OK;
	}

	STDMETHODIMP GetHDRProfile(DWORD* profile) override
	{
		if (!profile) return E_POINTER;
		*profile = mHdrProfile;
		return S_OK;
	}

	STDMETHODIMP SetHDRProfile(DWORD profile) override;

	STDMETHODIMP GetSDRProfile(DWORD* profile) override
	{
		if (!profile) return E_POINTER;
		*profile = mSdrProfile;
		return S_OK;
	}

	STDMETHODIMP SetSDRProfile(DWORD profile) override;

	STDMETHODIMP IsHdrProfileSwitchEnabled(bool* enabled) override
	{
		if (!enabled) return E_POINTER;
		*enabled = mHdrProfileSwitchEnabled;
		return S_OK;
	}

	STDMETHODIMP SetHdrProfileSwitchEnabled(bool enabled) override;

	STDMETHODIMP IsRefreshRateSwitchEnabled(bool* enabled) override
	{
		if (!enabled) return E_POINTER;
		*enabled = mRefreshRateSwitchEnabled;
		return S_OK;
	}

	STDMETHODIMP SetRefreshRateSwitchEnabled(bool enabled) override;

	DWORD GetMCProfileId(bool hdr)
	{
		return hdr ? mHdrProfile : mSdrProfile;
	}

	//////////////////////////////////////////////////////////////////////////
	//  ISpecifyPropertyPages2
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP GetPages(CAUUID* pPages) override;
	STDMETHODIMP CreatePage(const GUID& guid, IPropertyPage** ppPage) override;

	void OnModeUpdated(const mode_switch_result& result);
	void OnVideoFormatLoaded(VIDEO_FORMAT* vf);
	void OnAudioFormatLoaded(AUDIO_FORMAT* af);
	void OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light);

	void RecordVideoFrameLatency(const frame_metrics& metrics)
	{
		// TODO impl
	}

	void RecordAudioFrameLatency(const frame_metrics& metrics)
	{
		// TODO impl
	}

	void OnVideoCaptureLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mVideoCaptureLatencyStatus, "Video Capture");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadV1(&mVideoCaptureLatencyStatus);
		}
	}

	void OnVideoConversionLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mVideoConversionLatencyStatus, "Video Conversion");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadV2(&mVideoConversionLatencyStatus);
		}
	}

	void OnVideoAllocatorLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mVideoAllocatorLatencyStatus, "Video Allocator");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadV3(&mVideoAllocatorLatencyStatus);
		}
	}

	void OnAudioCaptureLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mAudioCaptureLatencyStatus, "Audio Capture");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadA1(&mAudioCaptureLatencyStatus);
		}
	}

	void OnAudioAllocatorLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mAudioAllocatorLatencyStatus, "Audio Allocator");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadA2(&mAudioAllocatorLatencyStatus);
		}
	}

protected:
	capture_filter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, const std::string& pLogPrefix,
		std::wstring pRegKeyBase);

	~capture_filter() override
	{
		#ifndef NO_QUILL
		quill::Backend::stop();
		#endif
	}

	log_data mLogData{};
	IReferenceClock* mClock;
	DEVICE_STATUS mDeviceStatus{};
	DISPLAY_STATUS mDisplayStatus{};
	AUDIO_INPUT_STATUS mAudioInputStatus{};
	AUDIO_OUTPUT_STATUS mAudioOutputStatus{};
	VIDEO_INPUT_STATUS mVideoInputStatus{};
	VIDEO_OUTPUT_STATUS mVideoOutputStatus{};
	CAPTURE_LATENCY mVideoCaptureLatencyStatus{};
	CAPTURE_LATENCY mVideoConversionLatencyStatus{};
	CAPTURE_LATENCY mVideoAllocatorLatencyStatus{};
	CAPTURE_LATENCY mAudioCaptureLatencyStatus{};
	CAPTURE_LATENCY mAudioAllocatorLatencyStatus{};
	HDR_STATUS mHdrStatus{};
	ISignalInfoCB* mInfoCallback = nullptr;
	std::set<DWORD> mRefreshRates{};
	std::wstring mRegKeyBase{};
	DWORD mHdrProfile{ 0 };
	DWORD mSdrProfile{ 0 };
	bool mHdrProfileSwitchEnabled{ false };
	bool mRefreshRateSwitchEnabled{ true };

private:
	void CaptureLatency(const metric& metric, CAPTURE_LATENCY& lat, const std::string& desc)
	{
		lat.min = metric.min();
		lat.mean = metric.mean();
		lat.max = metric.max();

		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] {} latency stats {:.3f},{:.3f},{:.3f}", mLogData.prefix,
			desc,
			static_cast<double>(lat.min) / 1000.0,
			lat.mean / 1000.0,
			static_cast<double>(lat.max) / 1000.0);
		#endif
	}
};

template <typename D_INF, typename V_SIG, typename A_SIG>
class hdmi_capture_filter : public capture_filter
{
public:
	// Callbacks to update the prop page data
	virtual void OnVideoSignalLoaded(V_SIG* vs) = 0;
	virtual void OnAudioSignalLoaded(A_SIG* as) = 0;
	virtual void OnDeviceUpdated() = 0;

protected:
	hdmi_capture_filter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, const std::string& logPrefix,
		const std::wstring& regKeyBase) :
		capture_filter(pName, punk, phr, clsid, logPrefix, regKeyBase)
	{
	}

	D_INF mDeviceInfo{};
};

#endif