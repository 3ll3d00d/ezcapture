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
#include "status_snapshot.h"
#include "web_server.h"

#include <streams.h>
#include "ISpecifyPropertyPages2.h"
#include "lavfilters_side_data.h"
#include <array>
#include <set>
#include <mutex>

EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

inline constexpr auto hdrProfileRegKey = L"hdrProfile";
inline constexpr auto sdrProfileRegKey = L"sdrProfile";
inline constexpr auto hdrProfileSwitchEnabledRegKey = L"hdrProfileSwitchEnabled";
inline constexpr auto refreshRateSwitchEnabledRegKey = L"refreshRateSwitchEnabled";
inline constexpr auto highThreadPriorityEnabledRegKey = L"highThreadPriorityEnabled";
inline constexpr auto audioCaptureEnabledRegKey = L"audioCaptureEnabled";
inline constexpr auto webEnabledRegKey = L"webEnabled";
inline constexpr auto webPortRegKey = L"webPort";
inline constexpr auto webBindAddressRegKey = L"webBindAddress";
inline constexpr auto webTokenRegKey = L"webToken";

struct web_config
{
	bool enabled{false};
	uint16_t port{0};
	std::string bindAddress{"127.0.0.1"};
	std::string token;
};

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
	// reloads the property page, and sends the status callback the settings as the page calls this after applying
	// its changes
	STDMETHODIMP Reload() override
	{
		bool havePage;
		{
			std::lock_guard settingsLock{mSettingsLock};
			std::lock_guard infoCallbackLock{mInfoCallbackLock};
			havePage = mInfoCallback != nullptr;
			if (havePage)
			{
				ReloadAll(mInfoCallback);
			}
		}
		NotifyControls(false);
		return havePage ? S_OK : E_FAIL;
	}

	// the property page's callback
	STDMETHODIMP SetCallback(ISignalInfoCB* cb) override
	{
		std::lock_guard infoCallbackLock{mInfoCallbackLock};
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

	STDMETHODIMP GetMetricsBindAddress(std::wstring* address) override
	{
		if (!address) return E_POINTER;
		std::lock_guard lock{mSettingsLock};
		address->assign(mMetricsConfig.bindAddress.begin(), mMetricsConfig.bindAddress.end());
		return S_OK;
	}

	STDMETHODIMP SetMetricsBindAddress(const std::wstring& address) override;

	STDMETHODIMP IsWebEnabled(bool* enabled) override
	{
		CheckPointer(enabled, E_POINTER)
		*enabled = mWebConfig.enabled;
		return S_OK;
	}

	STDMETHODIMP SetWebEnabled(bool enabled) override;

	STDMETHODIMP GetWebPort(DWORD* port) override
	{
		CheckPointer(port, E_POINTER)
		*port = mWebConfig.port;
		return S_OK;
	}

	STDMETHODIMP SetWebPort(DWORD port) override;

	STDMETHODIMP GetWebBindAddress(std::wstring* address) override
	{
		CheckPointer(address, E_POINTER)
		std::lock_guard lock{mSettingsLock};
		address->assign(mWebConfig.bindAddress.begin(), mWebConfig.bindAddress.end());
		return S_OK;
	}

	STDMETHODIMP SetWebBindAddress(const std::wstring& address) override;
	STDMETHODIMP NewWebToken() override;

	DWORD GetMCProfileId(bool hdr)
	{
		return hdr ? mHdrProfile : mSdrProfile;
	}

	//////////////////////////////////////////////////////////////////////////
	//  ISpecifyPropertyPages2
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP GetPages(CAUUID* pPages) override;
	STDMETHODIMP CreatePage(const GUID& guid, IPropertyPage** ppPage) override;
	// the property page CLSID, unique to each filter and build variant so they can be registered side by side
	virtual const GUID& GetPropertyPageClsid() const = 0;

	void OnModeUpdated(const mode_switch_result& result);
	void OnVideoFormatLoaded(video_format* vf);
	void OnAudioFormatLoaded(audio_format* af);
	void OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light);

	void OnDvStatusUpdated(const dv_status& status)
	{
		mDvStatus = status;
		std::lock_guard infoCallbackLock{mInfoCallbackLock};
		for (auto* cb : InfoCallbacks())
		{
			cb->Reload(&mDvStatus);
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
		mVideoHeadroom.intervalMs = metrics.intervalTicks / 10000.0;
		mVideoHeadroom.busyMeanMs = metrics.busy.mean() / 10000.0;
		mVideoHeadroom.busyMaxMs = static_cast<double>(metrics.busy.max()) / 10000.0;

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

		std::lock_guard infoCallbackLock{mInfoCallbackLock};
		for (auto* cb : InfoCallbacks())
		{
			cb->ReloadV1(&mVideoLatencyStats1);
			cb->ReloadV2(&mVideoLatencyStats2);
			cb->ReloadV3(&mVideoLatencyStats3);
			cb->ReloadVfps(&mVideoMeasuredFps);
			cb->ReloadVHeadroom(&mVideoHeadroom);
		}
	}

	// called each time the metrics window completes
	void RecordAudioFrameLatency(const frame_metrics& metrics)
	{
		mMetrics.on_window(metric_stream::AUDIO, metrics);
		const std::string src = "audio";
		CaptureLatency(metrics.m1, mAudioLatencyStats1, metrics.name1, src);
		CaptureLatency(metrics.m2, mAudioLatencyStats2, metrics.name2, src);
		std::lock_guard infoCallbackLock{mInfoCallbackLock};
		for (auto* cb : InfoCallbacks())
		{
			cb->ReloadA1(&mAudioLatencyStats1);
			cb->ReloadA2(&mAudioLatencyStats2);
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
	               std::wstring pRegKeyBase, uint16_t pDefaultMetricsPort, uint16_t pDefaultWebPort);

	~capture_filter() override;

	// deletes the pins and the clock, a subclass whose pins or clock use a device handle calls this before
	// releasing that handle as the base destructor only runs after the subclass destructor
	void ReleasePinsAndClock();

	// the id of the filter in web API paths, stable for a given device, e.g. magewell-<serial>
	virtual std::string GetWebId() const = 0;
	// a description of the filter for the web API
	virtual std::string GetWebName() const = 0;

	// starts the web API if it is enabled, the subclass calls this at the end of its constructor once the device
	// (and so GetWebId) is known
	void StartWebApi();
	// stops the web API, waiting for any request in progress; the subclass calls this at the start of its destructor
	// so no request can reach the filter while it is torn down
	void StopWebApi();

	log_data mLogData{};
	CBaseReferenceClock* mClock{nullptr};
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
	video_headroom mVideoHeadroom{};
	hdr_status mHdrStatus{};
	dv_status mDvStatus{};
	// the property page and the status callback (the web API's snapshot), held while they are called so neither can
	// be removed (and freed) mid call; the page only posts to its UI thread and the snapshot only copies the payload
	// so the lock is never held for long
	std::recursive_mutex mInfoCallbackLock;
	ISignalInfoCB* mInfoCallback = nullptr;
	ISignalInfoCB* mStatusCallback = nullptr;

	// the callbacks that are set, call while holding mInfoCallbackLock. When both locks are needed, mSettingsLock is
	// taken first.
	struct info_callbacks
	{
		std::array<ISignalInfoCB*, 2> cbs{};
		size_t count{0};
		using iterator = std::array<ISignalInfoCB*, 2>::const_iterator;
		iterator begin() const { return cbs.begin(); }
		iterator end() const { return cbs.begin() + static_cast<std::ptrdiff_t>(count); }
	};

	info_callbacks InfoCallbacks() const
	{
		info_callbacks result;
		if (mInfoCallback != nullptr)
		{
			result.cbs[result.count++] = mInfoCallback;
		}
		if (mStatusCallback != nullptr)
		{
			result.cbs[result.count++] = mStatusCallback;
		}
		return result;
	}

	// sends everything to cb, call while holding mInfoCallbackLock
	void ReloadAll(ISignalInfoCB* cb)
	{
		cb->Reload(&mAudioInputStatus);
		cb->Reload(&mAudioOutputStatus);
		cb->Reload(&mVideoInputStatus);
		cb->Reload(&mVideoOutputStatus);
		cb->Reload(&mHdrStatus);
		cb->Reload(&mDvStatus);
		cb->Reload(&mDeviceStatus);
		cb->Reload(&mDisplayStatus);
		cb->ReloadV1(&mVideoLatencyStats1);
		cb->ReloadV2(&mVideoLatencyStats2);
		cb->ReloadV3(&mVideoLatencyStats3);
		cb->ReloadVfps(&mVideoMeasuredFps);
		cb->ReloadVHeadroom(&mVideoHeadroom);
		cb->ReloadA1(&mAudioLatencyStats1);
		cb->ReloadA2(&mAudioLatencyStats2);
		cb->ReloadControls(mRefreshRateSwitchEnabled, mHdrProfileSwitchEnabled, mHdrProfile,
		                   mSdrProfile, mHighThreadPriorityEnabled, mAudioCaptureEnabled);
		cb->ReloadMetricsControls(mMetricsConfig.enabled, mMetricsConfig.bindAddress, mMetricsConfig.port,
		                          mMetricsStatus);
		cb->ReloadWebControls(mWebConfig.enabled, mWebConfig.bindAddress, mWebConfig.port, mWebStatus, mWebUrl,
		                      mWebConfig.token);
	}

	// sets (or clears, with nullptr) the status callback and sends it everything; not part of ISignalInfo so the
	// property page cannot replace it
	void SetStatusCallback(ISignalInfoCB* cb)
	{
		std::lock_guard settingsLock{mSettingsLock};
		std::lock_guard infoCallbackLock{mInfoCallbackLock};
		mStatusCallback = cb;
		if (cb != nullptr)
		{
			ReloadAll(cb);
		}
	}
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
	web_config mWebConfig{};
	uint16_t mDefaultWebPort{0};
	std::string mWebStatus{"Disabled"};
	// the address to open the page, including the token
	std::string mWebUrl;

	// guards the settings above against the property page and the web API changing them at the same time
	mutable std::recursive_mutex mSettingsLock;

	// the settings that only apply when the filter is loaded, as they were when it was
	struct loaded_settings
	{
		bool audioCapture{true};
		metrics_config metrics{};
	} mLoadedSettings{};

	// tells the callbacks the settings changed, includePage false when the page made the change (it is reading its
	// own controls at the time and reloads itself afterwards)
	void NotifyControls(bool includePage);

	// the web API's controls, called on a web server thread
	control_value GetWebControl(const control_def& def) const;
	bool SetWebControl(const control_def& def, const control_value& value);
	bool WebControlPending(const control_def& def) const;

	// the web API's view of the filter, it reads the status snapshot and calls back into the filter for controls
	class web_adapter : public web_target
	{
	public:
		explicit web_adapter(capture_filter* f) : filter(f)
		{
		}

		const status_snapshot& Status() const override { return snapshot; }
		control_value GetControl(const control_def& def) const override { return filter->GetWebControl(def); }

		bool SetControl(const control_def& def, const control_value& value) override
		{
			return filter->SetWebControl(def, value);
		}

		bool ControlPending(const control_def& def) const override { return filter->WebControlPending(def); }

		status_snapshot snapshot;

	private:
		capture_filter* filter;
	};

	std::unique_ptr<web_adapter> mWeb;
	std::shared_ptr<web_server> mWebServer;
	std::shared_ptr<web_registration> mWebRegistration;

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
	                    const std::wstring& regKeyBase, uint16_t defaultMetricsPort, uint16_t defaultWebPort) :
		capture_filter(pName, punk, phr, clsid, logPrefix, regKeyBase, defaultMetricsPort, defaultWebPort)
	{
	}

	D_INF mDeviceInfo{};
};

#endif
