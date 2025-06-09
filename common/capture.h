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

#include <string>
#include <utility>

#include "metric.h"
#include "logging.h"
#include "signalinfo.h"
#include "VideoFrameWriter.h"
#include "ISpecifyPropertyPages2.h"
#include "lavfilters_side_data.h"
#include <dvdmedia.h>
#include <wmcodecdsp.h>
#include <cmath>
#include <memory>

#include "bgr10_rgb48.h"
#include "r210_rgb48.h"
#include "uyvy_yv16.h"
#include "v210_p210.h"
#include "y210_p210.h"
#include "yuv2_yv16.h"
#include "yuy2_yv16.h"

EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;
EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

#define BACKOFF Sleep(20)
#define SHORT_BACKOFF Sleep(1)
#define S_RECONNECTION_UNNECESSARY ((HRESULT)1024L)

constexpr auto unity = 1.0;

inline bool diff(double x, double y)
{
	return fabs(x - y) > 0.000001;
}

inline void logBitmapHeader(const log_data& log, const std::string& desc, const BITMAPINFOHEADER* bmi)
{
	#ifndef NO_QUILL
	bool rgb = bmi->biCompression == 0;
	// use transform if we ever have c++23 in msvc
	auto pix = rgb ? std::nullopt : findByFourCC(bmi->biCompression);
	if (pix.has_value() || rgb)
	{
		auto name = rgb ? "RGB" : pix->name;
		LOG_WARNING(log.logger, "[{}] {} {},{},{},{},{}", log.prefix, desc, bmi->biBitCount, bmi->biWidth,
		            bmi->biHeight, bmi->biSizeImage, name);
	}
	else
	{
		LOG_WARNING(log.logger, "[{}] {} {},{},{},{},{:#08x}", log.prefix, desc, bmi->biBitCount, bmi->biWidth,
		            bmi->biHeight, bmi->biSizeImage, bmi->biCompression);
	}
	#endif
}

inline void logVideoMediaType(const log_data& log, const std::string& desc, const AM_MEDIA_TYPE* pmt)
{
	bool matched = false;
	if (IsEqualGUID(pmt->majortype, MEDIATYPE_Video) == TRUE)
	{
		if (IsEqualGUID(pmt->formattype, FORMAT_VIDEOINFO2) == TRUE)
		{
			#ifndef NO_QUILL
			auto header = reinterpret_cast<VIDEOINFOHEADER2*>(pmt->pbFormat);
			auto bmi = header->bmiHeader;
			logBitmapHeader(log, desc + " VIH2", &bmi);
			#endif

			matched = true;
		}
		else if (IsEqualGUID(pmt->formattype, FORMAT_VideoInfo) == TRUE)
		{
			#ifndef NO_QUILL
			auto header = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
			auto bmi = header->bmiHeader;
			logBitmapHeader(log, desc + " VIH", &bmi);
			#endif

			matched = true;
		}
	}

	#ifndef NO_QUILL
	if (!matched)
	{
		LOG_WARNING(log.logger, "[{}] {} ignored unknown type {:#08x} - {:#08x}", log.prefix, desc,
		            pmt->majortype.Data1, pmt->formattype.Data1);
	}
	#endif
}

inline void doLogHdrMeta(const HDR_META& newMeta, const log_data& log, bool logPrimaries, bool logWp, bool logMax, bool logTf)
{
	#ifndef NO_QUILL
	if (logPrimaries)
	{
		LOG_INFO(log.logger, "[{}] Primaries RGB {:.4f} x {:.4f} {:.4f} x {:.4f} {:.4f} x {:.4f}",
		         log.prefix,
		         newMeta.r_primary_x, newMeta.r_primary_y, newMeta.g_primary_x, newMeta.g_primary_y,
		         newMeta.b_primary_x, newMeta.b_primary_y);
	}
	if (logWp)
	{
		LOG_INFO(log.logger, "[{}] Whitepoint {:.4f} x {:.4f}", log.prefix,
		         newMeta.whitepoint_x, newMeta.whitepoint_y);
	}
	if (logMax)
	{
		LOG_INFO(log.logger, "[{}] DML/MaxCLL/MaxFALL {:.4f} / {:.4f} {} {}", log.prefix,
		         newMeta.minDML, newMeta.maxDML, newMeta.maxCLL, newMeta.maxFALL);
	}
	if (logTf)
	{
		LOG_INFO(log.logger, "[{}] Transfer Function {}", log.prefix, newMeta.transferFunction);
	}
	#endif
}

