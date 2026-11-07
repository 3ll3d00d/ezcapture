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
#include "version.h"
#include "signalinfo.h"
#include "commctrl.h"

#include <corecrt_wstdio.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "resource.h"

#define CHANNEL_VALID_TO_BINARY_PATTERN L"%c%c%c%c"
#define CHANNEL_VALID_TO_BINARY(val)  \
  ((val) & (0x01 << 3) ? '1' : '0'), \
  ((val) & (0x01 << 2) ? '1' : '0'), \
  ((val) & (0x01 << 1) ? '1' : '0'), \
  ((val) & (0x01 << 0) ? '1' : '0')

constexpr auto to_millis_ratio = 10000.0;

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
	WCHAR b1[16];

	SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));
	DWORD hdrProfileId = _wtoi(b1);
	auto hr1 = mSignalInfo->SetHDRProfile(hdrProfileId);

	SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));
	DWORD sdrProfileId = _wtoi(b1);
	auto hr2 = mSignalInfo->SetSDRProfile(sdrProfileId);

	bool switchProfiles = static_cast<bool>(SendDlgItemMessage(m_Dlg, IDC_SWITCH_MC_PROFILES, BM_GETCHECK, 0, 0));
	auto hr3 = mSignalInfo->SetHdrProfileSwitchEnabled(switchProfiles);

	bool switchRate = static_cast<bool>(SendDlgItemMessage(m_Dlg, IDC_SWITCH_REFRESH_RATE, BM_GETCHECK, 0, 0));
	auto hr4 = mSignalInfo->SetRefreshRateSwitchEnabled(switchRate);

	bool highPrio = static_cast<bool>(SendDlgItemMessage(m_Dlg, IDC_ENABLE_HIGH_PRIORITY, BM_GETCHECK, 0, 0));
	auto hr5 = mSignalInfo->SetHighThreadPriorityEnabled(highPrio);

	bool enableAudio = static_cast<bool>(SendDlgItemMessage(m_Dlg, IDC_ENABLE_AUDIO, BM_GETCHECK, 0, 0));
	auto hr6 = mSignalInfo->SetAudioCaptureEnabled(enableAudio);

	bool enableMetrics = static_cast<bool>(SendDlgItemMessage(m_Dlg, IDC_ENABLE_METRICS, BM_GETCHECK, 0, 0));
	auto hr7 = mSignalInfo->SetMetricsEnabled(enableMetrics);

	SendDlgItemMessage(m_Dlg, IDC_METRICS_PORT, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));
	DWORD metricsPort = _wtoi(b1);
	auto hr8 = mSignalInfo->SetMetricsPort(metricsPort);

	// show the outcome (e.g. that a restart is required)
	mSignalInfo->Reload();

	return hr1 == S_OK && hr2 == S_OK && hr3 == S_OK && hr4 == S_OK && hr5 == S_OK && hr6 == S_OK && hr7 == S_OK
	       && hr8 == S_OK
		       ? S_OK
		       : E_FAIL;
}

