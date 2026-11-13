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
#include "quill/Backend.h"
#ifndef NO_LOGGING
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/RotatingFileSink.h"
#include <string_view>
#include <utility>
#endif
#include <mutex>

#include "capture_filter.h"
#include "runtime_aware.h"
#include "version.h"
#include "winreg/WinReg.hpp"

#ifdef _DEBUG
#define MIN_LOG_LEVEL quill::LogLevel::TraceL3
#else
#define MIN_LOG_LEVEL quill::LogLevel::TraceL2
#endif

namespace
{
	std::mutex quillBackendLock;
	uint32_t quillBackendUsers{0};

	std::string to_narrow(const std::wstring& in)
	{
		std::string out;
		out.reserve(in.size());
		for (const auto c : in)
		{
			out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
		}
		return out;
	}
}

void capture_filter::AcquireQuillBackend(const std::string& logPrefix)
{
	std::lock_guard lock{quillBackendLock};
	if (quillBackendUsers++ == 0)
	{
		quill::BackendOptions bopt;
		bopt.thread_name = "QuillBackend_" + logPrefix;
		#ifdef NO_LOGGING
		// only metric samples to process and those are scraped every few seconds at most
		bopt.sleep_duration = std::chrono::milliseconds(50);
		#else
		bopt.enable_yield_when_idle = true;
		bopt.sleep_duration = std::chrono::nanoseconds(0);
		#endif
		quill::Backend::start(bopt);
	}
}

void capture_filter::ReleaseQuillBackend()
{
	std::lock_guard lock{quillBackendLock};
	if (quillBackendUsers > 0 && --quillBackendUsers == 0)
	{
		quill::Backend::stop();
	}
}

#ifndef NO_LOGGING
namespace
{
	// quill loggers are process wide and shared by name so every filter instance in the process (e.g. one created
	// only to show the property page while another is playing) uses the same loggers, they are created by the first
	// instance and removed by the last
	std::mutex loggingLock;
	int loggingUsers{0};
	int64_t loggingEpochSeconds{0};

	quill::RotatingFileSinkConfig latency_sink_config()
	{
		quill::RotatingFileSinkConfig cfg;
		cfg.set_max_backup_files(3);
		cfg.set_rotation_time_daily("00:00");
		cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
		cfg.set_filename_append_option(quill::FilenameAppendOption::None);
		return cfg;
	}

	quill::PatternFormatterOptions latency_pattern()
	{
		return quill::PatternFormatterOptions{"%(time),%(message)", "%H:%M:%S.%Qns", quill::Timezone::GmtTime};
	}
}

void capture_filter::AcquireLogging(const std::string& pLogPrefix)
{
	std::lock_guard lock{loggingLock};
	if (loggingUsers++ == 0)
	{
		loggingEpochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		auto filterFileSink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
			(std::filesystem::temp_directory_path() / std::format("{}_{}.log", pLogPrefix, loggingEpochSeconds)).
			string(),
			[]()
			{
				quill::RotatingFileSinkConfig cfg;
				cfg.set_max_backup_files(12);
				cfg.set_rotation_frequency_and_interval('H', 1);
				cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::DateAndTime);
				cfg.set_filename_append_option(quill::FilenameAppendOption::None);
				return cfg;
			}(),
			quill::FileEventNotifier{});
		auto* logger = CustomFrontend::create_or_get_logger(std::string{filterLoggerName},
		                                                    std::move(filterFileSink),
		                                                    quill::PatternFormatterOptions{
			                                                    "%(time) [%(thread_id)] %(short_source_location:<28) "
			                                                    "LOG_%(log_level:<9) %(logger:<12) %(message)",
			                                                    "%H:%M:%S.%Qns",
			                                                    quill::Timezone::GmtTime
		                                                    });
		auto videoLatencySink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
			(std::filesystem::temp_directory_path() / std::format("{}_video_{}.csv", pLogPrefix,
			                                                     loggingEpochSeconds)).string(),
			latency_sink_config(),
			quill::FileEventNotifier{});
		auto* videoLat = CustomFrontend::create_or_get_logger(std::string{videoLatencyLoggerName},
		                                                      std::move(videoLatencySink), latency_pattern());
		// printing absolutely everything we may ever log
		logger->set_log_level(MIN_LOG_LEVEL);
		videoLat->set_log_level(MIN_LOG_LEVEL);
	}
	mLogData.logger = CustomFrontend::get_logger(std::string{filterLoggerName});
	mLogData.videoLat = CustomFrontend::get_logger(std::string{videoLatencyLoggerName});
	mLogData.audioLat = CustomFrontend::get_logger(std::string{audioLatencyLoggerName});
	mLoggingAcquired = true;
}

