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

#ifndef NO_QUILL
#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/RotatingFileSink.h"
#include <string_view>
#include <utility>
#endif // !NO_QUILL

#include "capture.h"
#include <DXVA.h>

#ifdef _DEBUG
#define MIN_LOG_LEVEL quill::LogLevel::TraceL3
#else
#define MIN_LOG_LEVEL quill::LogLevel::TraceL2
#endif

CaptureFilter::CaptureFilter(LPCTSTR pName, LPUNKNOWN punk, HRESULT* phr, CLSID clsid, std::string pLogPrefix) :
	CSource(pName, punk, clsid)
{
	mLogData.prefix = pLogPrefix;

	#ifndef NO_QUILL
	quill::BackendOptions bopt;
	bopt.thread_name = "QuillBackend_" + pLogPrefix;
	bopt.enable_yield_when_idle = true;
	bopt.sleep_duration = std::chrono::nanoseconds(0);
	quill::Backend::start(bopt);

	auto filterFileSink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
		(std::filesystem::temp_directory_path() / (pLogPrefix + ".log")).string(),
		[]()
		{
			quill::RotatingFileSinkConfig cfg;
			cfg.set_open_mode('a');
			cfg.set_max_backup_files(12);
			cfg.set_rotation_frequency_and_interval('H', 1);
			cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::DateAndTime);
			cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);
			return cfg;
		}(),
		quill::FileEventNotifier{});
	mLogData.logger =
		CustomFrontend::create_or_get_logger(std::string{filterLoggerName},
		                                     std::move(filterFileSink),
		                                     quill::PatternFormatterOptions{
			                                     "%(time) [%(thread_id)] %(short_source_location:<28) "
			                                     "LOG_%(log_level:<9) %(logger:<12) %(message)",
			                                     "%H:%M:%S.%Qns",
			                                     quill::Timezone::GmtTime
		                                     });
	auto audioLatencySink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
		(std::filesystem::temp_directory_path() / (pLogPrefix + "_audio_latency.csv")).string(),
		[]()
		{
			quill::RotatingFileSinkConfig cfg;
			cfg.set_open_mode('a');
			cfg.set_max_backup_files(3);
			cfg.set_rotation_time_daily("00:00");
			cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
			cfg.set_filename_append_option(quill::FilenameAppendOption::StartDate);
			return cfg;
		}(),
		quill::FileEventNotifier{});
	mLogData.audioLat =
		CustomFrontend::create_or_get_logger(std::string{audioLatencyLoggerName},
		                                     std::move(audioLatencySink),
		                                     quill::PatternFormatterOptions{
			                                     "%(time),%(thread_id),%(logger:<12),%(message)",
			                                     "%H:%M:%S.%Qns",
			                                     quill::Timezone::GmtTime
		                                     });
	auto videoLatencySink = CustomFrontend::create_or_get_sink<quill::RotatingFileSink>(
		(std::filesystem::temp_directory_path() / (pLogPrefix + "_video_latency.csv")).string(),
		[]()
		{
			quill::RotatingFileSinkConfig cfg;
			cfg.set_open_mode('a');
			cfg.set_max_backup_files(3);
			cfg.set_rotation_time_daily("00:00");
			cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
			cfg.set_filename_append_option(quill::FilenameAppendOption::StartDate);
			return cfg;
		}(),
		quill::FileEventNotifier{});
	mLogData.videoLat =
		CustomFrontend::create_or_get_logger(std::string{videoLatencyLoggerName},
		                                     std::move(videoLatencySink),
		                                     quill::PatternFormatterOptions{
			                                     "%(time),%(thread_id),%(logger:<12),%(message)",
			                                     "%H:%M:%S.%Qns",
			                                     quill::Timezone::GmtTime
		                                     });

	// printing absolutely everything we may ever log
	mLogData.logger->set_log_level(MIN_LOG_LEVEL);
	mLogData.audioLat->set_log_level(MIN_LOG_LEVEL);
	mLogData.videoLat->set_log_level(MIN_LOG_LEVEL);
	#endif // !NO_QUILL

	auto monitorConfig = GetAllSupportedRefreshRates();
	mRefreshRates = std::move(monitorConfig.refreshRates);
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Monitor {} supported {} ignored {}", mLogData.prefix,
	         monitorConfig.name, monitorConfig.supportedModes, monitorConfig.ignoredModes);
	#endif
}

STDMETHODIMP CaptureFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
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

void CaptureFilter::GetReferenceTime(REFERENCE_TIME* rt) const
{
	m_pClock->GetTime(rt);
}

HRESULT CaptureFilter::GetTime(REFERENCE_TIME* pTime)
{
	return mClock->GetTime(pTime);
}

HRESULT CaptureFilter::AdviseTime(REFERENCE_TIME baseTime, REFERENCE_TIME streamTime, HEVENT hEvent,
                                  DWORD_PTR* pdwAdviseCookie)
{
	return mClock->AdviseTime(baseTime, streamTime, hEvent, pdwAdviseCookie);
}

HRESULT CaptureFilter::AdvisePeriodic(REFERENCE_TIME startTime, REFERENCE_TIME periodTime,
                                      HSEMAPHORE hSemaphore, DWORD_PTR* pdwAdviseCookie)
{
	return mClock->AdvisePeriodic(startTime, periodTime, hSemaphore, pdwAdviseCookie);
}

HRESULT CaptureFilter::Unadvise(DWORD_PTR dwAdviseCookie)
{
	return mClock->Unadvise(dwAdviseCookie);
}

ULONG CaptureFilter::GetMiscFlags()
{
	return AM_FILTER_MISC_FLAGS_IS_SOURCE;
};

