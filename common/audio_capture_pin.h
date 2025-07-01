/**
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
#ifndef AUDIO_CAPTURE_PIN_HEADER
#define AUDIO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "capture_pin.h"
#include <wmcodecdsp.h>
// needed by all implementations to provide speaker layouts
#include <ks.h>
#include <ksmedia.h>

EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;

/**
  * A stream of audio flowing from the capture device to an output pin.
  */
class audio_capture_pin : public capture_pin
{
protected:
	audio_capture_pin(HRESULT* phr, CSource* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix, device_type pType) :
		capture_pin(phr, pParent, pObjectName, pPinName, pLogPrefix, pType, false)
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
class hdmi_audio_capture_pin : public audio_capture_pin
{
public:
	hdmi_audio_capture_pin(HRESULT* phr, F* pParent, LPCSTR pObjectName, LPCWSTR pPinName, std::string pLogPrefix, device_type pType)
		: audio_capture_pin(phr, pParent, pObjectName, pPinName, pLogPrefix, pType),
		  mFilter(pParent)
	{
	}

protected:
	F* mFilter;

	void GetReferenceTime(REFERENCE_TIME* rt) const override
	{
		mFilter->GetReferenceTime(rt);
	}

	void RecordLatency()
	{
		if (mFrameTs.recordTo(mFrameMetrics))
		{
			mFilter->RecordAudioFrameLatency(mFrameMetrics);
		}
	}
};

class MemAllocator final : public CMemAllocator
{
public:
	MemAllocator(__inout_opt LPUNKNOWN, __inout HRESULT*);
};

#endif
