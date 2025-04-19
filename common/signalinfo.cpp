/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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

#include "version.h"
#include "signalinfo.h"
#include "commctrl.h"

#include <corecrt_wstdio.h>

#include "resource.h"

#define CHANNEL_VALID_TO_BINARY_PATTERN L"%c%c%c%c"
#define CHANNEL_VALID_TO_BINARY(val)  \
  ((val) & (0x01 << 3) ? '1' : '0'), \
  ((val) & (0x01 << 2) ? '1' : '0'), \
  ((val) & (0x01 << 1) ? '1' : '0'), \
  ((val) & (0x01 << 0) ? '1' : '0')

CUnknown* CSignalInfoProp::CreateInstance(LPUNKNOWN punk, HRESULT* phr)
{
	auto pNewObject = new CSignalInfoProp(punk, phr);

	if (pNewObject == nullptr)
	{
		if (phr)
		{
			*phr = E_OUTOFMEMORY;
		}
	}

	return pNewObject;
}

CSignalInfoProp::CSignalInfoProp(LPUNKNOWN pUnk, HRESULT* phr)
	: CBasePropertyPage("SignalInfoProp", pUnk, IDD_PROPPAGE_SIGNAL_INFO, IDS_TITLE)
{
}

CSignalInfoProp::~CSignalInfoProp() = default;


HRESULT CSignalInfoProp::OnActivate()
{
	INITCOMMONCONTROLSEX icc;
	icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icc.dwICC = ICC_BAR_CLASSES;
	if (InitCommonControlsEx(&icc) == FALSE)
	{
		return E_FAIL;
	}
	ASSERT(mSignalInfo != nullptr);

	auto version = L"v" EZ_VERSION_STR;
	SendDlgItemMessage(m_Dlg, IDC_SIGNAL_STATUS_FOOTER, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(version));
	return mSignalInfo->Reload();
}

HRESULT CSignalInfoProp::OnConnect(IUnknown* pUnk)
{
	if (pUnk == nullptr)
	{
		return E_POINTER;
	}
	ASSERT(mSignalInfo == nullptr);
	HRESULT hr = pUnk->QueryInterface(&mSignalInfo);
	if (SUCCEEDED(hr))
	{
		mSignalInfo->SetCallback(this);
	}
	return hr;
}

HRESULT CSignalInfoProp::OnDisconnect()
{
	if (mSignalInfo)
	{
		mSignalInfo->SetCallback(nullptr);
		mSignalInfo->Release();
		mSignalInfo = nullptr;
	}
	return S_OK;
}

HRESULT CSignalInfoProp::OnApplyChanges()
{
	return CBasePropertyPage::OnApplyChanges();
}

