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
#ifndef VIDEO_CAPTURE_PIN_HEADER
#define VIDEO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define S_RECONNECTION_UNNECESSARY ((HRESULT)1024L)

#include "capture_pin.h"
#include "modeswitcher.h"
#include "lavfilters_side_data.h"
#include "bgr10_rgb48.h"
#include "r210_rgb48.h"
#include "uyvy_yv16.h"
#include "v210_p210.h"
#include "y210_p210.h"
#include "yuv2_yv16.h"
#include "yuy2_yv16.h"

/**
 * A stream of video flowing from the capture device to an output pin.
 */
class video_capture_pin : public capture_pin
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

		return capture_pin::SetMediaType(pmt);
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
	video_capture_pin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix,
	                  const VIDEO_FORMAT& pVideoFormat, pixel_format_fallbacks pFallbacks, device_type pType) :
		capture_pin(phr, pParent, pObjectName, pPinName, pLogPrefix, pType, true),
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

	virtual void OnChangeMediaType()
	{
		if (mHasSignal)
		{
			DoSwitchMode();
		}
	}

	virtual void DoSwitchMode() = 0;

	VIDEO_FORMAT mVideoFormat{};
	pixel_format mSignalledFormat{NA};
	pixel_format_fallbacks mFormatFallbacks{};
};

template <class F, typename VF>
class hdmi_video_capture_pin : public video_capture_pin
{
public:
	hdmi_video_capture_pin(HRESULT* phr, F* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix,
	                       VIDEO_FORMAT pVideoFormat, pixel_format_fallbacks pFallbacks, device_type pType)
		: video_capture_pin(phr, pParent, pObjectName, pPinName, pLogPrefix, pVideoFormat, pFallbacks, pType),
		  mFilter(pParent),
		  mRateSwitcher(pLogPrefix, [this](const mode_switch_result& result)
		  {
			  mFilter->OnModeUpdated(result);
		  })
	{
	}

	void UpdateFrameWriterStrategy()
	{
		auto search = mFormatFallbacks.find(mVideoFormat.pixelFormat);
		SetFrameWriterStrategy(search == mFormatFallbacks.end() ? STRAIGHT_THROUGH : search->second.second,
		                       mVideoFormat.pixelFormat);
	}

	void RecordLatency()
	{
		if (mFrameTs.recordTo(mFrameMetrics))
		{
			mFilter->RecordVideoFrameLatency(mFrameMetrics);
		}
	}

protected:
	F* mFilter;
	std::unique_ptr<IVideoFrameWriter<VF>> mFrameWriter;
	frame_writer_strategy mFrameWriterStrategy{UNKNOWN};
	AsyncModeSwitcher mRateSwitcher;

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

	void DoSwitchMode() override
	{
		bool enabled;
		mFilter->IsRefreshRateSwitchEnabled(&enabled);
		if (enabled)
		{
			auto target = mVideoFormat.CalcRefreshRate();

			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Triggering refresh rate change to {} Hz", mLogData.prefix, target);
			#endif

			mRateSwitcher.PutThreadMsg(REFRESH_RATE, target, nullptr);
		}

		mFilter->IsHdrProfileSwitchEnabled(&enabled);
		if (enabled)
		{
			auto profileId = mFilter->GetMCProfileId(mVideoFormat.hdrMeta.transferFunction != 4);
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Triggering MC JRVR profile change to {}", mLogData.prefix, profileId);
			#endif
			mRateSwitcher.PutThreadMsg(MC_PROFILE, profileId, nullptr);
		}
	}

	void UpdateDisplayStatus() override
	{
		auto values = mode_switch::GetDisplayStatus();
		const mode_switch_result res = {
			.request = REFRESH_RATE,
			.rateSwitch{
				.displayStatus = std::get<0>(values),
				.refreshRate = std::get<1>(values)
			}
		};
		mFilter->OnModeUpdated(res);
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

	void LogHdrMetaIfPresent(const VIDEO_FORMAT* newVideoFormat) const
	{
		#ifndef NO_QUILL
		logHdrMeta(newVideoFormat->hdrMeta, mVideoFormat.hdrMeta, mLogData);
		#endif
	}

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

#endif
