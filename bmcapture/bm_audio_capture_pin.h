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
#ifndef BM_AUDIO_CAPTURE_PIN_HEADER
#define BM_AUDIO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "bm_capture_filter.h"
#include "audio_capture_pin.h"

EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;

class blackmagic_audio_capture_pin final :
	public hdmi_audio_capture_pin<blackmagic_capture_filter>
{
public:
	blackmagic_audio_capture_pin(HRESULT* phr, blackmagic_capture_filter* pParent, bool pPreview);
	~blackmagic_audio_capture_pin() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
		__in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT OnThreadCreate() override;
	HRESULT FillBuffer(IMediaSample* pms) override;

protected:
	HRESULT DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat);
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
	void DoThreadDestroy() override;

	std::shared_ptr<audio_frame> mCurrentFrame;
};
#endif
