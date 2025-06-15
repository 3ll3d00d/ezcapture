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

#include "registry.h"

registry::registry(log_data pLogData, const std::wstring& pKeyBase) : mLogData(std::move(pLogData))
{
	std::wstring prefix{L"Software\\3ll3d00d\\"};
	auto subkey = prefix + pKeyBase;
	LSTATUS retVal = RegOpenKeyEx(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ | KEY_WRITE, &mKey);
	mOpened = ERROR_SUCCESS == retVal;
}

registry::~registry()
{
	if (mOpened) RegCloseKey(mKey);
}

std::wstring registry::ReadString(LPCTSTR pSubKey, HRESULT& hr) const
{
	std::wstring result;
	if (!mOpened)
	{
		hr = E_FAIL;
		return result;
	}

	DWORD strSize;
	LONG readRetVal = RegQueryValueEx(mKey, pSubKey, nullptr, nullptr, nullptr, &strSize);
	if (readRetVal == ERROR_SUCCESS)
	{
		WCHAR* buffer = static_cast<WCHAR*>(CoTaskMemAlloc(strSize));
		if (!buffer)
		{
			hr = E_OUTOFMEMORY;
			return result;
		}
		memset(buffer, 0, strSize);
		readRetVal = RegQueryValueEx(mKey, pSubKey, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &strSize);
		result = std::wstring(buffer);
		CoTaskMemFree(buffer);
		hr = readRetVal == ERROR_SUCCESS ? S_OK : E_FAIL;
	}
	else
	{
		hr = E_INVALIDARG;
	}
	return result;
}

HRESULT registry::WriteString(LPCTSTR pSubKey, LPCTSTR pValue) const
{
	if (!mOpened)
	{
		return E_FAIL;
	}
	LONG writeRetVal = RegSetValueEx(mKey, pSubKey, 0, REG_SZ, reinterpret_cast<const BYTE*>(pValue),
	                                 static_cast<DWORD>((wcslen(pValue) + 1) * sizeof(WCHAR)));
	if (writeRetVal != ERROR_SUCCESS)
	{
		return E_FAIL;
	}
	#ifndef NO_QUILL
	if (S_OK == writeRetVal)
	{
		LOG_INFO(mLogData.logger, "[{}] Updated registry {}={}", mLogData.prefix, pSubKey, pValue);
	}
	else
	{
		LOG_WARNING(mLogData.logger, "[{}] Failed to update registry {}={} (res: {:#08x})",
		            mLogData.prefix, pSubKey, pValue, writeRetVal);
	}
	#endif
	return S_OK;
}

HRESULT registry::InitString(LPCTSTR pSubKey) const
{
	HRESULT hr;
	ReadString(pSubKey, hr);
	if (hr != S_OK)
	{
		hr = WriteString(pSubKey, L"");
	}
	return hr;
}

uint32_t registry::ReadUInt32(LPCTSTR pSubKey, HRESULT& hr) const
{
	DWORD sz = sizeof(DWORD);
	DWORD val = 0;
	hr = RegQueryValueEx(mKey, pSubKey, nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &sz);
	if (hr != ERROR_SUCCESS)
	{
		hr = E_FAIL;
	}
	return val;
}

HRESULT registry::WriteUInt32(LPCTSTR pSubKey, uint32_t pValue) const
{
	auto lStatus = RegSetValueEx(mKey, pSubKey, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&pValue), sizeof(pValue));
	if (S_OK == lStatus)
	{
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Updated registry {}={}", mLogData.prefix, pSubKey, pValue);
		#endif

		return S_OK;
	}

	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger, "[{}] Failed to update registry {}={} (res: {:#08x})",
	            mLogData.prefix, pSubKey, pValue, lStatus);
	#endif

	return E_FAIL;
}

HRESULT registry::InitUInt32(LPCTSTR pSubKey) const
{
	HRESULT hr;
	ReadUInt32(pSubKey, hr);
	if (hr != S_OK)
	{
		hr = WriteUInt32(pSubKey, 0);
	}
	return hr;
}

bool registry::ReadBool(LPCTSTR pSubKey, HRESULT& hr) const
{
	return ReadUInt32(pSubKey, hr) == 1;
}

HRESULT registry::WriteBool(LPCTSTR pSubKey, bool pValue) const
{
	return WriteUInt32(pSubKey, pValue ? 1 : 0);
}

HRESULT registry::InitBool(LPCTSTR pSubKey) const
{
	return InitUInt32(pSubKey);
}