inline void logHdrMeta(const HDR_META& newMeta, const HDR_META& oldMeta, const log_data& log)
{
	#ifndef NO_QUILL
	if (newMeta.exists())
	{
		bool logPrimaries;
		bool logWp;
		bool logMax;
		bool logTf;
		if (oldMeta.exists())
		{
			logPrimaries =
				diff(newMeta.r_primary_x, oldMeta.r_primary_x)
				|| diff(newMeta.r_primary_y, oldMeta.r_primary_y)
				|| diff(newMeta.g_primary_x, oldMeta.g_primary_x)
				|| diff(newMeta.g_primary_y, oldMeta.g_primary_y)
				|| diff(newMeta.b_primary_x, oldMeta.b_primary_x)
				|| diff(newMeta.b_primary_y, oldMeta.b_primary_y);
			logWp = diff(newMeta.whitepoint_x, oldMeta.whitepoint_x)
				|| diff(newMeta.whitepoint_y, oldMeta.whitepoint_y);
			logMax =
				diff(newMeta.maxCLL, oldMeta.maxCLL)
				|| diff(newMeta.minDML, oldMeta.minDML)
				|| diff(newMeta.maxDML, oldMeta.maxDML)
				|| newMeta.maxFALL != oldMeta.maxFALL;
			logTf = diff(newMeta.transferFunction, oldMeta.transferFunction);
			if (logPrimaries || logWp || logMax || logTf)
			{
				LOG_INFO(log.logger, "[{}] HDR metadata has changed", log.prefix);
			}
		}
		else
		{
			logPrimaries = true;
			logWp = true;
			logMax = true;
			logTf = true;
			LOG_INFO(log.logger, "[{}] HDR metadata is now present", log.prefix);
		}

		doLogHdrMeta(newMeta, log, logPrimaries, logWp, logMax, logTf);
	}
	else
	{
		LOG_WARNING(log.logger,
		            "[{}] HDR InfoFrame parsing failure, values are present but no metadata exists",
		            log.prefix);
		doLogHdrMeta(newMeta, log, true, true, true, false);
	}
	#endif
}

inline std::tuple<std::wstring, int> GetDisplayStatus()
{
	HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);

	MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
	DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

	if (GetMonitorInfo(activeMonitor, &monitorInfo)
		&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
	{
		auto width = devMode.dmPelsWidth;
		auto height = devMode.dmPelsHeight;
		auto freq = devMode.dmDisplayFrequency;
		auto status = std::wstring{monitorInfo.szDevice};
		status += L" " + std::to_wstring(width) + L" x " + std::to_wstring(height) + L" @ " + std::to_wstring(freq) +
			L" Hz";
		return {status, freq};
	}
	return {L"", 0};
}

inline HRESULT PrintResolution(const log_data& ld)
{
	HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);

	MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
	DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

	if (GetMonitorInfo(activeMonitor, &monitorInfo)
		&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
	{
		auto width = devMode.dmPelsWidth;
		auto height = devMode.dmPelsHeight;
		auto freq = devMode.dmDisplayFrequency;
		#ifndef NO_QUILL
		LOG_INFO(ld.logger, "[{}] Current monitor = {} {} x {} @ {} Hz", ld.prefix,
		         std::wstring{ monitorInfo.szDevice },
		         width, height, freq);
		#endif
		return S_OK;
	}
	return E_FAIL;
}

inline HRESULT ChangeResolution(const log_data& ld, DWORD targetRefreshRate)
{
	HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);
	MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
	DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

	if (GetMonitorInfo(activeMonitor, &monitorInfo)
		&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
	{
		auto width = devMode.dmPelsWidth;
		auto height = devMode.dmPelsHeight;
		auto freq = devMode.dmDisplayFrequency;
		if (freq == targetRefreshRate)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L2(ld.logger, "[{}] No change requested from {} {} x {} @ {} Hz", ld.prefix,
			             std::wstring{ monitorInfo.szDevice }, width, height, freq);
			#endif
			return S_OK;
		}

		#ifndef NO_QUILL
		LOG_INFO(ld.logger, "[{}] Requesting change from {} {} x {} @ {} Hz to {} Hz", ld.prefix,
		         std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate);
		#endif

		devMode.dmDisplayFrequency = targetRefreshRate;

		auto res = ChangeDisplaySettings(&devMode, 0);
		switch (res)
		{
		case DISP_CHANGE_SUCCESSFUL:
			#ifndef NO_QUILL
			LOG_INFO(ld.logger, "[{}] Completed change from {} {} x {} @ {} Hz to {} Hz", ld.prefix,
			         std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate);
			#endif
			return S_OK;
		default:
			#ifndef NO_QUILL
			LOG_INFO(ld.logger, "[{}] Failed to change from = {} {} x {} @ {} Hz to {} Hz due to {}", ld.prefix,
			         std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate, res);
			#endif
			return E_FAIL;
		}
	}
	return E_FAIL;
}