STDMETHODIMP CaptureFilter::GetState(DWORD dw, FILTER_STATE* pState)
{
	CBaseFilter::GetState(dw, pState);
	return *pState == State_Paused ? VFW_S_CANT_CUE : S_OK;
}

STDMETHODIMP CaptureFilter::SetSyncSource(IReferenceClock* pClock)
{
	CBaseFilter::SetSyncSource(pClock);
	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		if (stream != nullptr)
			stream->SetSyncSource(pClock);
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] pin {} is not a CBaseStreamControl!", mLogData.prefix, i);
			#endif
		}
	}
	return NOERROR;
}

HRESULT CaptureFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName)
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

STDMETHODIMP CaptureFilter::Run(REFERENCE_TIME tStart)
{
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Filter has started running at {}", mLogData.prefix, rt);
	#endif

	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<IAMTimeAware*>(m_paStreams[i]);
		stream->SetStartTime(rt);
		auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Running, tStart);
	}
	return CBaseFilter::Run(tStart);
}

STDMETHODIMP CaptureFilter::Pause()
{
	for (auto i = 0; i < m_iPins; i++)
	{
		auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Paused);
	}
	return CBaseFilter::Pause();
}

STDMETHODIMP CaptureFilter::Stop()
{
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Filter has stopped running at {}", mLogData.prefix, rt);
	#endif

	for (auto i = 0; i < m_iPins; i++)
	{
		auto stream = dynamic_cast<IAMTimeAware*>(m_paStreams[i]);
		stream->SetStopTime(rt);
		auto s1 = dynamic_cast<CBaseStreamControl*>(m_paStreams[i]);
		s1->NotifyFilterState(State_Stopped);
	}
	return CBaseFilter::Stop();
}

STDMETHODIMP CaptureFilter::GetPages(CAUUID* pPages)
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

STDMETHODIMP CaptureFilter::CreatePage(const GUID& guid, IPropertyPage** ppPage)
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

void CaptureFilter::OnDisplayUpdated(const std::wstring& status, int freq)
{
	mDisplayStatus.status = std::move(status);
	mDisplayStatus.freq = freq;
	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mDisplayStatus);
	}
}

void CaptureFilter::OnVideoFormatLoaded(VIDEO_FORMAT* vf)
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

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mVideoOutputStatus);
	}
}

void CaptureFilter::OnHdrUpdated(MediaSideDataHDR* hdr, MediaSideDataHDRContentLightLevel* light)
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

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mHdrStatus);
	}
}

void CaptureFilter::OnAudioFormatLoaded(AUDIO_FORMAT* af)
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

	if (mInfoCallback != nullptr)
	{
		mInfoCallback->Reload(&mAudioOutputStatus);
	}
}

//////////////////////////////////////////////////////////////////////////
// CapturePin
//////////////////////////////////////////////////////////////////////////
CapturePin::CapturePin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName,
                       const std::string& pLogPrefix) :
	CSourceStream(pObjectName, phr, pParent, pPinName),
	IAMTimeAware(pLogPrefix)
{
	mLogData.init(pLogPrefix);
}

HRESULT CapturePin::HandleStreamStateChange(IMediaSample* pms)
{
	const int iStreamState = CheckStreamState(pms);
	if (iStreamState == STREAM_FLOWING)
	{
		if (mLastSampleDiscarded)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] Recovery after sample discard, setting discontinuity", mLogData.prefix);
			#endif

			pms->SetDiscontinuity(TRUE);
			mLastSampleDiscarded = FALSE;
		}
		return S_OK;
	}
	else
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] Entering stream discard", mLogData.prefix);
		#endif

		mLastSampleDiscarded = TRUE;
		return S_FALSE;
	}
}

HRESULT CapturePin::OnThreadStartPlay()
{
	#ifndef NO_QUILL
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	PrintResolution(mLogData);

	if (IAmStopped())
	{
		LOG_WARNING(mLogData.logger, "[{}] Pin worker thread starting at {} but stream not started yet",
		            mLogData.prefix, rt);
	}
	else
	{
		LOG_WARNING(mLogData.logger, "[{}] Pin worker thread starting at {}, stream started at {}", mLogData.prefix, rt,
		            mStreamStartTime);
	}
	#endif
	return S_OK;
}

STDMETHODIMP CapturePin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER)

	if (riid == _uuidof(IAMStreamConfig))
	{
		return GetInterface(static_cast<IAMStreamConfig*>(this), ppv);
	}
	if (riid == _uuidof(IKsPropertySet))
	{
		return GetInterface(static_cast<IKsPropertySet*>(this), ppv);
	}
	if (riid == _uuidof(IAMStreamControl))
	{
		return GetInterface(static_cast<IAMStreamControl*>(this), ppv);
	}
	if (riid == _uuidof(IAMPushSource))
	{
		return GetInterface(static_cast<IAMPushSource*>(this), ppv);
	}
	return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