void capture_filter::AcquireAudioLatencyLogger(const std::string& pLogPrefix)
{
	std::lock_guard lock{loggingLock};
	mLogData.audioLat = CustomFrontend::get_logger(std::string{audioLatencyLoggerName});
	if (mLogData.audioLat == nullptr)
	{
		auto audioLatencySink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
			(std::filesystem::temp_directory_path() / std::format("{}_audio_{}.csv", pLogPrefix,
			                                                     loggingEpochSeconds)).string(),
			latency_sink_config(),
			quill::FileEventNotifier{});
		mLogData.audioLat = CustomFrontend::create_or_get_logger(std::string{audioLatencyLoggerName},
		                                                         std::move(audioLatencySink), latency_pattern());
		mLogData.audioLat->set_log_level(MIN_LOG_LEVEL);
	}
}

void capture_filter::ReleaseLogging()
{
	std::lock_guard lock{loggingLock};
	if (--loggingUsers > 0)
	{
		return;
	}
	// the removal must complete while the backend is running, a logger still pending removal cannot be recreated
	// so the next filter instance (i.e. the next playback) would throw from its constructor
	for (const auto* name : {filterLoggerName.data(), audioLatencyLoggerName.data(), videoLatencyLoggerName.data()})
	{
		if (auto* logger = CustomFrontend::get_logger(std::string{name}))
		{
			if (quill::Backend::is_running())
			{
				CustomFrontend::remove_logger_blocking(logger);
			}
			else
			{
				CustomFrontend::remove_logger(logger);
			}
		}
	}
}
#endif

capture_filter::capture_filter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, const std::string& pLogPrefix,
                               std::wstring pRegKeyBase, uint16_t pDefaultMetricsPort) :
	CSource(pName, punk, clsid),
	mRegKeyBase(ROOT_REG_KEY + std::move(pRegKeyBase))
{
	mLogData.prefix = pLogPrefix;
	mMetricsConfig.port = pDefaultMetricsPort;

	#ifndef NO_LOGGING
	AcquireQuillBackend(pLogPrefix);
	mQuillBackendAcquired = true;
	AcquireLogging(pLogPrefix);
	#endif

	auto monitorConfig = mode_switch::GetAllSupportedRefreshRates();
	mRefreshRates = std::move(monitorConfig.refreshRates);
	#ifndef NO_LOGGING
	LOG_INFO(mLogData.logger, "[{}] Initialised filter v{}", mLogData.prefix, EZ_VERSION_STR);
	LOG_INFO(mLogData.logger, "[{}] Monitor {} supported {} ignored {}", mLogData.prefix,
	         monitorConfig.name, monitorConfig.supportedModes, monitorConfig.ignoredModes);
	#endif

	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (auto res = key.TryGetDwordValue(hdrProfileRegKey))
		{
			mHdrProfile = res.GetValue();
		}
		if (auto res = key.TryGetDwordValue(sdrProfileRegKey))
		{
			mSdrProfile = res.GetValue();
		}
		if (auto res = key.TryGetDwordValue(hdrProfileSwitchEnabledRegKey))
		{
			mHdrProfileSwitchEnabled = res.GetValue() == 1;
		}
		if (auto res = key.TryGetDwordValue(refreshRateSwitchEnabledRegKey))
		{
			mRefreshRateSwitchEnabled = res.GetValue() == 1;
		}
		if (auto res = key.TryGetDwordValue(highThreadPriorityEnabledRegKey))
		{
			mHighThreadPriorityEnabled = res.GetValue() == 1;
		}
		if (auto res = key.TryGetDwordValue(audioCaptureEnabledRegKey))
		{
			mAudioCaptureEnabled = res.GetValue() == 1;
		}
		if (auto res = key.TryGetDwordValue(metricsEnabledRegKey))
		{
			mMetricsConfig.enabled = res.GetValue() == 1;
		}
		if (auto res = key.TryGetDwordValue(metricsPortRegKey))
		{
			if (const auto port = res.GetValue(); port > 0 && port <= 65535)
			{
				mMetricsConfig.port = static_cast<uint16_t>(port);
			}
		}
		if (auto res = key.TryGetStringValue(metricsBindAddressRegKey))
		{
			if (auto addr = to_narrow(res.GetValue()); !addr.empty())
			{
				mMetricsConfig.bindAddress = std::move(addr);
			}
		}
		if (auto res = key.TryGetDwordValue(metricsSignalRetentionRegKey))
		{
			mMetricsConfig.signalRetention = std::chrono::seconds{res.GetValue()};
		}
		#ifndef NO_LOGGING
		LOG_INFO(mLogData.logger,
		         "[{}] Loaded properties from registry [hdrProfile:{}, sdrProfile: {}, profileSwitch: {}, rateSwitch: {}, highPriority: {}, audio: {}]",
		         mLogData.prefix, mHdrProfile, mSdrProfile, mHdrProfileSwitchEnabled, mRefreshRateSwitchEnabled,
		         mHighThreadPriorityEnabled, mAudioCaptureEnabled);
		#endif

		if (mAudioCaptureEnabled)
		{
			#ifndef NO_LOGGING
			AcquireAudioLatencyLogger(pLogPrefix);
			#endif
		}
	}

	if (mMetricsConfig.enabled)
	{
		if (!mQuillBackendAcquired)
		{
			AcquireQuillBackend(pLogPrefix);
			mQuillBackendAcquired = true;
		}
		if (auto err = mMetrics.start(pLogPrefix, mMetricsConfig); err.empty())
		{
			mMetricsStatus = "Serving " + mMetrics.endpoint();
			#ifndef NO_LOGGING
			LOG_INFO(mLogData.logger, "[{}] Prometheus metrics available at {}", mLogData.prefix, mMetrics.endpoint());
			#endif
		}
		else
		{
			mMetricsStatus = err;
			#ifndef NO_LOGGING
			LOG_ERROR(mLogData.logger, "[{}] Prometheus metrics are unavailable: {}", mLogData.prefix, err);
			#else
			ReleaseQuillBackend();
			mQuillBackendAcquired = false;
			#endif
		}
	}
}

