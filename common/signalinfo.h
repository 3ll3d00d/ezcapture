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

#include <Windows.h>
#include <Commctrl.h>
#include <initguid.h>
#include <streams.h>

#include "domain.h"


// {8DC689DB-68FE-4C30-AAE5-0E515CF9324C}
DEFINE_GUID(CLSID_SignalInfoProps,
    0x8dc689db, 0x68fe, 0x4c30, 0xaa, 0xe5, 0xe, 0x51, 0x5c, 0xf9, 0x32, 0x4c);

// {6A505550-28B2-4668-BC2C-461E75A63BC4}
DEFINE_GUID(IID_ISignalInfo,
            0x6a505550, 0x28b2, 0x4668, 0xbc, 0x2c, 0x46, 0x1e, 0x75, 0xa6, 0x3b, 0xc4);

// {4D6B8852-06A6-4997-BC07-3507BB77F748}
DEFINE_GUID(IID_ISignalInfoCB,
    0x4d6b8852, 0x6a6, 0x4997, 0xbc, 0x7, 0x35, 0x7, 0xbb, 0x77, 0xf7, 0x48);


interface __declspec(uuid("4D6B8852-06A6-4997-BC07-3507BB77F748")) ISignalInfoCB
{
    STDMETHOD(Reload)(AUDIO_INPUT_STATUS* payload) = 0;
    STDMETHOD(Reload)(AUDIO_OUTPUT_STATUS* payload) = 0;
    STDMETHOD(Reload)(VIDEO_INPUT_STATUS* payload) = 0;
    STDMETHOD(Reload)(VIDEO_OUTPUT_STATUS* payload) = 0;
    STDMETHOD(Reload)(HDR_STATUS* payload) = 0;
    STDMETHOD(Reload)(DEVICE_STATUS* payload) = 0;
	STDMETHOD(Reload)(DISPLAY_STATUS* payload) = 0;
};

interface __declspec(uuid("6A505550-28B2-4668-BC2C-461E75A63BC4")) ISignalInfo : public IUnknown
{
	STDMETHOD(SetCallback)(ISignalInfoCB* cb) = 0;
	STDMETHOD(Reload)() = 0;
};

class CSignalInfoProp :
	public ISignalInfoCB,
	public CBasePropertyPage
{
public:
	// Provide the way for COM to create a Filter object
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

	CSignalInfoProp(LPUNKNOWN pUnk, HRESULT* phr);
	~CSignalInfoProp() override;

	HRESULT OnActivate() override;
	HRESULT OnConnect(IUnknown* pUnk) override;
	HRESULT OnDisconnect() override;
	HRESULT OnApplyChanges() override;
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    // ISignalInfoCB
    HRESULT Reload(AUDIO_INPUT_STATUS* payload) override;
    HRESULT Reload(AUDIO_OUTPUT_STATUS* payload) override;
    HRESULT Reload(VIDEO_INPUT_STATUS* payload) override;
    HRESULT Reload(VIDEO_OUTPUT_STATUS* payload) override;
    HRESULT Reload(HDR_STATUS* payload) override;
    HRESULT Reload(DEVICE_STATUS* payload) override;
	HRESULT Reload(DISPLAY_STATUS* payload) override;

private:
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite)
		{
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}

	ISignalInfo* mSignalInfo = nullptr;
};