// largely a copy of CSourceStream but with logging replaced (as DbgLog to file seems to never ever work)
// and with better error handling to avoid visible freezes with no option but to restart
HRESULT CapturePin::DoBufferProcessingLoop(void)
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Entering DoBufferProcessingLoop", mLogData.prefix);
	#endif

	Command com;

	mFirst = true;

	OnThreadStartPlay();

	do
	{
		while (!CheckRequest(&com))
		{
			IMediaSample* pSample;

			HRESULT hrBuf = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
			if (FAILED(hrBuf) || hrBuf == S_FALSE)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Failed to GetDeliveryBuffer ({:#08x}), retrying", mLogData.prefix,
				            static_cast<unsigned long>(hrBuf));
				#endif
				SHORT_BACKOFF;
				continue;
			}

			mFirst = false;

			HRESULT hr = FillBuffer(pSample);

			if (hr == S_OK)
			{
				hr = Deliver(pSample);
				pSample->Release();

				if (hr != S_OK)
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] Failed to deliver sample downstream ({:#08x}), process loop will exit",
					            mLogData.prefix, static_cast<unsigned long>(hr));
					#endif

					return S_OK;
				}
			}
			else if (hr == S_FALSE)
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] Buffer not filled, retrying", mLogData.prefix);
				#endif
				pSample->Release();
			}
			else
			{
				#ifndef NO_QUILL
				LOG_WARNING(mLogData.logger, "[{}] FillBuffer failed ({:#08x}), sending EOS and EC_ERRORABORT",
				            mLogData.prefix, static_cast<unsigned long>(hr));
				#endif

				pSample->Release();
				DeliverEndOfStream();
				m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
				return hr;
			}

			// all paths release the sample
		}

		// For all commands sent to us there must be a Reply call!

		if (com == CMD_RUN || com == CMD_PAUSE)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] DoBufferProcessingLoop Replying to CMD {}", mLogData.prefix,
			         static_cast<int>(com));
			#endif
			Reply(NOERROR);
		}
		else if (com != CMD_STOP)
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] DoBufferProcessingLoop Replying to UNEXPECTED CMD {}", mLogData.prefix,
			          static_cast<int>(com));
			#endif
			Reply(static_cast<DWORD>(E_UNEXPECTED));
		}
		else
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] DoBufferProcessingLoop CMD_STOP will exit", mLogData.prefix);
			#endif
		}
	}
	while (com != CMD_STOP);

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] Exiting DoBufferProcessingLoop", mLogData.prefix);
	#endif
	return S_FALSE;
}

HRESULT CapturePin::OnThreadDestroy()
{
	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] >>> CapturePin::OnThreadDestroy", mLogData.prefix);
	#endif

	DoThreadDestroy();

	#ifndef NO_QUILL
	LOG_INFO(mLogData.logger, "[{}] <<< CapturePin::OnThreadDestroy", mLogData.prefix);
	#endif

	return S_OK;
}

HRESULT CapturePin::BeginFlush()
{
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::BeginFlush", mLogData.prefix);
	#endif

	this->Flushing(TRUE);
	return CSourceStream::BeginFlush();
}

HRESULT CapturePin::EndFlush()
{
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::EndFlush", mLogData.prefix);
	#endif

	this->Flushing(FALSE);
	return CSourceStream::EndFlush();
}

HRESULT CapturePin::Notify(IBaseFilter* pSelf, Quality q)
{
	// just to avoid use of DbgBreak in default implementation as we can't do anything about it given we are a slave to the device
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::Notify {}", mLogData.prefix, q.Type == 0 ? "Famine" : "Flood");
	#endif

	return S_OK;
}

// see https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-iamstreamconfig-setformat
STDMETHODIMP CapturePin::SetFormat(AM_MEDIA_TYPE* pmt)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetFormat ignored MT with GUID {}", mLogData.prefix,
	            pmt->majortype.Data1);
	#endif
	return VFW_E_INVALIDMEDIATYPE;
}

STDMETHODIMP CapturePin::GetFormat(AM_MEDIA_TYPE** ppmt)
{
	CMediaType cmt;
	GetMediaType(&cmt);
	*ppmt = CreateMediaType(&cmt);
	return S_OK;
}

HRESULT CapturePin::SetMediaType(const CMediaType* pmt)
{
	const HRESULT hr = CSourceStream::SetMediaType(pmt);

	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] CapturePin::SetMediaType ({:#08x})", mLogData.prefix,
	             static_cast<unsigned long>(hr));
	#endif

	return hr;
}

HRESULT CapturePin::DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties)
{
	CheckPointer(pIMemAlloc, E_POINTER)
	CheckPointer(pProperties, E_POINTER)
	CAutoLock cAutoLock(m_pFilter->pStateLock());
	HRESULT hr = NOERROR;
	auto acceptedUpstreamBufferCount = ProposeBuffers(pProperties);

	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::DecideBufferSize size: {} count: {} (from upstream? {})",
	             mLogData.prefix, pProperties->cbBuffer, pProperties->cBuffers, acceptedUpstreamBufferCount);
	#endif

	ALLOCATOR_PROPERTIES actual;
	hr = pIMemAlloc->SetProperties(pProperties, &actual);

	if (FAILED(hr))
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::DecideBufferSize failed to SetProperties result {:#08x}",
		            mLogData.prefix, static_cast<unsigned long>(hr));
		#endif

		return hr;
	}
	if (actual.cbBuffer < pProperties->cbBuffer)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::DecideBufferSize actual buffer is {} not {}", mLogData.prefix,
		            actual.cbBuffer, pProperties->cbBuffer);
		#endif

		return E_FAIL;
	}

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// CapturePin -> IKsPropertySet
//////////////////////////////////////////////////////////////////////////
HRESULT CapturePin::Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
                        DWORD cbInstanceData, void* pPropData, DWORD cbPropData)
{
	// Set: Cannot set any properties.
	return E_NOTIMPL;
}

// Get: Return the pin category (our only property). 
HRESULT CapturePin::Get(
	REFGUID guidPropSet, // Which property set.
	DWORD dwPropID, // Which property in that set.
	void* pInstanceData, // Instance data (ignore).
	DWORD cbInstanceData, // Size of the instance data (ignore).
	void* pPropData, // Buffer to receive the property data.
	DWORD cbPropData, // Size of the buffer.
	DWORD* pcbReturned // Return the size of the property.
)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	if (pPropData == nullptr && pcbReturned == nullptr) return E_POINTER;

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == nullptr) return S_OK; // Caller just wants to know the size. 
	if (cbPropData < sizeof(GUID)) return E_UNEXPECTED; // The buffer is too small.

	// declares the pin to a live source capture or preview pin
	*static_cast<GUID*>(pPropData) = mPreview ? PIN_CATEGORY_PREVIEW : PIN_CATEGORY_CAPTURE;
	return S_OK;
}