INT_PTR CSignalInfoProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CSignalInfoProp::Reload(AUDIO_INPUT_STATUS* payload)
{
	WCHAR buffer[28];
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->audioInStatus ? "LOCKED" : "NONE");
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_SIGNAL_STATUS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->audioInIsPcm ? "Y" : "N");
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_PCM, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d bit", payload->audioInBitDepth);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_BIT_DEPTH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, CHANNEL_VALID_TO_BINARY_PATTERN, CHANNEL_VALID_TO_BINARY(payload->audioInChannelPairs));
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_CH_MASK, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%#04x", payload->audioInChannelMap);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_CH_MAP, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d Hz", payload->audioInFs);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_FS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%#02x", payload->audioInLfeLevel);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_IN_LFE_LEVEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(AUDIO_OUTPUT_STATUS* payload)
{
	WCHAR buffer[28];
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->audioOutCodec.c_str());
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_CODEC, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d bit", payload->audioOutBitDepth);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_BIT_DEPTH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->audioOutChannelCount);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_CH_COUNT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->audioOutChannelLayout.c_str());
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_CH_LAYOUT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d Hz", payload->audioOutFs);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_FS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%d dB", payload->audioOutLfeOffset);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_LFE_LEVEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	if (payload->audioOutLfeChannelIndex == -1)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"No LFE");
	}
	else
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->audioOutLfeChannelIndex);
	}
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_LFE_CH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	if (payload->audioOutCodec == "PCM")
	{
		_snwprintf_s(buffer, _TRUNCATE, L"N/A");
	}
	else
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->audioOutDataBurstSize);
	}
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_OUT_BURST_SZ, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(VIDEO_INPUT_STATUS* payload)
{
	WCHAR buffer[28];
	_snwprintf_s(buffer, _TRUNCATE, L"%d x %d (%d:%d) %d bit", payload->inX, payload->inY, payload->inAspectX, payload->inAspectY, payload->inBitDepth);
	SendDlgItemMessage(m_Dlg, IDC_IN_DIMENSIONS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f Hz", payload->inFps);
	SendDlgItemMessage(m_Dlg, IDC_IN_FPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%lld", payload->inFrameDuration);
	SendDlgItemMessage(m_Dlg, IDC_IN_FRAME_DUR, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->inColourFormat.c_str());
	SendDlgItemMessage(m_Dlg, IDC_IN_CF, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->inQuantisation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_IN_QUANTISATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->inSaturation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_IN_SATURATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->inPixelLayout.c_str());
	SendDlgItemMessage(m_Dlg, IDC_IN_PIXEL_LAYOUT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->signalStatus.c_str());
	SendDlgItemMessage(m_Dlg, IDC_SIGNAL_STATUS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(VIDEO_OUTPUT_STATUS* payload)
{
	WCHAR buffer[28];
	_snwprintf_s(buffer, _TRUNCATE, L"%d x %d (%d:%d) %d bit", payload->outX, payload->outY, payload->outAspectX, payload->outAspectY, payload->outBitDepth);
	SendDlgItemMessage(m_Dlg, IDC_OUT_DIMENSIONS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f Hz", payload->outFps);
	SendDlgItemMessage(m_Dlg, IDC_OUT_FPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outColourFormat.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_CF, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outQuantisation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_QUANTISATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outSaturation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_SATURATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs / %hs", payload->outPixelLayout.c_str(), payload->outPixelStructure.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_PIXEL_LAYOUT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outTransferFunction.c_str());
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_OUT_TF, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(HDR_STATUS* payload)
{
	WCHAR buffer[28];
	if (payload->hdrOn)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f x %.4f", payload->hdrPrimaryRX, payload->hdrPrimaryRY);
		SendDlgItemMessage(m_Dlg, IDC_HDR_RED, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f x %.4f", payload->hdrPrimaryGX, payload->hdrPrimaryGY);
		SendDlgItemMessage(m_Dlg, IDC_HDR_GREEN, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f x %.4f", payload->hdrPrimaryBX, payload->hdrPrimaryBY);
		SendDlgItemMessage(m_Dlg, IDC_HDR_BLUE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f x %.4f", payload->hdrWpX, payload->hdrWpY);
		SendDlgItemMessage(m_Dlg, IDC_HDR_WHITE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f / %.1f", payload->hdrMinDML, payload->hdrMaxDML);
		SendDlgItemMessage(m_Dlg, IDC_HDR_DML, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.1f", payload->hdrMaxCLL);
		SendDlgItemMessage(m_Dlg, IDC_HDR_MAX_CLL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		_snwprintf_s(buffer, _TRUNCATE, L"%.1f", payload->hdrMaxFALL);
		SendDlgItemMessage(m_Dlg, IDC_HDR_MAX_FALL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	else
	{
		_snwprintf_s(buffer, _TRUNCATE, L"SDR");
		SendDlgItemMessage(m_Dlg, IDC_HDR_RED, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_GREEN, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_BLUE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_WHITE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_DML, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_MAX_CLL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		SendDlgItemMessage(m_Dlg, IDC_HDR_MAX_FALL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(DEVICE_STATUS* payload)
{
	if (!payload->deviceDesc.empty())
	{
		WCHAR buffer[256];
		_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->deviceDesc.c_str());
		SendDlgItemMessage(m_Dlg, IDC_DEVICE_ID, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	return S_OK;
}