// Non template parts of the filter impl
class CaptureFilter :
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
			mInfoCallback->ReloadA(&mAudioCaptureLatencyStatus);
			return S_OK;
		}
		return E_FAIL;
	}

	STDMETHODIMP SetCallback(ISignalInfoCB* cb) override
	{
		mInfoCallback = cb;
		return S_OK;
	}

	//////////////////////////////////////////////////////////////////////////
	//  ISpecifyPropertyPages2
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP GetPages(CAUUID* pPages) override;
	STDMETHODIMP CreatePage(const GUID& guid, IPropertyPage** ppPage) override;

	void OnDisplayUpdated(std::wstring status, int freq);
	void OnVideoFormatLoaded(VIDEO_FORMAT* vf);
	void OnAudioFormatLoaded(AUDIO_FORMAT* af);
	void OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light);

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

	void OnAudioCaptureLatencyUpdated(const metric& metric)
	{
		CaptureLatency(metric, mAudioCaptureLatencyStatus, "Audio");
		if (mInfoCallback != nullptr)
		{
			mInfoCallback->ReloadA(&mAudioCaptureLatencyStatus);
		}
	}

protected:
	CaptureFilter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, std::string pLogPrefix);

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
	CAPTURE_LATENCY mAudioCaptureLatencyStatus{};
	HDR_STATUS mHdrStatus{};
	ISignalInfoCB* mInfoCallback = nullptr;

private:
	void CaptureLatency(const metric& metric, CAPTURE_LATENCY& lat, const std::string& desc)
	{
		lat.min = metric.min();
		lat.max = metric.max();
		lat.mean = metric.mean();

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
class HdmiCaptureFilter : public CaptureFilter
{
public:
	// Callbacks to update the prop page data
	virtual void OnVideoSignalLoaded(V_SIG* vs) = 0;
	virtual void OnAudioSignalLoaded(A_SIG* as) = 0;
	virtual void OnDeviceUpdated() = 0;

protected:
	HdmiCaptureFilter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, std::string logPrefix) :
		CaptureFilter(pName, punk, phr, clsid, logPrefix)
	{
	}

	D_INF mDeviceInfo{};
};

class IAMTimeAware
{
public:
	void SetStartTime(LONGLONG streamStartTime)
	{
		mStreamStartTime = streamStartTime;

		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetStartTime at {}", mLogData.prefix, streamStartTime);
		#endif
	}

	virtual void SetStopTime(LONGLONG streamStopTime)
	{
		mStreamStopTime = streamStopTime;

		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetStopTime at {}", mLogData.prefix, streamStopTime);
		#endif
	}

	boolean IAmStarted()
	{
		return mStreamStartTime > mStreamStopTime;
	}

	boolean IAmStopped()
	{
		return !IAmStarted();
	}

protected:
	IAMTimeAware(std::string pLogPrefix, const std::string& pLoggerName)
	{
		mLogData.prefix = std::move(pLogPrefix);
		#ifndef NO_QUILL
		mLogData.logger = CustomFrontend::get_logger(pLoggerName);
		#endif
	}

	log_data mLogData{};
	LONGLONG mStreamStartTime{-1LL};
	LONGLONG mStreamStopTime{-1LL};
};


/**
 * A stream of audio or video flowing from the capture device to an output pin.
 */