// QuerySupported: Query whether the pin supports the specified property.
HRESULT CapturePin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	// We support getting this property, but not setting it.
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

HRESULT CapturePin::RenegotiateMediaType(const CMediaType* pmt, long newSize, boolean renegotiateOnQueryAccept)
{
	auto timeout = 100;
	auto retVal = VFW_E_CHANGING_FORMAT;
	auto oldMediaType = m_mt;
	HRESULT hrQA = m_Connected->QueryAccept(pmt);

receiveconnection:

	HRESULT hr = m_Connected->ReceiveConnection(this, pmt);
	if (SUCCEEDED(hr))
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::RenegotiateMediaType ReceiveConnection succeeded [{:#08x}]",
		             mLogData.prefix, static_cast<unsigned long>(hr));
		#endif

		hr = SetMediaType(pmt);
		if (SUCCEEDED(hr))
		{
			retVal = S_OK;
		}
		else
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::RenegotiateMediaType SetMediaType failed [{:#08x}]",
			             mLogData.prefix, static_cast<unsigned long>(hr));
			#endif
		}
	}
	else if (hr == VFW_E_BUFFERS_OUTSTANDING && timeout != -1)
	{
		if (timeout > 0)
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::NegotiateMediaType Buffers outstanding, retrying in 10ms..",
			             mLogData.prefix);
			#endif

			BACKOFF;
			timeout -= 10;
		}
		else
		{
			#ifndef NO_QUILL
			LOG_TRACE_L1(
				mLogData.logger, "[{}] CapturePin::NegotiateMediaType Buffers outstanding, timeout reached, flushing..",
				mLogData.prefix);
			#endif

			DeliverBeginFlush();
			DeliverEndFlush();
			timeout = -1;
		}
		goto receiveconnection;
	}
	else if (hrQA == S_OK) // docs say check S_OK explicitly rather than use the SUCCEEDED macro
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::NegotiateMediaType QueryAccept accepted", mLogData.prefix);
		#endif

		hr = SetMediaType(pmt);
		if (SUCCEEDED(hr))
		{
			if (!renegotiateOnQueryAccept)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::NegotiateMediaType - No buffer change",
				             mLogData.prefix);
				#endif

				retVal = S_OK;
			}
			else if (nullptr != m_pInputPin)
			{
				ALLOCATOR_PROPERTIES props, actual, checkProps;
				m_pAllocator->GetProperties(&props);
				m_pAllocator->Decommit();
				props.cbBuffer = newSize;
				hr = m_pAllocator->SetProperties(&props, &actual);
				if (SUCCEEDED(hr))
				{
					hr = m_pAllocator->Commit();
					m_pAllocator->GetProperties(&checkProps);
					if (SUCCEEDED(hr))
					{
						if (checkProps.cbBuffer == props.cbBuffer && checkProps.cBuffers == props.cBuffers)
						{
							#ifndef NO_QUILL
							LOG_TRACE_L1(mLogData.logger, "[{}] Updated allocator to {} bytes {} buffers",
							             mLogData.prefix,
							             props.cbBuffer, props.cBuffers);
							#endif
							retVal = S_OK;
						}
						else
						{
							#ifndef NO_QUILL
							LOG_WARNING(
								mLogData.logger,
								"[{}] Allocator accepted update to {} bytes {} buffers but is {} bytes {} buffers",
								mLogData.prefix, props.cbBuffer, props.cBuffers, checkProps.cbBuffer,
								checkProps.cBuffers);
							#endif
						}
					}
					else
					{
						#ifndef NO_QUILL
						LOG_WARNING(mLogData.logger,
						            "[{}] Allocator did not accept update to {} bytes {} buffers [{:#08x}]",
						            mLogData.prefix, props.cbBuffer, props.cBuffers, static_cast<unsigned long>(hr));
						#endif
					}
				}
				else
				{
					#ifndef NO_QUILL
					LOG_WARNING(mLogData.logger,
					            "[{}] Allocator did not commit update to {} bytes {} buffers [{:#08x}]",
					            mLogData.prefix, props.cbBuffer, props.cBuffers, static_cast<unsigned long>(hr));
					#endif
				}
			}
		}
	}
	else
	{
		#ifndef NO_QUILL
		LOG_WARNING(
			mLogData.logger,
			"[{}] CapturePin::NegotiateMediaType Receive Connection failed (hr: {:#08x}); QueryAccept: {:#08x}",
			mLogData.prefix, static_cast<unsigned long>(hr), static_cast<unsigned long>(hrQA));
		#endif
	}
	if (retVal == S_OK)
	{
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::NegotiateMediaType succeeded", mLogData.prefix);
		#endif

		mUpdatedMediaType = true;
	}
	else
	{
		// reinstate the old formats otherwise we're stuck thinking we have the new format
		#ifndef NO_QUILL
		LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::NegotiateMediaType failed {:#08x}", mLogData.prefix,
		             static_cast<unsigned long>(retVal));
		#endif

		SetMediaType(&oldMediaType);
	}

	return retVal;
}

