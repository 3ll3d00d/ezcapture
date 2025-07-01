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
#ifndef CAPTURE_PIN_HEADER
#define CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <streams.h>

#include "metric.h"
#include "logging.h"
#include "domain.h"
#include "runtime_aware.h"

#include <dvdmedia.h>
#include <cmath>
#include <optional>

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

inline void doLogHdrMeta(const HDR_META& newMeta, const log_data& log, bool logPrimaries, bool logWp, bool logMax,
	bool logTf)
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
	else if (oldMeta.exists())
	{
		LOG_INFO(log.logger, "[{}] HDR metadata has been removed", log.prefix);
	}
	#endif
}

/**
 * A stream of audio or video flowing from the capture device to an output pin.
 */
class capture_pin :
	public CSourceStream,
	public IAMStreamConfig,
	public IKsPropertySet,
	public IAMPushSource,
	public CBaseStreamControl,
	public runtime_aware
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
		mAllocatorLatency.resize(newSize);
	}

protected:
	capture_pin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, const std::string& pLogPrefix);

	virtual void DoThreadDestroy() = 0;
	virtual bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) = 0;
	HRESULT RenegotiateMediaType(const CMediaType* pmt, long newSize, boolean renegotiateOnQueryAccept);
	HRESULT HandleStreamStateChange(IMediaSample* pms);

	log_data mLogData{};
	CCritSec mCaptureCritSec;
	uint64_t mFrameCounter{ 0 };
	bool mPreview{ false };
	WORD mSinceLast{ 0 };

	bool mFirst{ true };
	bool mLastSampleDiscarded{ false };
	bool mUpdatedMediaType{ false };
	bool mHasSignal{ false };
	LONGLONG mLastSentHdrMetaAt{ 0 };
	// per frame
	LONGLONG mPreviousFrameTime{ 0 };
	LONGLONG mCurrentFrameTime{ 0 };
	// measurements
	bool mLoggedLatencyHeader{ false };
	metric mCaptureLatency{};
	metric mConversionLatency{};
	metric mAllocatorLatency{};

	frame_metrics mFrameMetrics{};
};

#endif