class CapturePin :
	public CSourceStream,
	public IAMStreamConfig,
	public IKsPropertySet,
	public IAMPushSource,
	public CBaseStreamControl,
	public IAMTimeAware
{
public:
	DECLARE_IUNKNOWN;

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

	virtual void GetReferenceTime(REFERENCE_TIME* rt) const = 0;

	//////////////////////////////////////////////////////////////////////////
	//  IPin
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP BeginFlush(void) override;
	STDMETHODIMP EndFlush(void) override;

	//////////////////////////////////////////////////////////////////////////
	//  IQualityControl
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE Notify(IBaseFilter* pSelf, Quality q) override;

	//////////////////////////////////////////////////////////////////////////
	//  IAMStreamConfig
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* pmt) override;
	HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** ppmt) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties) override;
	HRESULT SetMediaType(const CMediaType* pmt) override;
	HRESULT OnThreadDestroy() override;
	HRESULT OnThreadStartPlay() override;
	HRESULT DoBufferProcessingLoop() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseStreamControl
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	//  IKsPropertySet
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData, DWORD cbInstanceData,
	                              void* pPropData, DWORD cbPropData) override;
	HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, DWORD dwPropID, void* pInstanceData, DWORD cbInstanceData,
	                              void* pPropData, DWORD cbPropData, DWORD* pcbReturned) override;
	HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) override;

	//////////////////////////////////////////////////////////////////////////
	//  IAMPushSource
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetPushSourceFlags(ULONG* pFlags) override
	{
		*pFlags = 0;
		return S_OK;
	}

	HRESULT GetLatency(REFERENCE_TIME* prtLatency) override { return E_NOTIMPL; }
	HRESULT SetPushSourceFlags(ULONG Flags) override { return E_NOTIMPL; }
	HRESULT SetStreamOffset(REFERENCE_TIME rtOffset) override { return E_NOTIMPL; }
	HRESULT GetStreamOffset(REFERENCE_TIME* prtOffset) override { return E_NOTIMPL; }
	HRESULT GetMaxStreamOffset(REFERENCE_TIME* prtMaxOffset) override { return E_NOTIMPL; }
	HRESULT SetMaxStreamOffset(REFERENCE_TIME rtMaxOffset) override { return E_NOTIMPL; }

	void ResizeMetrics(double expectedRefreshRatePerSecond)
	{
		// aim for metrics to update approx once every 1500ms
		auto newSize = std::lrint(expectedRefreshRatePerSecond * 3 / 2);
		mConversionLatency.resize(newSize);
		mCaptureLatency.resize(newSize);
	}

protected:
	CapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix);

	virtual void DoThreadDestroy() = 0;
	virtual bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) = 0;
	HRESULT RenegotiateMediaType(const CMediaType* pmt, long newSize, boolean renegotiateOnQueryAccept);
	HRESULT HandleStreamStateChange(IMediaSample* pms);

	log_data mLogData{};
	CCritSec mCaptureCritSec;
	uint64_t mFrameCounter{0};
	bool mPreview{false};
	WORD mSinceLast{0};

	bool mFirst{true};
	bool mLastSampleDiscarded{false};
	bool mUpdatedMediaType{false};
	bool mHasSignal{false};
	LONGLONG mLastSentHdrMetaAt{0};
	// per frame
	LONGLONG mPreviousFrameTime{0};
	LONGLONG mCurrentFrameTime{0};
	// measurements
	metric mCaptureLatency{};
	metric mConversionLatency{};
};

/**
 * A stream of video flowing from the capture device to an output pin.
 */
class VideoCapturePin : public CapturePin
{
public:
	STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override
	{
		#ifndef NO_QUILL
		logVideoMediaType(mLogData, "VideoCapturePin::SetFormat", pmt);
		#endif

		return VFW_E_INVALIDMEDIATYPE;
	}

	HRESULT SetMediaType(const CMediaType* pmt) override
	{
		#ifndef NO_QUILL
		logVideoMediaType(mLogData, "VideoCapturePin::SetMediaType", pmt);
		#endif

		return CapturePin::SetMediaType(pmt);
	}