//////////////////////////////////////////////////////////////////////////
// VideoCapturePin
//////////////////////////////////////////////////////////////////////////
void VideoCapturePin::VideoFormatToMediaType(CMediaType* pmt, VIDEO_FORMAT* videoFormat) const
{
	auto pvi = reinterpret_cast<VIDEOINFOHEADER2*>(pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));
	ZeroMemory(pvi, sizeof(VIDEOINFOHEADER2));

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VIDEOINFO2);
	pmt->SetTemporalCompression(FALSE);
	pmt->SetSampleSize(videoFormat->imageSize);

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle
	pvi->dwBitRate = static_cast<DWORD>(videoFormat->imageSize * 8 * videoFormat->fps);
	pvi->dwBitErrorRate = 0;
	pvi->AvgTimePerFrame = static_cast<DWORD>(static_cast<double>(dshowTicksPerSecond) / videoFormat->fps);
	pvi->dwInterlaceFlags = 0;
	pvi->dwPictAspectRatioX = videoFormat->aspectX;
	pvi->dwPictAspectRatioY = videoFormat->aspectY;

	// dwControlFlags is a 32bit int. With AMCONTROL_COLORINFO_PRESENT the upper 24 bits are used by DXVA_ExtendedFormat.
	// That struct is 32 bits so it's lower member (SampleFormat) is actually overbooked with the value of dwConotrolFlags
	// so can't be used. LAV has defined some out-of-spec but compatible with madVR values for the more modern formats,
	// which we use as well see
	// https://github.com/Nevcairiel/LAVFilters/blob/ddef56ae155d436f4301346408f4fdba755197d6/decoder/LAVVideo/Media.cpp

	auto colorimetry = reinterpret_cast<DXVA_ExtendedFormat*>(&(pvi->dwControlFlags));
	// 1 = REC.709, 4 = BT.2020
	colorimetry->VideoTransferMatrix = videoFormat->colourFormat == BT2020
		                                   ? static_cast<DXVA_VideoTransferMatrix>(4)
		                                   : DXVA_VideoTransferMatrix_BT709;
	// 1 = REC.709, 9 = BT.2020
	colorimetry->VideoPrimaries = videoFormat->colourFormat == BT2020
		                              ? static_cast<DXVA_VideoPrimaries>(9)
		                              : DXVA_VideoPrimaries_BT709;
	// 4 = REC.709, 15 = SMPTE ST 2084 (PQ), 16 = HLG (JRVR only)
	colorimetry->VideoTransferFunction = static_cast<DXVA_VideoTransferFunction>(videoFormat->hdrMeta.transferFunction);
	// 0 = unknown, 1 = 0-255, 2 = 16-235
	colorimetry->NominalRange = static_cast<DXVA_NominalRange>(videoFormat->quantisation);

	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] DXVA_ExtendedFormat {} {} {} {}", mLogData.prefix,
	             static_cast<int>(colorimetry->VideoTransferMatrix),
	             static_cast<int>(colorimetry->VideoPrimaries), static_cast<int>(colorimetry->VideoTransferFunction),
	             static_cast<int>(colorimetry->NominalRange));
	#endif

	pvi->dwControlFlags += AMCONTROL_USED;
	pvi->dwControlFlags += AMCONTROL_COLORINFO_PRESENT;

	auto isRgb = videoFormat->pixelFormat.GetBiCompression() == BI_RGB;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = videoFormat->cx;
	pvi->bmiHeader.biHeight = isRgb && videoFormat->bottomUpDib ? -(videoFormat->cy) : videoFormat->cy;
	// RGB on windows is upside down
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biBitCount = videoFormat->pixelFormat.bitsPerPixel;
	pvi->bmiHeader.biCompression = videoFormat->pixelFormat.GetBiCompression();
	pvi->bmiHeader.biSizeImage = videoFormat->imageSize;
	pvi->bmiHeader.biXPelsPerMeter = 0;
	pvi->bmiHeader.biYPelsPerMeter = 0;
	pvi->bmiHeader.biClrUsed = 0;
	pvi->bmiHeader.biClrImportant = 0;

	// Work out the GUID for the subtype from the header info.
	const auto subTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
	pmt->SetSubtype(&subTypeGUID);
}

bool VideoCapturePin::ShouldChangeMediaType(VIDEO_FORMAT* newVideoFormat, bool pixelFallBackIsActive)
{
	auto reconnect = false;
	if (newVideoFormat->cx != mVideoFormat.cx || newVideoFormat->cy != mVideoFormat.cy)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video dimension change {}x{} to {}x{}",
		         mLogData.prefix, mVideoFormat.cx, mVideoFormat.cy,
		         newVideoFormat->cx, newVideoFormat->cy);
		#endif
	}
	if (newVideoFormat->aspectX != mVideoFormat.aspectX || newVideoFormat->aspectY != mVideoFormat.aspectY)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video AR change {}x{} to {}x{}",
		         mLogData.prefix, mVideoFormat.aspectX, mVideoFormat.aspectY,
		         newVideoFormat->aspectX, newVideoFormat->aspectY);
		#endif
	}
	if (abs(newVideoFormat->frameInterval - mVideoFormat.frameInterval) >= 100)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video FPS change {:.3f} to {:.3f}",
		         mLogData.prefix, mVideoFormat.fps, newVideoFormat->fps);
		#endif
	}
	if (mVideoFormat.pixelFormat.format != newVideoFormat->pixelFormat.format && !pixelFallBackIsActive)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video pixel format change {} to {}",
		         mLogData.prefix, mVideoFormat.pixelFormat.name, newVideoFormat->pixelFormat.name);
		#endif
	}
	if (mVideoFormat.colourFormat != newVideoFormat->colourFormat)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video colour format change {} to {}",
		         mLogData.prefix, mVideoFormat.colourFormatName, newVideoFormat->colourFormatName);
		#endif
	}
	if (mVideoFormat.quantisation != newVideoFormat->quantisation || mVideoFormat.saturation != newVideoFormat->
		saturation)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video colorimetry change quant {} to {} sat {} to {}",
		         mLogData.prefix,
		         static_cast<int>(mVideoFormat.quantisation), static_cast<int>(newVideoFormat->quantisation),
		         static_cast<int>(mVideoFormat.saturation), static_cast<int>(newVideoFormat->saturation)
		);
		#endif
	}
	auto incomingTransferFunction = newVideoFormat->hdrMeta.transferFunction;
	if (mVideoFormat.hdrMeta.transferFunction != newVideoFormat->hdrMeta.transferFunction)
	{
		reconnect = true;

		#ifndef NO_QUILL
		auto formatFrom = mVideoFormat.hdrMeta.transferFunction == 0
			                  ? "?"
			                  : mVideoFormat.hdrMeta.transferFunction == 4
			                  ? "REC.709"
			                  : mVideoFormat.hdrMeta.transferFunction == 15
			                  ? "SMPTE ST 2084 (PQ)"
			                  : "HLG";
		auto formatTo = incomingTransferFunction == 0
			                ? "?"
			                : incomingTransferFunction == 4
			                ? "REC.709"
			                : incomingTransferFunction == 15
			                ? "SMPTE ST 2084 (PQ)"
			                : "HLG";
		LOG_INFO(mLogData.logger, "[{}] Video transfer function change {} ({}) to {} ({})",
		         mLogData.prefix, formatFrom, mVideoFormat.hdrMeta.transferFunction, formatTo,
		         incomingTransferFunction);
		#endif
	}

	return reconnect;
}