INT_PTR CSignalInfoProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SWITCH_REFRESH_RATE && HIWORD(wParam) == BN_CLICKED)
		{
			bool bFlag = static_cast<bool>(SendDlgItemMessage(m_Dlg, LOWORD(wParam), BM_GETCHECK, 0, 0));
			bool existing;
			mSignalInfo->IsRefreshRateSwitchEnabled(&existing);

			if (bFlag != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_SWITCH_MC_PROFILES && HIWORD(wParam) == BN_CLICKED)
		{
			bool bFlag = static_cast<bool>(SendDlgItemMessage(m_Dlg, LOWORD(wParam), BM_GETCHECK, 0, 0));
			bool existing;
			mSignalInfo->IsHdrProfileSwitchEnabled(&existing);

			EnableWindow(GetDlgItem(m_Dlg, IDC_MC_HDR_PROFILE), bFlag);
			EnableWindow(GetDlgItem(m_Dlg, IDC_MC_SDR_PROFILE), bFlag);
			if (bFlag != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_ENABLE_HIGH_PRIORITY && HIWORD(wParam) == BN_CLICKED)
		{
			bool bFlag = static_cast<bool>(SendDlgItemMessage(m_Dlg, LOWORD(wParam), BM_GETCHECK, 0, 0));
			bool existing;
			mSignalInfo->IsHighThreadPriorityEnabled(&existing);

			if (bFlag != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_ENABLE_AUDIO && HIWORD(wParam) == BN_CLICKED)
		{
			bool bFlag = static_cast<bool>(SendDlgItemMessage(m_Dlg, LOWORD(wParam), BM_GETCHECK, 0, 0));
			bool existing;
			mSignalInfo->IsAudioCaptureEnabled(&existing);

			if (bFlag != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_ENABLE_METRICS && HIWORD(wParam) == BN_CLICKED)
		{
			bool bFlag = static_cast<bool>(SendDlgItemMessage(m_Dlg, LOWORD(wParam), BM_GETCHECK, 0, 0));
			bool existing;
			mSignalInfo->IsMetricsEnabled(&existing);

			EnableWindow(GetDlgItem(m_Dlg, IDC_METRICS_PORT), bFlag);
			if (bFlag != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_METRICS_PORT && HIWORD(wParam) == EN_CHANGE)
		{
			WCHAR b1[16];
			SendDlgItemMessage(m_Dlg, IDC_METRICS_PORT, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));
			DWORD port = _wtoi(b1);
			if (port > 65535)
			{
				SendDlgItemMessage(m_Dlg, LOWORD(wParam), EM_UNDO, 0, 0);
				return FALSE;
			}
			DWORD existing;
			mSignalInfo->GetMetricsPort(&existing);
			if (port != 0 && port != existing)
			{
				SetDirty();
				return TRUE;
			}
			return FALSE;
		}
		if (LOWORD(wParam) == IDC_MC_HDR_PROFILE && HIWORD(wParam) == EN_CHANGE)
		{
			WCHAR b1[16];
			SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));

			DWORD profileId = _wtoi(b1);
			size_t len = wcslen(b1);
			if (profileId == 0 && (b1[0] != L'0' || len > 1))
			{
				SendDlgItemMessage(m_Dlg, LOWORD(wParam), EM_UNDO, 0, 0);
			}
			else
			{
				DWORD existing;
				mSignalInfo->GetHDRProfile(&existing);

				swprintf_s(b1, L"%d", profileId);
				if (wcslen(b1) != len)
				{
					SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(b1));
				}
				if (profileId != existing)
				{
					SetDirty();
					return TRUE;
				}
			}
			return FALSE;
		}

		if (LOWORD(wParam) == IDC_MC_SDR_PROFILE && HIWORD(wParam) == EN_CHANGE)
		{
			WCHAR b1[16];
			SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_GETTEXT, 16, reinterpret_cast<LPARAM>(&b1));

			DWORD profileId = _wtoi(b1);
			size_t len = wcslen(b1);
			if (profileId == 0 && (b1[0] != L'0' || len > 1))
			{
				SendDlgItemMessage(m_Dlg, LOWORD(wParam), EM_UNDO, 0, 0);
			}
			else
			{
				DWORD existing;
				mSignalInfo->GetSDRProfile(&existing);
				swprintf_s(b1, L"%d", profileId);
				if (wcslen(b1) != len)
				{
					SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(b1));
				}
				if (profileId != existing)
				{
					SetDirty();
					return TRUE;
				}
			}
			return FALSE;
		}
		break;
	}
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CSignalInfoProp::Reload(audio_input_status* payload)
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

HRESULT CSignalInfoProp::Reload(audio_output_status* payload)
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

HRESULT CSignalInfoProp::Reload(video_input_status* payload)
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

HRESULT CSignalInfoProp::Reload(video_output_status* payload)
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