	HRESULT CheckMediaType(const CMediaType* pmt) override
	{
		#ifndef NO_QUILL
		logVideoMediaType(mLogData, "VideoCapturePin::CheckMediaType", pmt);
		#endif

		CAutoLock lock(m_pFilter->pStateLock());

		auto hr = E_FAIL;
		auto idx = 0;
		CMediaType mt;
		if (S_OK == GetMediaType(idx++, &mt))
		{
			if (mt == *pmt)
			{
				hr = S_OK;
			}
			else if (S_OK == GetMediaType(idx, &mt))
			{
				if (mt == *pmt)
				{
					hr = S_OK;
				}
			}
		}

		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] CapturePin::CheckMediaType (idx: {}, res: {:#08x}, sz: {})",
		             mLogData.prefix, idx, static_cast<unsigned long>(hr), pmt->GetSampleSize());
		#endif

		return hr;
	}

protected:
	VideoCapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix,
	                const VIDEO_FORMAT& pVideoFormat, pixel_format_fallbacks pFallbacks) :
		CapturePin(phr, pParent, pObjectName, pPinName, std::move(pLogPrefix)),
		mVideoFormat(pVideoFormat),
		mSignalledFormat(pVideoFormat.pixelFormat),
		mFormatFallbacks(std::move(pFallbacks))
	{
	}

	// CSourceStream
	HRESULT GetMediaType(int iPosition, __inout CMediaType* pMediaType) override
	{
		CAutoLock lock(m_pFilter->pStateLock());
		if (iPosition < 0)
		{
			return E_INVALIDARG;
		}
		switch (iPosition)
		{
		case 0:
			VideoFormatToMediaType(pMediaType, &mVideoFormat);
			return S_OK;
		case 1:
			const auto s = mFormatFallbacks.find(mSignalledFormat);
			if (s == mFormatFallbacks.end())
			{
				return VFW_S_NO_MORE_ITEMS;
			}
			auto fallbackPixelFormat = s->second.first;
			auto fallbackVideoFormat = mVideoFormat;
			fallbackVideoFormat.pixelFormat = std::move(fallbackPixelFormat);
			fallbackVideoFormat.CalculateDimensions();
			VideoFormatToMediaType(pMediaType, &fallbackVideoFormat);
			return S_OK;
		}
		return VFW_S_NO_MORE_ITEMS;
	}

	// IAMStreamConfig
	HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize) override;
	HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC) override;
	// CapturePin
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;

	void VideoFormatToMediaType(CMediaType* pmt, VIDEO_FORMAT* videoFormat) const;
	bool ShouldChangeMediaType(VIDEO_FORMAT* newVideoFormat, bool pixelFallBackIsActive = false);
	HRESULT DoChangeMediaType(const CMediaType* pNewMt, const VIDEO_FORMAT* newVideoFormat);
	virtual void UpdateDisplayStatus() = 0;

	long CalcRefreshRate() const
	{
		auto fps = mVideoFormat.fps;
		auto refreshRate = std::lround(fps - 0.49); // 23.976 will become 23, 24 will become 24 etc
		return refreshRate;
	}

	virtual void OnChangeMediaType()
	{
		if (mHasSignal)
		{
			ChangeResolution(mLogData, CalcRefreshRate());
			UpdateDisplayStatus();
		}
	}

	VIDEO_FORMAT mVideoFormat{};
	pixel_format mSignalledFormat{NA};
	pixel_format_fallbacks mFormatFallbacks{};
};

/**
 * A stream of audio flowing from the capture device to an output pin.
 */
class AudioCapturePin : public CapturePin
{
protected:
	AudioCapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix) :
		CapturePin(phr, pParent, pObjectName, pPinName, pLogPrefix)
	{
	}

	static void AudioFormatToMediaType(CMediaType* pmt, AUDIO_FORMAT* audioFormat);

	bool ShouldChangeMediaType(AUDIO_FORMAT* newAudioFormat);

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetMediaType(CMediaType* pmt) override;
	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT DecideAllocator(IMemInputPin* pPin, __deref_out IMemAllocator** pAlloc) override;
	HRESULT InitAllocator(__deref_out IMemAllocator** ppAlloc) override;
	//////////////////////////////////////////////////////////////////////////
	//  IAMStreamConfig
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize) override;
	HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC) override;

	AUDIO_FORMAT mAudioFormat{};
};


template <class F, typename VF>
class HdmiVideoCapturePin : public VideoCapturePin
{
public:
	HdmiVideoCapturePin(HRESULT* phr, F* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix,
	                    VIDEO_FORMAT pVideoFormat, pixel_format_fallbacks pFallbacks)
		: VideoCapturePin(phr, pParent, pObjectName, pPinName, pLogPrefix, pVideoFormat, pFallbacks),
		  mFilter(pParent)
	{
	}

	void UpdateFrameWriterStrategy()
	{
		auto search = mFormatFallbacks.find(mVideoFormat.pixelFormat);
		SetFrameWriterStrategy(search == mFormatFallbacks.end() ? STRAIGHT_THROUGH : search->second.second,
		                       mVideoFormat.pixelFormat);
	}