HRESULT VideoCapturePin::DoChangeMediaType(const CMediaType* pNewMt, const VIDEO_FORMAT* newVideoFormat)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger,
	            "[{}] Proposing new video format {} x {} ({}:{}) @ {:.3f} Hz in {} bits ({} {} tf: {}) size {} bytes",
	            mLogData.prefix, newVideoFormat->cx, newVideoFormat->cy, newVideoFormat->aspectX,
	            newVideoFormat->aspectY, newVideoFormat->fps, newVideoFormat->pixelFormat.bitDepth,
	            newVideoFormat->pixelFormat.name, newVideoFormat->colourFormatName,
	            newVideoFormat->hdrMeta.transferFunction, newVideoFormat->imageSize);

	if (mLogData.logger->should_log_statement(quill::LogLevel::TraceL3))
	{
		AM_MEDIA_TYPE currentMt;
		if (SUCCEEDED(m_Connected->ConnectionMediaType(&currentMt)))
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] MT,lSampleSize,biBitCount,biWidth,biHeight,biSizeImage",
			             mLogData.prefix);

			auto currentHeader = reinterpret_cast<VIDEOINFOHEADER2*>(currentMt.pbFormat);
			auto currentBmi = currentHeader->bmiHeader;

			LOG_TRACE_L3(mLogData.logger, "[{}] current,{},{},{},{},{}", mLogData.prefix, currentMt.lSampleSize,
			             currentBmi.biBitCount, currentBmi.biWidth, currentBmi.biHeight, currentBmi.biSizeImage);

			auto newHeader = reinterpret_cast<VIDEOINFOHEADER2*>(pNewMt->pbFormat);
			auto newBmi = newHeader->bmiHeader;

			LOG_TRACE_L3(mLogData.logger, "[{}] proposed,{},{},{},{},{}", mLogData.prefix, pNewMt->lSampleSize,
			             newBmi.biBitCount, newBmi.biWidth, newBmi.biHeight, newBmi.biSizeImage);

			FreeMediaType(currentMt);
		}
	}
	#endif

	auto retVal = RenegotiateMediaType(pNewMt, newVideoFormat->imageSize,
	                                   newVideoFormat->imageSize != mVideoFormat.imageSize);
	if (retVal == S_OK)
	{
		mVideoFormat = *newVideoFormat;
		OnChangeMediaType();
	}

	return retVal;
}

STDMETHODIMP VideoCapturePin::GetNumberOfCapabilities(int* piCount, int* piSize)
{
	*piCount = 1;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

STDMETHODIMP VideoCapturePin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC)
{
	if (iIndex > 0)
	{
		return S_FALSE;
	}
	if (iIndex < 0)
	{
		return E_INVALIDARG;
	}
	CMediaType cmt;
	GetMediaType(0, &cmt);
	*pmt = CreateMediaType(&cmt);

	auto pvi = reinterpret_cast<VIDEOINFOHEADER2*>((*pmt)->pbFormat);

	auto pvscc = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);

	pvscc->guid = FORMAT_VideoInfo2;
	pvscc->VideoStandard = AnalogVideo_PAL_D;
	pvscc->InputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->InputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MinCroppingSize.cx = 80;
	pvscc->MinCroppingSize.cy = 60;
	pvscc->MaxCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->CropGranularityX = 80;
	pvscc->CropGranularityY = 60;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = 80;
	pvscc->MinOutputSize.cy = 60;
	pvscc->MaxOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MaxFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MinBitsPerSecond = pvi->dwBitRate;
	pvscc->MaxBitsPerSecond = pvi->dwBitRate;

	return S_OK;
}

bool VideoCapturePin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
{
	pProperties->cbBuffer = mVideoFormat.imageSize;
	if (pProperties->cBuffers < 1)
	{
		// 1 works for mpc-vr, 16 works for madVR so go with that as a default if the input pin doesn't suggest a number.
		pProperties->cBuffers = 16;
		return false;
	}
	return true;
}


//////////////////////////////////////////////////////////////////////////
// MemAllocator 
//////////////////////////////////////////////////////////////////////////
MemAllocator::MemAllocator(LPUNKNOWN pUnk, HRESULT* pHr) : CMemAllocator("MemAllocator", pUnk, pHr)
{
	// exists purely to allow for easy debugging of what is going on inside CMemAllocator
}