capture_filter::~capture_filter()
{
	mMetrics.stop();
	// delete the pins here rather than in ~CSource so anything they log as they are torn down happens before the
	// loggers are released (the pin destructor removes it from m_paStreams and decrements m_iPins)
	while (m_iPins > 0)
	{
		delete m_paStreams[m_iPins - 1];
	}
	#ifndef NO_LOGGING
	if (mLoggingAcquired)
	{
		LOG_INFO(mLogData.logger, "[{}] Shutting down logging system", mLogData.prefix);
		ReleaseLogging();
	}
	#endif
	if (mQuillBackendAcquired)
	{
		ReleaseQuillBackend();
	}
}

STDMETHODIMP capture_filter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER)

	if (riid == _uuidof(IReferenceClock))
	{
		return GetInterface(static_cast<IReferenceClock*>(this), ppv);
	}
	if (riid == _uuidof(IAMFilterMiscFlags))
	{
		return GetInterface(static_cast<IAMFilterMiscFlags*>(this), ppv);
	}
	if (riid == _uuidof(ISpecifyPropertyPages2))
	{
		return GetInterface(static_cast<ISpecifyPropertyPages2*>(this), ppv);
	}
	if (riid == IID_ISpecifyPropertyPages)
	{
		return GetInterface(static_cast<ISpecifyPropertyPages*>(this), ppv);
	}
	if (riid == IID_ISignalInfo)
	{
		return GetInterface(static_cast<ISignalInfo*>(this), ppv);
	}
	return CSource::NonDelegatingQueryInterface(riid, ppv);
}

void capture_filter::GetReferenceTime(REFERENCE_TIME* rt) const
{
	m_pClock->GetTime(rt);
}

HRESULT capture_filter::GetTime(REFERENCE_TIME* pTime)
{
	return mClock->GetTime(pTime);
}

HRESULT capture_filter::AdviseTime(REFERENCE_TIME baseTime, REFERENCE_TIME streamTime, HEVENT hEvent,
                                   DWORD_PTR* pdwAdviseCookie)
{
	return mClock->AdviseTime(baseTime, streamTime, hEvent, pdwAdviseCookie);
}

HRESULT capture_filter::AdvisePeriodic(REFERENCE_TIME startTime, REFERENCE_TIME periodTime,
                                       HSEMAPHORE hSemaphore, DWORD_PTR* pdwAdviseCookie)
{
	return mClock->AdvisePeriodic(startTime, periodTime, hSemaphore, pdwAdviseCookie);
}

