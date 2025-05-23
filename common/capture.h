﻿/*
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

#include "logging.h"
#include "signalinfo.h"
#include "ISpecifyPropertyPages2.h"
#include "lavfilters_side_data.h"
#include <dvdmedia.h>
#include <wmcodecdsp.h>
#include <cmath>

EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;
EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

#define BACKOFF Sleep(20)
#define SHORT_BACKOFF Sleep(1)

constexpr auto unity = 1.0;

inline bool diff(double x, double y)
{
	return fabs(x - y) > 0.000001;
}

inline void logHdrMeta(HDR_META newMeta, HDR_META oldMeta, log_data log)
{
	#ifndef NO_QUILL
	if (newMeta.exists)
	{
		bool logPrimaries;
		bool logWp;
		bool logMax;
		bool logTf;
		if (oldMeta.exists)
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
	}
	else
	{
		LOG_WARNING(log.logger,
		            "[{}] HDR InfoFrame parsing failure, values are present but no metadata exists",
		            log.prefix);
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
	STDMETHODIMP Reload() override = 0;
	STDMETHODIMP SetCallback(ISignalInfoCB* cb) override;

	//////////////////////////////////////////////////////////////////////////
	//  ISpecifyPropertyPages2
	//////////////////////////////////////////////////////////////////////////
	STDMETHODIMP GetPages(CAUUID* pPages) override;
	STDMETHODIMP CreatePage(const GUID& guid, IPropertyPage** ppPage) override;

	void OnDisplayUpdated(std::wstring status, int freq);
	void OnVideoFormatLoaded(VIDEO_FORMAT* vf);
	void OnAudioFormatLoaded(AUDIO_FORMAT* af);
	void OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light);

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
	HDR_STATUS mHdrStatus{};
	ISignalInfoCB* mInfoCallback = nullptr;
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
	void SetStartTime(LONGLONG streamStartTime);

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

protected:
	CapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix);

	virtual void DoThreadDestroy() = 0;
	virtual bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) = 0;
	HRESULT RenegotiateMediaType(const CMediaType* pmt, long newSize, boolean renegotiateOnQueryAccept);
	HRESULT HandleStreamStateChange(IMediaSample* pms);

	log_data mLogData{};
	CCritSec mCaptureCritSec;
	LONGLONG mFrameCounter{0};
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
};

/**
 * A stream of video flowing from the capture device to an output pin.
 */
class VideoCapturePin : public CapturePin
{
protected:
	VideoCapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix) :
		CapturePin(phr, pParent, pObjectName, pPinName, pLogPrefix)
	{
	}

	// CSourceStream
	HRESULT GetMediaType(CMediaType* pmt) override;
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


template <class F>
class HdmiVideoCapturePin : public VideoCapturePin
{
public:
	HdmiVideoCapturePin(HRESULT* phr, F* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix)
		: VideoCapturePin(phr, pParent, pObjectName, pPinName, pLogPrefix),
		  mFilter(pParent)
	{
	}

protected:
	F* mFilter;

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
			if (mVideoFormat.hdrMeta.exists)
			{
				// This can fail if you have a filter behind this which does not understand side data
				IMediaSideData* pMediaSideData = nullptr;
				if (SUCCEEDED(pms->QueryInterface(&pMediaSideData)))
				{
					#ifndef NO_QUILL
					LOG_TRACE_L1(mLogData.logger, "[{}] Updating HDR meta in frame {}, last update at {}",
					             mLogData.prefix,
					             mFrameCounter, mLastSentHdrMetaAt);
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
};

class MemAllocator final : public CMemAllocator
{
public:
	MemAllocator(__inout_opt LPUNKNOWN, __inout HRESULT*);
};