//////////////////////////////////////////////////////////////////////////
// AudioCapturePin 
//////////////////////////////////////////////////////////////////////////
HRESULT AudioCapturePin::DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc)
{
	// copied from CBaseOutputPin but preferring to use our own allocator first

	HRESULT hr = NOERROR;
	*ppAlloc = nullptr;

	ALLOCATOR_PROPERTIES prop;
	ZeroMemory(&prop, sizeof(prop));

	pPin->GetAllocatorRequirements(&prop);
	if (prop.cbAlign == 0)
	{
		prop.cbAlign = 1;
	}

	/* Try the allocator provided by the output pin. */
	hr = InitAllocator(ppAlloc);
	if (SUCCEEDED(hr))
	{
		hr = DecideBufferSize(*ppAlloc, &prop);
		if (SUCCEEDED(hr))
		{
			hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
			if (SUCCEEDED(hr))
			{
				return NOERROR;
			}
		}
	}

	if (*ppAlloc)
	{
		(*ppAlloc)->Release();
		*ppAlloc = nullptr;
	}

	/* Try the allocator provided by the input pin */
	hr = pPin->GetAllocator(ppAlloc);
	if (SUCCEEDED(hr))
	{
		hr = DecideBufferSize(*ppAlloc, &prop);
		if (SUCCEEDED(hr))
		{
			hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
			if (SUCCEEDED(hr))
			{
				return NOERROR;
			}
		}
	}

	if (*ppAlloc)
	{
		(*ppAlloc)->Release();
		*ppAlloc = nullptr;
	}

	return hr;
}

void AudioCapturePin::AudioFormatToMediaType(CMediaType* pmt, AUDIO_FORMAT* audioFormat)
{
	// based on https://github.com/Nevcairiel/LAVFilters/blob/81c5676cb99d0acfb1457b8165a0becf5601cae3/decoder/LAVAudio/LAVAudio.cpp#L1186
	pmt->majortype = MEDIATYPE_Audio;
	pmt->formattype = FORMAT_WaveFormatEx;

	if (audioFormat->codec == PCM)
	{
		// LAVAudio compatible big endian PCM
		if (audioFormat->bitDepthInBytes == 3)
		{
			pmt->subtype = MEDIASUBTYPE_PCM_IN24;
		}
		else if (audioFormat->bitDepthInBytes == 4)
		{
			pmt->subtype = MEDIASUBTYPE_PCM_IN32;
		}
		else
		{
			pmt->subtype = MEDIASUBTYPE_PCM_SOWT;
		}

		WAVEFORMATEXTENSIBLE wfex;
		memset(&wfex, 0, sizeof(wfex));

		WAVEFORMATEX* wfe = &wfex.Format;
		wfe->wFormatTag = static_cast<WORD>(pmt->subtype.Data1);
		wfe->nChannels = audioFormat->outputChannelCount;
		wfe->nSamplesPerSec = audioFormat->fs;
		wfe->wBitsPerSample = audioFormat->bitDepth;
		wfe->nBlockAlign = wfe->nChannels * wfe->wBitsPerSample / 8;
		wfe->nAvgBytesPerSec = wfe->nSamplesPerSec * wfe->nBlockAlign;

		if (audioFormat->outputChannelCount > 2 || wfe->wBitsPerSample > 16 || wfe->nSamplesPerSec > 48000)
		{
			wfex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
			wfex.Format.cbSize = sizeof(wfex) - sizeof(wfex.Format);
			wfex.dwChannelMask = audioFormat->channelMask;
			wfex.Samples.wValidBitsPerSample = wfex.Format.wBitsPerSample;
			wfex.SubFormat = pmt->subtype;
		}
		pmt->SetSampleSize(wfe->wBitsPerSample * wfe->nChannels / 8);
		pmt->SetFormat(reinterpret_cast<BYTE*>(&wfex), sizeof(wfex.Format) + wfex.Format.cbSize);
	}
	else
	{
		// working assumption is that LAVAudio is downstream so use a format it supports
		// https://learn.microsoft.com/en-us/windows/win32/coreaudio/representing-formats-for-iec-61937-transmissions
		WAVEFORMATEXTENSIBLE_IEC61937 wf_iec61937;
		memset(&wf_iec61937, 0, sizeof(wf_iec61937));
		WAVEFORMATEXTENSIBLE* wf = &wf_iec61937.FormatExt;
		wf->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;

		switch (audioFormat->codec)
		{
		case AC3:
			pmt->subtype = MEDIASUBTYPE_DOLBY_AC3;
			wf->Format.nChannels = 2; // 1 IEC 60958 Line.
			wf->dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
			wf->SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;
			wf_iec61937.dwEncodedChannelCount = 6;
			wf->Format.nSamplesPerSec = 48000;
			break;
		case EAC3:
			pmt->subtype = MEDIASUBTYPE_DOLBY_DDPLUS;
			wf->Format.nChannels = 2; // 1 IEC 60958 Line.
			wf->dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
			wf->SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS;
			wf_iec61937.dwEncodedChannelCount = 6;
			wf->Format.nSamplesPerSec = 192000;
			break;
		case DTS:
			pmt->subtype = MEDIASUBTYPE_DTS;
			wf->Format.nChannels = 2; // 1 IEC 60958 Lines.
			wf->dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
			wf->SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DTS;
			wf_iec61937.dwEncodedChannelCount = 6;
			wf->Format.nSamplesPerSec = 48000;
			break;
		case DTSHD:
			pmt->subtype = MEDIASUBTYPE_DTS_HD;
			wf->Format.nChannels = 8; // 4 IEC 60958 Lines.
			wf->dwChannelMask = KSAUDIO_SPEAKER_7POINT1;
			wf->SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD;
			wf_iec61937.dwEncodedChannelCount = 8;
			wf->Format.nSamplesPerSec = 192000;
			break;
		case TRUEHD:
			pmt->subtype = MEDIASUBTYPE_DOLBY_TRUEHD;
			wf->Format.nChannels = 8; // 4 IEC 60958 Lines.
			wf->dwChannelMask = KSAUDIO_SPEAKER_7POINT1;
			wf->SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP;
			wf_iec61937.dwEncodedChannelCount = 8;
			wf->Format.nSamplesPerSec = 192000;
			break;
		case BITSTREAM:
		case PCM:
		case PAUSE_OR_NULL:
			// should never get here
			break;
		}
		wf_iec61937.dwEncodedSamplesPerSec = 48000;
		wf_iec61937.dwAverageBytesPerSec = 0;
		wf->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wf->Format.wBitsPerSample = 16;
		wf->Samples.wValidBitsPerSample = 16;
		wf->Format.nBlockAlign = wf->Format.wBitsPerSample / 8 * wf->Format.nChannels;
		wf->Format.nAvgBytesPerSec = wf->Format.nSamplesPerSec * wf->Format.nBlockAlign;
		wf->Format.cbSize = sizeof(wf_iec61937) - sizeof(wf->Format);

		pmt->SetSampleSize(wf->Format.nBlockAlign);
		pmt->SetFormat(reinterpret_cast<BYTE*>(&wf_iec61937), sizeof(wf_iec61937) + wf->Format.cbSize);
	}
}

