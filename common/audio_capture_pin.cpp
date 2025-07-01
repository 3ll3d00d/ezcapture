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

#include "audio_capture_pin.h"

MemAllocator::MemAllocator(LPUNKNOWN pUnk, HRESULT* pHr) : CMemAllocator("MemAllocator", pUnk, pHr)
{
	// exists purely to allow for easy debugging of what is going on inside CMemAllocator
}

HRESULT audio_capture_pin::DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc)
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

void audio_capture_pin::AudioFormatToMediaType(CMediaType* pmt, audio_format* audioFormat)
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

bool audio_capture_pin::ShouldChangeMediaType(audio_format* newAudioFormat)
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

HRESULT audio_capture_pin::GetMediaType(CMediaType* pmt)
{
	AudioFormatToMediaType(pmt, &mAudioFormat);
	return NOERROR;
}

STDMETHODIMP audio_capture_pin::GetNumberOfCapabilities(int* piCount, int* piSize)
{
	*piCount = 1;
	*piSize = sizeof(AUDIO_STREAM_CONFIG_CAPS);
	return S_OK;
}

STDMETHODIMP audio_capture_pin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC)
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

HRESULT audio_capture_pin::InitAllocator(IMemAllocator** ppAllocator)
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