HRESULT CSignalInfoProp::Reload(hdr_status* payload)
{
	mLastHdr = *payload;
	mHaveHdr = true;
	if (mDvShown)
	{
		return S_OK;
	}
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

namespace
{
	// SMPTE ST 2084, 12-bit code value to cd/m2
	double pq_to_nits(uint16_t pq)
	{
		constexpr double m1 = 2610.0 / 16384.0;
		constexpr double m2 = 2523.0 / 4096.0 * 128.0;
		constexpr double c1 = 3424.0 / 4096.0;
		constexpr double c2 = 2413.0 / 4096.0 * 32.0;
		constexpr double c3 = 2392.0 / 4096.0 * 32.0;
		const double ep = std::pow(std::clamp(pq / 4095.0, 0.0, 1.0), 1.0 / m2);
		return 10000.0 * std::pow(std::max(ep - c1, 0.0) / (c2 - c3 * ep), 1.0 / m1);
	}

	void set_text(HWND dlg, int id, const wchar_t* text)
	{
		SendDlgItemMessage(dlg, id, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text));
	}
}

void CSignalInfoProp::ShowHdrLabels()
{
	set_text(m_Dlg, IDC_HDR_RED_LABEL, L"Red");
	set_text(m_Dlg, IDC_HDR_GREEN_LABEL, L"Green");
	set_text(m_Dlg, IDC_HDR_BLUE_LABEL, L"Blue");
	set_text(m_Dlg, IDC_HDR_WHITE_LABEL, L"White");
	set_text(m_Dlg, IDC_HDR_DML_LABEL, L"DML");
	set_text(m_Dlg, IDC_HDR_MAX_FALL_LABEL, L"MaxFALL");
	set_text(m_Dlg, IDC_HDR_MAX_CLL_LABEL, L"MaxCLL");
}

HRESULT CSignalInfoProp::Reload(dv_status* payload)
{
	const auto state = payload->state;
	if (state == DV_INACTIVE || state == DV_SEARCHING)
	{
		set_text(m_Dlg, IDC_HDR_BOX, state == DV_SEARCHING ? L"HDR (Dolby Vision: not detected)" : L"HDR");
		if (mDvShown)
		{
			mDvShown = false;
			ShowHdrLabels();
			if (mHaveHdr)
			{
				auto last = mLastHdr;
				Reload(&last);
			}
		}
		return S_OK;
	}

	mDvShown = true;
	set_text(m_Dlg, IDC_HDR_BOX, L"Dolby Vision");
	set_text(m_Dlg, IDC_HDR_RED_LABEL, L"Status");
	set_text(m_Dlg, IDC_HDR_GREEN_LABEL, L"Range");
	set_text(m_Dlg, IDC_HDR_BLUE_LABEL, L"Source");
	set_text(m_Dlg, IDC_HDR_WHITE_LABEL, L"L1 nits");
	set_text(m_Dlg, IDC_HDR_DML_LABEL, L"Metadata");
	set_text(m_Dlg, IDC_HDR_MAX_FALL_LABEL, L"Rejected");
	set_text(m_Dlg, IDC_HDR_MAX_CLL_LABEL, L"CRC");

	WCHAR buffer[64];
	set_text(m_Dlg, IDC_HDR_RED, state == DV_LOCKED
		                             ? L"Locked"
		                             : state == DV_NO_METADATA
		                             ? L"Locked, no metadata"
		                             : L"Renderer rejected P210");
	if (payload->metadataValid)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%s, colour space %u", payload->fullRange ? L"Full" : L"Limited",
		             static_cast<unsigned>(payload->colourSpace));
		set_text(m_Dlg, IDC_HDR_GREEN, buffer);
		_snwprintf_s(buffer, _TRUNCATE, L"%.4f - %.0f nits", pq_to_nits(payload->sourceMinPq),
		             pq_to_nits(payload->sourceMaxPq));
		set_text(m_Dlg, IDC_HDR_BLUE, buffer);
	}
	else
	{
		set_text(m_Dlg, IDC_HDR_GREEN, L"-");
		set_text(m_Dlg, IDC_HDR_BLUE, L"-");
	}
	if (payload->metadataValid && payload->hasL1)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%.2f / %.1f / %.0f", pq_to_nits(payload->l1MinPq),
		             pq_to_nits(payload->l1AvgPq), pq_to_nits(payload->l1MaxPq));
		set_text(m_Dlg, IDC_HDR_WHITE, buffer);
	}
	else
	{
		set_text(m_Dlg, IDC_HDR_WHITE, L"-");
	}
	_snwprintf_s(buffer, _TRUNCATE, L"%zu bytes / %zu pkts", payload->metadataBytes, payload->packets);
	set_text(m_Dlg, IDC_HDR_DML, buffer);
	_snwprintf_s(buffer, _TRUNCATE, L"%llu of %llu", payload->rejectedFrames, payload->frames);
	set_text(m_Dlg, IDC_HDR_MAX_FALL, buffer);
	_snwprintf_s(buffer, _TRUNCATE, L"%llu bad, %llu fixed", payload->crcFailedCopies, payload->recoveredPackets);
	set_text(m_Dlg, IDC_HDR_MAX_CLL, buffer);
	return S_OK;
}