HRESULT capture_filter::Unadvise(DWORD_PTR dwAdviseCookie)
{
	return mClock->Unadvise(dwAdviseCookie);
}

ULONG capture_filter::GetMiscFlags()
{
	return AM_FILTER_MISC_FLAGS_IS_SOURCE;
};

STDMETHODIMP capture_filter::GetState(DWORD dw, FILTER_STATE* pState)
{
	CBaseFilter::GetState(dw, pState);
	return *pState == State_Paused ? VFW_S_CANT_CUE : S_OK;
}

STDMETHODIMP capture_filter::SetSyncSource(IReferenceClock* pClock)
{
	CBaseFilter::SetSyncSource(pClock);
	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		if (stream != nullptr)
			stream->SetSyncSource(pClock);
		else
		{
			#ifndef NO_LOGGING
			LOG_ERROR(mLogData.logger, "[{}] pin {} is not a CBaseStreamControl!", mLogData.prefix, i);
			#endif
		}
	}
	return NOERROR;
}

HRESULT capture_filter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName)
{
	auto hr = CSource::JoinFilterGraph(pGraph, pName);
	if (SUCCEEDED(hr))
	{
		for (auto i = 0; i < m_iPins; i++)
		{
			auto stream = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
			stream->SetFilterGraph(m_pSink);
		}
	}
	return hr;
}

STDMETHODIMP capture_filter::Run(REFERENCE_TIME tStart)
{
	int64_t rt;
	GetReferenceTime(&rt);
	int64_t hrt = high_res_now();

	#ifndef NO_LOGGING
	LOG_INFO(mLogData.logger, "[{}] Filter has started running at {} / {}", mLogData.prefix, rt, hrt);
	#endif

	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<runtime_aware*>(m_paStreams[i]);
		stream->SetStartTime(rt, hrt);
		const auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Running, tStart);
	}
	return CBaseFilter::Run(tStart);
}

STDMETHODIMP capture_filter::Pause()
{
	for (auto i = 0; i < m_iPins; i++)
	{
		const auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Paused);
	}
	return CBaseFilter::Pause();
}

STDMETHODIMP capture_filter::Stop()
{
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	#ifndef NO_LOGGING
	LOG_INFO(mLogData.logger, "[{}] Filter has stopped running at {}", mLogData.prefix, rt);
	#endif

	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<runtime_aware*>(m_paStreams[i]);
		stream->SetStopTime(rt);
		auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Stopped);
	}
	return CBaseFilter::Stop();
}

