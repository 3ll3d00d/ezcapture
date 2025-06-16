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

#include "version.h"
#include "signalinfo.h"
#include "commctrl.h"

#include <corecrt_wstdio.h>
#include <vector>

#include "resource.h"

#define CHANNEL_VALID_TO_BINARY_PATTERN L"%c%c%c%c"
#define CHANNEL_VALID_TO_BINARY(val)  \
  ((val) & (0x01 << 3) ? '1' : '0'), \
  ((val) & (0x01 << 2) ? '1' : '0'), \
  ((val) & (0x01 << 1) ? '1' : '0'), \
  ((val) & (0x01 << 0) ? '1' : '0')

static std::string wstring_to_string(const std::wstring& input)
{
	if (input.empty())
	{
		auto empty = std::string{};
		return empty;
	}
	int size = WideCharToMultiByte(CP_ACP, 0, input.c_str(), input.size(), nullptr, 0, nullptr, nullptr);
	if (size <= 0)
	{
		auto empty = std::string{};
		return empty;
	}
	std::vector<char> outChars(input.length() + 1);
	if (WideCharToMultiByte(CP_ACP, 0, input.c_str(), input.size(), outChars.data(), size, nullptr, nullptr) <= 0)
	{
		outChars.clear();
		auto empty = std::string{};
		return empty;
	}
	outChars[size] = '\0';
	return {outChars.begin(), outChars.end()};
}

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
	WCHAR b1[256];
	SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_GETTEXT, 256, reinterpret_cast<LPARAM>(&b1));
	auto hr1 = mSignalInfo->SetHDRProfile(std::wstring(b1));

	WCHAR b2[256];
	SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_GETTEXT, 256, reinterpret_cast<LPARAM>(&b2));
	auto hr2 = mSignalInfo->SetSDRProfile(std::wstring(b2));

	return hr1 == S_OK && hr2 == S_OK ? S_OK : E_FAIL;
}

INT_PTR CSignalInfoProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_MC_HDR_PROFILE)
		{
			WCHAR b1[256];
			SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_GETTEXT, 256, reinterpret_cast<LPARAM>(&b1));
			std::wstring updated(b1);

			std::wstring existing;
			mSignalInfo->GetHDRProfile(&existing);

			if (existing != updated)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_MC_SDR_PROFILE)
		{
			WCHAR b1[256];
			SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_GETTEXT, 256, reinterpret_cast<LPARAM>(&b1));
			std::wstring updated(b1);

			std::wstring existing;
			mSignalInfo->GetSDRProfile(&existing);

			if (existing != updated)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		break;
	}
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
	_snwprintf_s(buffer, _TRUNCATE, CHANNEL_VALID_TO_BINARY_PATTERN,
	             CHANNEL_VALID_TO_BINARY(payload->audioInChannelPairs));
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
	_snwprintf_s(buffer, _TRUNCATE, L"%d x %d (%d:%d) %d bit", payload->inX, payload->inY, payload->inAspectX,
	             payload->inAspectY, payload->inBitDepth);
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
	_snwprintf_s(buffer, _TRUNCATE, L"%d x %d (%d:%d) %d bit", payload->outX, payload->outY, payload->outAspectX,
	             payload->outAspectY, payload->outBitDepth);
	SendDlgItemMessage(m_Dlg, IDC_OUT_DIMENSIONS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f Hz", payload->outFps);
	SendDlgItemMessage(m_Dlg, IDC_OUT_FPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outColourFormat.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_CF, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outQuantisation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_QUANTISATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->outSaturation.c_str());
	SendDlgItemMessage(m_Dlg, IDC_OUT_SATURATION, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs / %hs", payload->outSubsampling.c_str(), payload->outPixelStructure.c_str());
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
	WCHAR buffer[256];
	if (!payload->deviceDesc.empty())
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%hs", payload->deviceDesc.c_str());
		SendDlgItemMessage(m_Dlg, IDC_DEVICE_ID, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	_snwprintf_s(buffer, _TRUNCATE, L"%.1f C", payload->temperature);
	SendDlgItemMessage(m_Dlg, IDC_TEMP, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	_snwprintf_s(buffer, _TRUNCATE, L"%hs%lld.0",
	             payload->protocol == PCIE ? "Gen " : "",
	             payload->linkSpeed);
	SendDlgItemMessage(m_Dlg, IDC_LINKSPEED, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	if (payload->linkWidth > 0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%lld.0x", payload->linkWidth);
		SendDlgItemMessage(m_Dlg, IDC_LINKWIDTH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		ShowWindow(GetDlgItem(m_Dlg, IDC_LINKWIDTH_LABEL), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_LINKWIDTH), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_LINKWIDTH_LABEL), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_LINKWIDTH), SW_HIDE);
	}
	if (payload->maxPayloadSize > 0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->maxPayloadSize);
		SendDlgItemMessage(m_Dlg, IDC_PCIE_MPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS_LABEL), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS_LABEL), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS), SW_HIDE);
	}
	if (payload->maxReadRequestSize > 0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->maxReadRequestSize);
		SendDlgItemMessage(m_Dlg, IDC_PCIE_MRRS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MRRS_LABEL), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MRRS), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MRRS_LABEL), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MRRS), SW_HIDE);
	}
	if (payload->fanSpeed > 0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%d", payload->fanSpeed);
		SendDlgItemMessage(m_Dlg, IDC_FAN, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		ShowWindow(GetDlgItem(m_Dlg, IDC_FAN), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_FAN_LABEL), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_FAN_LABEL), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_FAN), SW_HIDE);
	}
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(DISPLAY_STATUS* payload)
{
	if (!payload->status.empty())
	{
		WCHAR buffer[256];
		_snwprintf_s(buffer, _TRUNCATE, L"%ls", payload->status.c_str());
		SendDlgItemMessage(m_Dlg, IDC_DISPLAY, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadV1(CAPTURE_LATENCY* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / 1000.0,
	             payload->mean / 1000.0, static_cast<double>(payload->max) / 1000.0);
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CAP_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadV2(CAPTURE_LATENCY* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / 1000.0,
	             payload->mean / 1000.0, static_cast<double>(payload->max) / 1000.0);
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CONV_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadA(CAPTURE_LATENCY* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / 1000.0,
	             payload->mean / 1000.0, static_cast<double>(payload->max) / 1000.0);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_CAP_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadProfiles(const std::wstring& hdr, const std::wstring& sdr)
{
	SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(hdr.c_str()));

	std::wstring s(std::begin(sdr), std::end(sdr));
	SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(sdr.c_str()));

	return S_OK;
}