	void RecordLatency(uint64_t conv, uint64_t cap)
	{
		if (mConversionLatency.sample(conv))
		{
			mFilter->OnVideoConversionLatencyUpdated(mConversionLatency);
		}
		if (mCaptureLatency.sample(cap))
		{
			mFilter->OnVideoCaptureLatencyUpdated(mCaptureLatency);
		}
	}

protected:
	F* mFilter;
	std::unique_ptr<IVideoFrameWriter<VF>> mFrameWriter;
	frame_writer_strategy mFrameWriterStrategy{UNKNOWN};

	virtual void OnFrameWriterStrategyUpdated()
	{
		switch (mFrameWriterStrategy)
		{
		case YUV2_YV16:
			mFrameWriter = std::make_unique<yuv2_yv16<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case V210_P210:
			mFrameWriter = std::make_unique<v210_p210<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case R210_BGR48:
			mFrameWriter = std::make_unique<r210_rgb48<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case BGR10_BGR48:
			mFrameWriter = std::make_unique<bgr10_rgb48<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case Y210_P210:
			mFrameWriter = std::make_unique<y210_p210<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case YUY2_YV16:
			mFrameWriter = std::make_unique<yuy2_yv16<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case UYVY_YV16:
			mFrameWriter = std::make_unique<uyvy_yv16<VF>>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		default:
			// ugly back to workaround inability of c++ to call pure virtual function
			;
		}
	}

