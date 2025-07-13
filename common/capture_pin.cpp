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

#include "capture_pin.h"
#include "modeswitcher.h"

capture_pin::capture_pin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName,
                         const std::string& pLogPrefix, device_type pType, bool pVideo) :
	CSourceStream(pObjectName, phr, pParent, pPinName),
	runtime_aware(pLogPrefix, pType, pVideo)
{
	mLogData.init(pLogPrefix);
}

HRESULT capture_pin::HandleStreamStateChange(IMediaSample* pms)
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

HRESULT capture_pin::OnThreadStartPlay()
{
	#ifndef NO_QUILL
	REFERENCE_TIME rt;
	GetReferenceTime(&rt);

	mode_switch::PrintResolution(mLogData);

	if (IsStreamStopped())
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

STDMETHODIMP capture_pin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
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
HRESULT capture_pin::DoBufferProcessingLoop(void)
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
			mFrameTs.reset();
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

HRESULT capture_pin::BumpThreadPriority()
{
	if (!SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL))
	{
		#ifndef NO_QUILL
		LOG_ERROR(mLogData.logger, "[{}] Failed to set thread priority [{:#08x}]", mLogData.prefix, GetLastError());
		#endif
		return E_FAIL;
	}
	else
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Set thread priority to TIME_CRITICAL", mLogData.prefix);
		#endif
		return S_OK;
	}
}

HRESULT capture_pin::OnThreadDestroy()
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

HRESULT capture_pin::BeginFlush()
{
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::BeginFlush", mLogData.prefix);
	#endif

	this->Flushing(TRUE);
	return CSourceStream::BeginFlush();
}

HRESULT capture_pin::EndFlush()
{
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::EndFlush", mLogData.prefix);
	#endif

	this->Flushing(FALSE);
	return CSourceStream::EndFlush();
}

HRESULT capture_pin::Notify(IBaseFilter* pSelf, Quality q)
{
	// just to avoid use of DbgBreak in default implementation as we can't do anything about it given we are a slave to the device
	#ifndef NO_QUILL
	LOG_TRACE_L1(mLogData.logger, "[{}] CapturePin::Notify {}", mLogData.prefix, q.Type == 0 ? "Famine" : "Flood");
	#endif

	return S_OK;
}

// see https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-iamstreamconfig-setformat
STDMETHODIMP capture_pin::SetFormat(AM_MEDIA_TYPE* pmt)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetFormat ignored MT with GUID {}", mLogData.prefix,
	            pmt->majortype.Data1);
	#endif
	return VFW_E_INVALIDMEDIATYPE;
}

STDMETHODIMP capture_pin::GetFormat(AM_MEDIA_TYPE** ppmt)
{
	CMediaType cmt;
	GetMediaType(&cmt);
	*ppmt = CreateMediaType(&cmt);
	return S_OK;
}

HRESULT capture_pin::SetMediaType(const CMediaType* pmt)
{
	const HRESULT hr = CSourceStream::SetMediaType(pmt);

	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] CapturePin::SetMediaType ({:#08x})", mLogData.prefix,
	             static_cast<unsigned long>(hr));
	#endif

	return hr;
}

HRESULT capture_pin::DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties)
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
HRESULT capture_pin::Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
                         DWORD cbInstanceData, void* pPropData, DWORD cbPropData)
{
	// Set: Cannot set any properties.
	return E_NOTIMPL;
}

// Get: Return the pin category (our only property). 
HRESULT capture_pin::Get(
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
HRESULT capture_pin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	// We support getting this property, but not setting it.
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

HRESULT capture_pin::RenegotiateMediaType(const CMediaType* pmt, long newSize, boolean renegotiateOnQueryAccept)
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