bool AudioCapturePin::ShouldChangeMediaType(AUDIO_FORMAT* newAudioFormat)
{
	auto reconnect = false;
	if (mAudioFormat.inputChannelCount != newAudioFormat->inputChannelCount)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Input channel count change {} to {}", mLogData.prefix,
		         mAudioFormat.inputChannelCount,
		         newAudioFormat->inputChannelCount);
		#endif
	}
	if (mAudioFormat.outputChannelCount != newAudioFormat->outputChannelCount)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Output channel count change {} to {}", mLogData.prefix,
		         mAudioFormat.outputChannelCount,
		         newAudioFormat->outputChannelCount);
		#endif
	}
	if (mAudioFormat.bitDepthInBytes != newAudioFormat->bitDepthInBytes)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Bit depth change {} to {}", mLogData.prefix, mAudioFormat.bitDepthInBytes,
		         newAudioFormat->bitDepthInBytes);
		#endif
	}
	if (mAudioFormat.fs != newAudioFormat->fs)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Fs change {} to {}", mLogData.prefix, mAudioFormat.fs, newAudioFormat->fs);
		#endif
	}
	if (mAudioFormat.codec != newAudioFormat->codec)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Codec change {} to {}", mLogData.prefix, codecNames[mAudioFormat.codec],
		         codecNames[newAudioFormat->codec]);
		#endif
	}
	if (mAudioFormat.channelAllocation != newAudioFormat->channelAllocation)
	{
		reconnect = true;
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Channel allocation change {} to {}", mLogData.prefix,
		         mAudioFormat.channelAllocation,
		         newAudioFormat->channelAllocation);
		#endif
	}
	if (mAudioFormat.codec != PCM && newAudioFormat->codec != PCM && mAudioFormat.dataBurstSize != newAudioFormat->
		dataBurstSize)
	{
		reconnect = true;
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Bitstream databurst change {} to {}", mLogData.prefix,
		         mAudioFormat.dataBurstSize,
		         newAudioFormat->dataBurstSize);
		#endif
	}
	return reconnect;
}

HRESULT AudioCapturePin::GetMediaType(CMediaType* pmt)
{
	AudioFormatToMediaType(pmt, &mAudioFormat);
	return NOERROR;
}

STDMETHODIMP AudioCapturePin::GetNumberOfCapabilities(int* piCount, int* piSize)
{
	*piCount = 1;
	*piSize = sizeof(AUDIO_STREAM_CONFIG_CAPS);
	return S_OK;
}

STDMETHODIMP AudioCapturePin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC)
{
	if (iIndex > 0)
	{
		return S_FALSE;
	}
	if (iIndex < 0)
	{
		return E_INVALIDARG;
	}
	CMediaType cmt;
	GetMediaType(&cmt);
	*pmt = CreateMediaType(&cmt);

	auto pascc = reinterpret_cast<AUDIO_STREAM_CONFIG_CAPS*>(pSCC);
	pascc->guid = FORMAT_WaveFormatEx;
	pascc->MinimumChannels = mAudioFormat.outputChannelCount;
	pascc->MaximumChannels = mAudioFormat.outputChannelCount;
	pascc->ChannelsGranularity = 1;
	pascc->MinimumBitsPerSample = mAudioFormat.bitDepth;
	pascc->MaximumBitsPerSample = mAudioFormat.bitDepth;
	pascc->BitsPerSampleGranularity = 1;
	pascc->MinimumSampleFrequency = mAudioFormat.fs;
	pascc->MaximumSampleFrequency = mAudioFormat.fs;
	pascc->SampleFrequencyGranularity = 1;

	return S_OK;
}

HRESULT AudioCapturePin::InitAllocator(IMemAllocator** ppAllocator)
{
	HRESULT hr = S_OK;
	auto pAlloc = new MemAllocator(nullptr, &hr);
	if (!pAlloc)
	{
		return E_OUTOFMEMORY;
	}

	if (FAILED(hr))
	{
		delete pAlloc;
		return hr;
	}

	return pAlloc->QueryInterface(IID_IMemAllocator, reinterpret_cast<void**>(ppAllocator));
}