HRESULT CSignalInfoProp::Reload(device_status* payload)
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
		_snwprintf_s(buffer, _TRUNCATE, L"%d / %d", payload->maxPayloadSize, payload->maxReadRequestSize);
		SendDlgItemMessage(m_Dlg, IDC_PCIE_MPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS_LABEL), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS), SW_SHOW);
	}
	else
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS_LABEL), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_PCIE_MPS), SW_HIDE);
	}
	if (payload->bandwidth > 0.0 && payload->bandwidthUsed > 0.0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%.0f / %.0f MB/s (%.0f%%)", payload->bandwidthUsed / 1e6,
		             payload->bandwidth / 1e6, 100.0 * payload->bandwidthUsed / payload->bandwidth);
	}
	else if (payload->bandwidth > 0.0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%.0f MB/s available", payload->bandwidth / 1e6);
	}
	else if (payload->bandwidthUsed > 0.0)
	{
		_snwprintf_s(buffer, _TRUNCATE, L"%.0f MB/s", payload->bandwidthUsed / 1e6);
	}
	else
	{
		_snwprintf_s(buffer, _TRUNCATE, L"?");
	}
	SendDlgItemMessage(m_Dlg, IDC_BANDWIDTH, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
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

HRESULT CSignalInfoProp::Reload(display_status* payload)
{
	if (!payload->status.empty())
	{
		WCHAR buffer[256];
		_snwprintf_s(buffer, _TRUNCATE, L"%ls", payload->status.c_str());
		SendDlgItemMessage(m_Dlg, IDC_DISPLAY, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	}
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadV1(latency_stats* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / to_millis_ratio,
	             payload->mean / to_millis_ratio, static_cast<double>(payload->max) / to_millis_ratio);
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CAP_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

	_snwprintf(buffer, _TRUNCATE, L"%hs", payload->name.c_str());
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CAP_LAT_LABEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadV2(latency_stats* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / to_millis_ratio,
	             payload->mean / to_millis_ratio, static_cast<double>(payload->max) / to_millis_ratio);
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CONV_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

	_snwprintf(buffer, _TRUNCATE, L"%hs", payload->name.c_str());
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_CONV_LAT_LABEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadV3(latency_stats* payload)
{
	if (payload->name.empty())
	{
		ShowWindow(GetDlgItem(m_Dlg, IDC_VIDEO_ALLOC_LAT), SW_HIDE);
		ShowWindow(GetDlgItem(m_Dlg, IDC_VIDEO_ALLOC_LAT_LABEL), SW_HIDE);
	}
	else
	{
		WCHAR buffer[256];
		_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / to_millis_ratio,
			payload->mean / to_millis_ratio, static_cast<double>(payload->max) / to_millis_ratio);
		SendDlgItemMessage(m_Dlg, IDC_VIDEO_ALLOC_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

		_snwprintf(buffer, _TRUNCATE, L"%hs", payload->name.c_str());
		SendDlgItemMessage(m_Dlg, IDC_VIDEO_ALLOC_LAT_LABEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

		ShowWindow(GetDlgItem(m_Dlg, IDC_VIDEO_ALLOC_LAT), SW_SHOW);
		ShowWindow(GetDlgItem(m_Dlg, IDC_VIDEO_ALLOC_LAT_LABEL), SW_SHOW);
}
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadVfps(double* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f Hz", *payload);
	SendDlgItemMessage(m_Dlg, IDC_VIDEO_FPS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

	return S_OK;
}

HRESULT CSignalInfoProp::ReloadA1(latency_stats* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / to_millis_ratio,
	             payload->mean / to_millis_ratio, static_cast<double>(payload->max) / to_millis_ratio);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_CAP_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

	_snwprintf(buffer, _TRUNCATE, L"%hs", payload->name.c_str());
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_CAP_LAT_LABEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadA2(latency_stats* payload)
{
	WCHAR buffer[256];
	_snwprintf_s(buffer, _TRUNCATE, L"%.3f / %.3f / %.3f ms", static_cast<double>(payload->min) / to_millis_ratio,
	             payload->mean / to_millis_ratio, static_cast<double>(payload->max) / to_millis_ratio);
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_ALLOC_LAT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));

	_snwprintf(buffer, _TRUNCATE, L"%hs", payload->name.c_str());
	SendDlgItemMessage(m_Dlg, IDC_AUDIO_ALLOC_LAT_LABEL, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	return S_OK;
}

HRESULT CSignalInfoProp::ReloadControls(const bool& rateEnabled, const bool& profileEnabled, const DWORD& hdr,
                                        const DWORD& sdr, const bool& highThreadPrio, const bool& audioCapture)
{
	WCHAR buffer[8];

	_snwprintf_s(buffer, _TRUNCATE, L"%d", hdr);
	SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	EnableWindow(GetDlgItem(m_Dlg, IDC_MC_HDR_PROFILE), profileEnabled);
	SendDlgItemMessage(m_Dlg, IDC_MC_HDR_PROFILE_SPIN, UDM_SETRANGE32, 0, 1000);

	_snwprintf_s(buffer, _TRUNCATE, L"%d", sdr);
	SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	EnableWindow(GetDlgItem(m_Dlg, IDC_MC_SDR_PROFILE), profileEnabled);
	SendDlgItemMessage(m_Dlg, IDC_MC_SDR_PROFILE_SPIN, UDM_SETRANGE32, 0, 1000);

	SendDlgItemMessage(m_Dlg, IDC_SWITCH_MC_PROFILES, BM_SETCHECK, profileEnabled, 0);
	SendDlgItemMessage(m_Dlg, IDC_SWITCH_REFRESH_RATE, BM_SETCHECK, rateEnabled, 0);
	SendDlgItemMessage(m_Dlg, IDC_ENABLE_HIGH_PRIORITY, BM_SETCHECK, highThreadPrio, 0);
	SendDlgItemMessage(m_Dlg, IDC_ENABLE_AUDIO, BM_SETCHECK, audioCapture, 0);

	return S_OK;
}

HRESULT CSignalInfoProp::ReloadMetricsControls(const bool& enabled, const DWORD& port, const std::string& status)
{
	WCHAR buffer[8];
	_snwprintf_s(buffer, _TRUNCATE, L"%d", port);
	SendDlgItemMessage(m_Dlg, IDC_METRICS_PORT, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(buffer));
	EnableWindow(GetDlgItem(m_Dlg, IDC_METRICS_PORT), enabled);
	SendDlgItemMessage(m_Dlg, IDC_ENABLE_METRICS, BM_SETCHECK, enabled, 0);

	WCHAR statusBuffer[128];
	_snwprintf_s(statusBuffer, _TRUNCATE, L"%hs", status.c_str());
	SendDlgItemMessage(m_Dlg, IDC_METRICS_STATUS, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(statusBuffer));
	return S_OK;
}
