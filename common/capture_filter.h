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
#include "metrics.h"
#include "modeswitcher.h"

#include <streams.h>
#include "ISpecifyPropertyPages2.h"
#include "lavfilters_side_data.h"
#include <set>

EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

inline constexpr auto hdrProfileRegKey = L"hdrProfile";
inline constexpr auto sdrProfileRegKey = L"sdrProfile";
inline constexpr auto hdrProfileSwitchEnabledRegKey = L"hdrProfileSwitchEnabled";
inline constexpr auto refreshRateSwitchEnabledRegKey = L"refreshRateSwitchEnabled";
inline constexpr auto highThreadPriorityEnabledRegKey = L"highThreadPriorityEnabled";
inline constexpr auto audioCaptureEnabledRegKey = L"audioCaptureEnabled";

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
			mInfoCallback->Reload(&mDvStatus);
			mInfoCallback->Reload(&mDeviceStatus);
			mInfoCallback->Reload(&mDisplayStatus);
			mInfoCallback->ReloadV1(&mVideoLatencyStats1);
			mInfoCallback->ReloadV2(&mVideoLatencyStats2);
			mInfoCallback->ReloadV3(&mVideoLatencyStats3);
			mInfoCallback->ReloadVfps(&mVideoMeasuredFps);
			mInfoCallback->ReloadA1(&mAudioLatencyStats1);
			mInfoCallback->ReloadA2(&mAudioLatencyStats2);
			mInfoCallback->ReloadControls(mRefreshRateSwitchEnabled, mHdrProfileSwitchEnabled, mHdrProfile,
			                              mSdrProfile, mHighThreadPriorityEnabled, mAudioCaptureEnabled);
			mInfoCallback->ReloadMetricsControls(mMetricsConfig.enabled, mMetricsConfig.port, mMetricsStatus);
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

	STDMETHODIMP IsHighThreadPriorityEnabled(bool* enabled) override
	{
		if (!enabled) return E_POINTER;
		*enabled = mHighThreadPriorityEnabled;
		return S_OK;
	}

	STDMETHODIMP SetHighThreadPriorityEnabled(bool enabled) override;

	STDMETHODIMP IsAudioCaptureEnabled(bool* enabled) override
	{
		if (!enabled) return E_POINTER;
		*enabled = mAudioCaptureEnabled;
		return S_OK;
	}

	STDMETHODIMP SetAudioCaptureEnabled(bool enabled) override;

	STDMETHODIMP IsMetricsEnabled(bool* enabled) override
	{
		if (!enabled) return E_POINTER;
		*enabled = mMetricsConfig.enabled;
		return S_OK;
	}

	STDMETHODIMP SetMetricsEnabled(bool enabled) override;

	STDMETHODIMP GetMetricsPort(DWORD* port) override
	{
		if (!port) return E_POINTER;
		*port = mMetricsConfig.port;
		return S_OK;
	}

	STDMETHODIMP SetMetricsPort(DWORD port) override;

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
	void OnVideoFormatLoaded(video_format* vf);
	void OnAudioFormatLoaded(audio_format* af);
	void OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light);

	void OnDvStatusUpdated(const dv_status& status)
	{
		mDvStatus = status;
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->Reload(&mDvStatus);
		}
	}

	// called for every frame
	void OnVideoFrameMeasured(const frame_metrics& metrics)
	{
		mMetrics.on_frame(metric_stream::VIDEO, metrics);
	}

	// called for every frame
	void OnAudioFrameMeasured(const frame_metrics& metrics)
	{
		mMetrics.on_frame(metric_stream::AUDIO, metrics);
	}

	// called each time the metrics window completes
	void RecordVideoFrameLatency(const frame_metrics& metrics)
	{
		mMetrics.on_window(metric_stream::VIDEO, metrics);
		const std::string src = "video";
		CaptureLatency(metrics.m1, mVideoLatencyStats1, metrics.name1, src);
		CaptureLatency(metrics.m2, mVideoLatencyStats2, metrics.name2, src);
		mVideoMeasuredFps = metrics.actualFrameRate;

		#ifndef NO_LOGGING
		LOG_TRACE_L2(mLogData.logger, "[{}] Measured fps {:.3f} Hz ({},{},{},{})", mLogData.prefix,
		             mVideoMeasuredFps, metrics.startTs, metrics.endTs, metrics.endTs - metrics.startTs,
		             metrics.m1.capacity()-1);
		#endif

		if (metrics.name3.empty())
		{
			mVideoLatencyStats3.name.clear();
		}
		else
		{
			CaptureLatency(metrics.m3, mVideoLatencyStats3, metrics.name3, src);
		}

		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadV1(&mVideoLatencyStats1);
			mInfoCallback->ReloadV2(&mVideoLatencyStats2);
			mInfoCallback->ReloadV3(&mVideoLatencyStats3);
			mInfoCallback->ReloadVfps(&mVideoMeasuredFps);
		}
	}

	// called each time the metrics window completes
	void RecordAudioFrameLatency(const frame_metrics& metrics)
	{
		mMetrics.on_window(metric_stream::AUDIO, metrics);
		const std::string src = "audio";
		CaptureLatency(metrics.m1, mAudioLatencyStats1, metrics.name1, src);
		CaptureLatency(metrics.m2, mAudioLatencyStats2, metrics.name2, src);
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadA1(&mAudioLatencyStats1);
			mInfoCallback->ReloadA2(&mAudioLatencyStats2);
		}
	}