HRESULT capture_filter::SetHDRProfile(DWORD profile)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(hdrProfileRegKey, profile))
		{
			if (res)
			{
				mHdrProfile = profile;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetSDRProfile(DWORD profile)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(sdrProfileRegKey, profile))
		{
			if (res)
			{
				mSdrProfile = profile;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetHdrProfileSwitchEnabled(bool enabled)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(hdrProfileSwitchEnabledRegKey, enabled ? 1 : 0))
		{
			if (res)
			{
				mHdrProfileSwitchEnabled = enabled;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetRefreshRateSwitchEnabled(bool enabled)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(refreshRateSwitchEnabledRegKey, enabled ? 1 : 0))
		{
			if (res)
			{
				mRefreshRateSwitchEnabled = enabled;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetHighThreadPriorityEnabled(bool enabled)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(highThreadPriorityEnabledRegKey, enabled ? 1 : 0))
		{
			if (res)
			{
				mHighThreadPriorityEnabled = enabled;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetAudioCaptureEnabled(bool enabled)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(audioCaptureEnabledRegKey, enabled ? 1 : 0))
		{
			if (res)
			{
				mAudioCaptureEnabled = enabled;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetMetricsEnabled(bool enabled)
{
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(metricsEnabledRegKey, enabled ? 1 : 0))
		{
			if (mMetricsConfig.enabled != enabled)
			{
				mMetricsConfig.enabled = enabled;
				mMetricsStatus = "Applies when the filter is next loaded";
			}
			return S_OK;
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetMetricsPort(DWORD port)
{
	if (port == 0 || port > 65535)
	{
		return E_INVALIDARG;
	}
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetDwordValue(metricsPortRegKey, port))
		{
			if (mMetricsConfig.port != port)
			{
				mMetricsConfig.port = static_cast<uint16_t>(port);
				mMetricsStatus = "Applies when the filter is next loaded";
			}
			return S_OK;
		}
	}
	return E_FAIL;
}

HRESULT capture_filter::SetMetricsBindAddress(const std::wstring& address)
{
	if (address.empty())
	{
		return E_INVALIDARG;
	}
	if (winreg::RegKey key{HKEY_CURRENT_USER, mRegKeyBase})
	{
		if (const auto res = key.TrySetStringValue(metricsBindAddressRegKey, address))
		{
			if (auto narrow = to_narrow(address); mMetricsConfig.bindAddress != narrow)
			{
				mMetricsConfig.bindAddress = std::move(narrow);
				mMetricsStatus = "Applies when the filter is next loaded";
			}
			return S_OK;
		}
	}
	return E_FAIL;
}

STDMETHODIMP capture_filter::GetPages(CAUUID* pPages)
{
	CheckPointer(pPages, E_POINTER)
	pPages->cElems = 1;
	pPages->pElems = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID) * pPages->cElems));
	if (pPages->pElems == nullptr)
	{
		return E_OUTOFMEMORY;
	}
	pPages->pElems[0] = CLSID_SignalInfoProps;
	return S_OK;
}

STDMETHODIMP capture_filter::CreatePage(const GUID& guid, IPropertyPage** ppPage)
{
	CheckPointer(ppPage, E_POINTER)
	HRESULT hr = S_OK;

	if (*ppPage != nullptr)
	{
		return E_INVALIDARG;
	}

	if (guid == CLSID_SignalInfoProps)
	{
		*ppPage = new CSignalInfoProp(nullptr, &hr);
	}

	if (SUCCEEDED(hr) && *ppPage)
	{
		(*ppPage)->AddRef();
		return S_OK;
	}
	delete*ppPage;
	ppPage = nullptr;
	return E_FAIL;
}

void capture_filter::OnModeUpdated(const mode_switch_result& result)
{
	switch (result.request)
	{
	case REFRESH_RATE:
		mDisplayStatus.status = result.rateSwitch.displayStatus;
		mDisplayStatus.freq = result.rateSwitch.refreshRate;
		break;
	case MC_PROFILE:
		break;
	}

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mDisplayStatus);
	}
}

void capture_filter::OnVideoFormatLoaded(video_format* vf)
{
	mVideoOutputStatus.outX = vf->cx;
	mVideoOutputStatus.outY = vf->cy;
	mVideoOutputStatus.outAspectX = vf->aspectX;
	mVideoOutputStatus.outAspectY = vf->aspectY;
	mVideoOutputStatus.outFps = vf->fps;

	switch (vf->colourFormat)
	{
	case COLOUR_FORMAT_UNKNOWN:
		mVideoOutputStatus.outColourFormat = "?";
		break;
	case RGB:
		mVideoOutputStatus.outColourFormat = "RGB";
		break;
	case REC601:
		mVideoOutputStatus.outColourFormat = "REC601";
		break;
	case REC709:
		mVideoOutputStatus.outColourFormat = "REC709";
		break;
	case BT2020:
		mVideoOutputStatus.outColourFormat = "BT2020";
		break;
	case BT2020C:
		mVideoOutputStatus.outColourFormat = "BT2020C";
		break;
	case P3D65:
		mVideoOutputStatus.outColourFormat = "P3D65";
		break;
	}

	switch (vf->quantisation)
	{
	case QUANTISATION_UNKNOWN:
		mVideoOutputStatus.outQuantisation = "?";
		break;
	case QUANTISATION_LIMITED:
		mVideoOutputStatus.outQuantisation = "Limited";
		break;
	case QUANTISATION_FULL:
		mVideoOutputStatus.outQuantisation = "Full";
		break;
	}

	switch (vf->saturation)
	{
	case SATURATION_UNKNOWN:
		mVideoOutputStatus.outSaturation = "?";
		break;
	case SATURATION_LIMITED:
		mVideoOutputStatus.outSaturation = "Limited";
		break;
	case SATURATION_FULL:
		mVideoOutputStatus.outSaturation = "Full";
		break;
	case EXTENDED_GAMUT:
		mVideoOutputStatus.outSaturation = "Extended";
		break;
	}

	mVideoOutputStatus.outBitDepth = vf->pixelFormat.bitDepth;

	switch (vf->pixelFormat.subsampling)
	{
	case YUV_420:
		mVideoOutputStatus.outSubsampling = "YUV 4:2:0";
		break;
	case YUV_422:
		mVideoOutputStatus.outSubsampling = "YUV 4:2:2";
		break;
	case YUV_444:
		mVideoOutputStatus.outSubsampling = "YUV 4:4:4";
		break;
	case RGB_444:
		mVideoOutputStatus.outSubsampling = "RGB 4:4:4";
		break;
	}

	mVideoOutputStatus.outPixelStructure = vf->pixelFormat.name;
	switch (vf->hdrMeta.transferFunction)
	{
	case 4:
		mVideoOutputStatus.outTransferFunction = "REC.709";
		break;
	case 15:
		mVideoOutputStatus.outTransferFunction = "SMPTE ST 2084 (PQ)";
		break;
	case 16:
		mVideoOutputStatus.outTransferFunction = "HLG";
		break;
	default:
		mVideoOutputStatus.outTransferFunction = "?";
		break;
	}

	PublishVideoSignalMetrics();

	// what crosses the bus is the captured format, the DV tunnel is captured as RGB and decoded to P210 on the CPU
	const auto& busFormat = vf->dvTunnel ? vf->dvCaptureFormat : vf->pixelFormat;
	mDeviceStatus.bandwidthUsed = captureBytesPerSecond(busFormat, vf->cx, vf->cy, vf->fps);
	#ifndef NO_LOGGING
	if (mDeviceStatus.bandwidth > 0.0 && mDeviceStatus.bandwidthUsed > mDeviceStatus.bandwidth)
	{
		LOG_WARNING(mLogData.logger,
		            "[{}] Capturing {}x{}@{:.3f} {} needs {:.0f}MB/s, more than the estimated {:.0f}MB/s available, expect dropped frames",
		            mLogData.prefix, vf->cx, vf->cy, vf->fps, busFormat.name,
		            mDeviceStatus.bandwidthUsed / 1e6, mDeviceStatus.bandwidth / 1e6);
	}
	#endif

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mVideoOutputStatus);
		mInfoCallback->Reload(&mDeviceStatus);
	}
}

