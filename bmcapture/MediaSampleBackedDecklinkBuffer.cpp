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

#include "MediaSampleBackedDecklinkBuffer.h"

#include "capture.h"
#include "combase.h"

HRESULT MediaSampleBackedDecklinkBuffer::QueryInterface(const IID& riid, void** ppvObject)
{
	if (riid == _uuidof(IDeckLinkVideoBuffer))
	{
		return GetInterface(this, ppvObject);
	}

	if (riid == IID_IUnknown)
	{
		GetInterface(reinterpret_cast<LPUNKNOWN>(reinterpret_cast<PNDUNKNOWN>(this)), ppvObject);
		return NOERROR;
	}
	*ppvObject = nullptr;
	return E_NOINTERFACE;
}

HRESULT MediaSampleBackedDecklinkBuffer::GetBytes(void** buffer)
{
	BYTE* out;
	auto hr = mSample->GetPointer(&out);
	if (hr != S_OK)
	{
		return hr;
	}
	*buffer = &out;
	return S_OK;
}
