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
#define NOMINMAX

#include <string>
#include <windows.h>
#include "logging.h"

class registry
{
public:
    registry(log_data pLogData, const std::wstring& pKeyBase);
    ~registry();

    std::wstring ReadString(LPCTSTR pSubKey, HRESULT& hr) const;
    HRESULT WriteString(LPCTSTR pSubKey, LPCTSTR pValue) const;
    HRESULT InitString(LPCTSTR pSubKey) const;

	uint32_t ReadUInt32(LPCTSTR pSubKey, HRESULT& hr) const;
    HRESULT WriteUInt32(LPCTSTR pSubKey, uint32_t pValue) const;
    HRESULT InitUInt32(LPCTSTR pSubKey) const;

    bool ReadBool(LPCTSTR pSubKey, HRESULT& hr) const;
    HRESULT WriteBool(LPCTSTR pSubKey, bool pValue) const;
    HRESULT InitBool(LPCTSTR pSubKey) const;

private:
    HKEY mKey{};
    bool mOpened;
    log_data mLogData;
};