void capture_filter::OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light)
{
	if (hdr == nullptr)
	{
		mHdrStatus.hdrOn = false;
	}
	else
	{
		mHdrStatus.hdrOn = true;
		mHdrStatus.hdrPrimaryRX = hdr->display_primaries_x[2];
		mHdrStatus.hdrPrimaryRY = hdr->display_primaries_y[2];
		mHdrStatus.hdrPrimaryGX = hdr->display_primaries_x[0];
		mHdrStatus.hdrPrimaryGY = hdr->display_primaries_y[0];
		mHdrStatus.hdrPrimaryBX = hdr->display_primaries_x[1];
		mHdrStatus.hdrPrimaryBY = hdr->display_primaries_y[1];
		mHdrStatus.hdrWpX = hdr->white_point_x;
		mHdrStatus.hdrWpY = hdr->white_point_y;
		mHdrStatus.hdrMinDML = hdr->min_display_mastering_luminance;
		mHdrStatus.hdrMaxDML = hdr->max_display_mastering_luminance;
		mHdrStatus.hdrMaxCLL = light->MaxCLL;
		mHdrStatus.hdrMaxFALL = light->MaxFALL;
	}

	PublishVideoSignalMetrics();

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mHdrStatus);
	}
}

void capture_filter::OnAudioFormatLoaded(audio_format* af)
{
	mAudioOutputStatus.audioOutChannelLayout = af->channelLayout;
	mAudioOutputStatus.audioOutBitDepth = af->bitDepth;
	mAudioOutputStatus.audioOutCodec = codecNames[af->codec];
	mAudioOutputStatus.audioOutFs = af->fs;
	constexpr double epsilon = 1e-6;
	mAudioOutputStatus.audioOutLfeOffset = std::abs(af->lfeLevelAdjustment - unity) <= epsilon * std::abs(
		                                       af->lfeLevelAdjustment)
		                                       ? 0
		                                       : -10;
	if (af->lfeChannelIndex == not_present)
	{
		mAudioOutputStatus.audioOutLfeChannelIndex = -1;
	}
	else
	{
		mAudioOutputStatus.audioOutLfeChannelIndex = af->lfeChannelIndex + af->channelOffsets[af->lfeChannelIndex];
	}
	mAudioOutputStatus.audioOutChannelCount = af->outputChannelCount;
	mAudioOutputStatus.audioOutDataBurstSize = af->dataBurstSize;

	PublishAudioSignalMetrics();

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mAudioOutputStatus);
	}
}