	void SetFrameWriterStrategy(const frame_writer_strategy newStrategy, const pixel_format& signalledFormat)
	{
		mSignalledFormat = signalledFormat;

		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] Updating conversion strategy from {} to {}", mLogData.prefix,
		             to_string(mFrameWriterStrategy), to_string(newStrategy));
		#endif
		mFrameWriterStrategy = newStrategy;

		OnFrameWriterStrategyUpdated();
	}

	boolean IsFallbackActive(const VIDEO_FORMAT* newVideoFormat) const
	{
		if (newVideoFormat->pixelFormat.format != mVideoFormat.pixelFormat.format)
		{
			auto search = mFormatFallbacks.find(newVideoFormat->pixelFormat);
			if (search != mFormatFallbacks.end())
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

	void UpdateDisplayStatus() override
	{
		auto values = GetDisplayStatus();
		mFilter->OnDisplayUpdated(std::get<0>(values), std::get<1>(values));
	}

	void GetReferenceTime(REFERENCE_TIME* rt) const override
	{
		mFilter->GetReferenceTime(rt);
	}

	void AppendHdrSideDataIfNecessary(IMediaSample* pms, long long endTime)
	{
		// Update once per second at most
		if (endTime > mLastSentHdrMetaAt + dshowTicksPerSecond)
		{
			mLastSentHdrMetaAt = endTime;
			if (mVideoFormat.hdrMeta.exists())
			{
				// This can fail if you have a filter behind this which does not understand side data
				IMediaSideData* pMediaSideData = nullptr;
				if (SUCCEEDED(pms->QueryInterface(&pMediaSideData)))
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Updating HDR meta in frame {}, last update at {}",
					             mLogData.prefix, mFrameCounter, mLastSentHdrMetaAt);
					#endif

					MediaSideDataHDR hdr;
					ZeroMemory(&hdr, sizeof(hdr));

					hdr.display_primaries_x[0] = mVideoFormat.hdrMeta.g_primary_x;
					hdr.display_primaries_x[1] = mVideoFormat.hdrMeta.b_primary_x;
					hdr.display_primaries_x[2] = mVideoFormat.hdrMeta.r_primary_x;
					hdr.display_primaries_y[0] = mVideoFormat.hdrMeta.g_primary_y;
					hdr.display_primaries_y[1] = mVideoFormat.hdrMeta.b_primary_y;
					hdr.display_primaries_y[2] = mVideoFormat.hdrMeta.r_primary_y;

					hdr.white_point_x = mVideoFormat.hdrMeta.whitepoint_x;
					hdr.white_point_y = mVideoFormat.hdrMeta.whitepoint_y;

					hdr.max_display_mastering_luminance = mVideoFormat.hdrMeta.maxDML;
					hdr.min_display_mastering_luminance = mVideoFormat.hdrMeta.minDML;

					pMediaSideData->SetSideData(IID_MediaSideDataHDR, reinterpret_cast<const BYTE*>(&hdr),
					                            sizeof(hdr));

					MediaSideDataHDRContentLightLevel hdrLightLevel;
					ZeroMemory(&hdrLightLevel, sizeof(hdrLightLevel));

					hdrLightLevel.MaxCLL = mVideoFormat.hdrMeta.maxCLL;
					hdrLightLevel.MaxFALL = mVideoFormat.hdrMeta.maxFALL;

					pMediaSideData->SetSideData(IID_MediaSideDataHDRContentLightLevel,
					                            reinterpret_cast<const BYTE*>(&hdrLightLevel),
					                            sizeof(hdrLightLevel));
					pMediaSideData->Release();

					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: R {:.4f} {:.4f}", mLogData.prefix,
					             hdr.display_primaries_x[2], hdr.display_primaries_y[2]);
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: G {:.4f} {:.4f}", mLogData.prefix,
					             hdr.display_primaries_x[0], hdr.display_primaries_y[0]);
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: B {:.4f} {:.4f}", mLogData.prefix,
					             hdr.display_primaries_x[1], hdr.display_primaries_y[1]);
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: W {:.4f} {:.4f}", mLogData.prefix,
					             hdr.white_point_x, hdr.white_point_y);
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: DML {} {}", mLogData.prefix,
					             hdr.min_display_mastering_luminance, hdr.max_display_mastering_luminance);
					LOG_TRACE_L1(mLogData.logger, "[{}] HDR meta: MaxCLL/MaxFALL {} {}", mLogData.prefix,
					             hdrLightLevel.MaxCLL, hdrLightLevel.MaxFALL);
					#endif

					mFilter->OnHdrUpdated(&hdr, &hdrLightLevel);
				}
				else
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] HDR meta to send via MediaSideDataHDR but not supported by MediaSample",
					            mLogData.prefix);
					#endif
				}
			}
			else
			{
				mFilter->OnHdrUpdated(nullptr, nullptr);
			}
		}
	}

	virtual void LogHdrMetaIfPresent(const VIDEO_FORMAT* newVideoFormat) = 0;

	HRESULT OnVideoSignal(VIDEO_FORMAT newVideoFormat)
	{
		auto retVal = S_RECONNECTION_UNNECESSARY;

		#ifndef NO_QUILL
		LogHdrMetaIfPresent(&newVideoFormat);
		#endif

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
				retVal = S_OK;
				SetFrameWriterStrategy(STRAIGHT_THROUGH, signalledFormat);
			}
			else
			{
				auto search = mFormatFallbacks.find(newVideoFormat.pixelFormat);
				if (search != mFormatFallbacks.end())
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

						SetFrameWriterStrategy(search->second.second, signalledFormat);

						retVal = S_OK;
					}
					else
					{
						#ifndef NO_QUILL
						LOG_WARNING(mLogData.logger,
						            "[{}] VideoFormat changed but fallback format also not able to reconnect! Will retry after backoff [Result: {:#08x}]",
						            mLogData.prefix, static_cast<unsigned long>(hr),
						            fallbackVideoFormat.pixelFormat.name);
						#endif

						retVal = E_FAIL;
					}
				}
				else
				{
					#ifndef NO_QUILL
					LOG_ERROR(mLogData.logger,
					          "[{}] VideoFormat changed but not able to reconnect! Will retry after backoff [Result: {:#08x}]",
					          mLogData.prefix, static_cast<unsigned long>(hr));
					#endif

					retVal = E_FAIL;
				}
			}
			if (reconnected) mFilter->OnVideoFormatLoaded(&mVideoFormat);
		}
		return retVal;
	}
};

template <class F>
class HdmiAudioCapturePin : public AudioCapturePin
{
public:
	HdmiAudioCapturePin(HRESULT* phr, F* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix)
		: AudioCapturePin(phr, pParent, pObjectName, pPinName, pLogPrefix),
		  mFilter(pParent)
	{
	}

protected:
	F* mFilter;

	void GetReferenceTime(REFERENCE_TIME* rt) const override
	{
		mFilter->GetReferenceTime(rt);
	}

	void RecordLatency(uint64_t cap)
	{
		if (mCaptureLatency.sample(cap))
		{
			mFilter->OnAudioCaptureLatencyUpdated(mCaptureLatency);
		}
	}
};

class MemAllocator final : public CMemAllocator
{
public:
	MemAllocator(__inout_opt LPUNKNOWN, __inout HRESULT*);
};