protected:
	// publish the current signal to the (optional) Prometheus metrics, call after any of the status changes
	void PublishVideoSignalMetrics()
	{
		mMetrics.on_video_signal(mVideoInputStatus, mVideoOutputStatus, mHdrStatus);
	}

	void PublishAudioSignalMetrics()
	{
		mMetrics.on_audio_signal(mAudioInputStatus, mAudioOutputStatus);
	}

	capture_filter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, const std::string& pLogPrefix,
	               std::wstring pRegKeyBase, uint16_t pDefaultMetricsPort);

	~capture_filter() override;

	log_data mLogData{};
	IReferenceClock* mClock;
	device_status mDeviceStatus{};
	display_status mDisplayStatus{};
	audio_input_status mAudioInputStatus{};
	audio_output_status mAudioOutputStatus{};
	video_input_status mVideoInputStatus{};
	video_output_status mVideoOutputStatus{};
	latency_stats mVideoLatencyStats1{};
	latency_stats mVideoLatencyStats2{};
	latency_stats mVideoLatencyStats3{};
	latency_stats mAudioLatencyStats1{};
	latency_stats mAudioLatencyStats2{};
	double mVideoMeasuredFps{0.0};
	hdr_status mHdrStatus{};
	dv_status mDvStatus{};
	ISignalInfoCB* mInfoCallback = nullptr;
	std::set<DWORD> mRefreshRates{};
	std::wstring mRegKeyBase{};
	DWORD mHdrProfile{0};
	DWORD mSdrProfile{0};
	bool mHdrProfileSwitchEnabled{false};
	bool mRefreshRateSwitchEnabled{true};
	bool mHighThreadPriorityEnabled{true};
	bool mAudioCaptureEnabled{true};
	metrics_config mMetricsConfig{};
	std::string mMetricsStatus{"Disabled"};
	capture_metrics mMetrics{};

private:
	// the quill backend is shared by every filter in the module so is started/stopped by reference count
	static void AcquireQuillBackend(const std::string& logPrefix);
	static void ReleaseQuillBackend();

	bool mQuillBackendAcquired{false};
	#ifndef NO_LOGGING
	void AcquireLogging(const std::string& pLogPrefix);
	void AcquireAudioLatencyLogger(const std::string& pLogPrefix);
	static void ReleaseLogging();
	bool mLoggingAcquired{false};
	#endif

	void CaptureLatency(const metric& metric, latency_stats& lat, const std::string& desc, const std::string& src)
	{
		lat.min = metric.min();
		lat.mean = metric.mean();
		lat.max = metric.max();
		lat.name = desc;

		#ifndef NO_LOGGING
		LOG_TRACE_L2(mLogData.logger, "[{}] {} {} latency stats {:.3f},{:.3f},{:.3f}", mLogData.prefix,
		             desc, src, static_cast<double>(lat.min) / 10000.0, lat.mean / 10000.0,
		             static_cast<double>(lat.max) / 10000.0);
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
	                    const std::wstring& regKeyBase, uint16_t defaultMetricsPort) :
		capture_filter(pName, punk, phr, clsid, logPrefix, regKeyBase, defaultMetricsPort)
	{
	}

	D_INF mDeviceInfo{};
};

#endif
