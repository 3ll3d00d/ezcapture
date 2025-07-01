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
#define NOMINMAX // quill does not compile without this

#include <streams.h>
#include <initguid.h>
#include <dvdmedia.h>
#include <dllsetup.h>
#include "mw_capture_filter.h"

#define CreateComObject(clsid, iid, var) CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, iid, (void **)&var);

#if CAPTURE_NAME_SUFFIX == 1
#define FILTER_NAME L"Magewell Pro Capture (Trace)"
 // {9E53337D-9E32-40B4-AD39-B8525CDECD45}
DEFINE_GUID(CLSID_MWCAPTURE_FILTER,
    0x9e53337d, 0x9e32, 0x40b4, 0xad, 0x39, 0xb8, 0x52, 0x5c, 0xde, 0xcd, 0x45);
#elif CAPTURE_NAME_SUFFIX == 2
#define FILTER_NAME L"Magewell Pro Capture (Warn)"
 // {87A31069-9A13-40D6-9C84-5499D8A44519}
DEFINE_GUID(CLSID_MWCAPTURE_FILTER,
    0x87a31069, 0x9a13, 0x40d6, 0x9c, 0x84, 0x54, 0x99, 0xd8, 0xa4, 0x45, 0x19);
#else
#define FILTER_NAME L"Magewell Pro Capture"
 // {4E3B0A92-2476-4016-81F0-201F19F6FBAE}
DEFINE_GUID(CLSID_MWCAPTURE_FILTER,
    0x4e3b0a92, 0x2476, 0x4016, 0x81, 0xf0, 0x20, 0x1f, 0x19, 0xf6, 0xfb, 0xae);
#endif

// LAV compatibility
// 34326E69-0000-0010-8000-00AA00389B71 (big-endian int24)
DEFINE_GUID(MEDIASUBTYPE_PCM_IN24, 0x34326E69, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// 32336E69-0000-0010-8000-00AA00389B71 (big-endian int32)
DEFINE_GUID(MEDIASUBTYPE_PCM_IN32, 0x32336E69, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
// 74776f73-0000-0010-8000-00AA00389B71 (big-endian int8 or int16)
DEFINE_GUID(MEDIASUBTYPE_PCM_SOWT, 0x74776f73, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);


constexpr AMOVIESETUP_MEDIATYPE sVideoPinTypes =
{
    &MEDIATYPE_Video, // Major type
    &MEDIASUBTYPE_NULL // Minor type
};

constexpr AMOVIESETUP_MEDIATYPE sAudioPinTypes =
{
    &MEDIATYPE_Audio, // Major type
    &MEDIASUBTYPE_NULL // Minor type
};

constexpr AMOVIESETUP_PIN sVideoPin = {
    const_cast<LPWSTR>(L"Video"),
    FALSE, // Is it rendered
    TRUE, // Is it an output
    FALSE, // Are we allowed none
    FALSE, // And allowed many
    &CLSID_NULL, // Connects to filter
    nullptr, // Connects to pin
    1, // Number of types
    &sVideoPinTypes // Pin information
};

constexpr AMOVIESETUP_PIN sAudioPin = {
    const_cast<LPWSTR>(L"Audio"),
    FALSE, // Is it rendered
    TRUE, // Is it an output
    FALSE, // Are we allowed none
    FALSE, // And allowed many
    &CLSID_NULL, // Connects to filter
    nullptr, // Connects to pin
    1, // Number of types
    &sAudioPinTypes // Pin information
};

const AMOVIESETUP_PIN sMIPPins[] = { sVideoPin, sAudioPin };

constexpr AMOVIESETUP_FILTER sMIPSetup =
{
    &CLSID_MWCAPTURE_FILTER, // Filter CLSID
    L"MagewellCapture", // String name
    MERIT_DO_NOT_USE, // Filter merit
    2, // Number of pins
    sMIPPins // Pin information
};


// List of class IDs and creator functions for the class factory.
CFactoryTemplate g_Templates[] = {
    {
        FILTER_NAME,
        &CLSID_MWCAPTURE_FILTER,
        magewell_capture_filter::CreateInstance,
        nullptr,
        &sMIPSetup
    },
    {
        L"MWCapture Properties",
        &CLSID_SignalInfoProps,
        CSignalInfoProp::CreateInstance,
        nullptr,
        nullptr
    }
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI RegisterAllServers(LPCWSTR szFileName, BOOL bRegister);

STDAPI AMovieSetupRegisterServer(CLSID clsServer, LPCWSTR szDescription, LPCWSTR szFileName, LPCWSTR szThreadingModel = L"Both", LPCWSTR szServerType = L"InprocServer32");

STDAPI AMovieSetupUnregisterServer(CLSID clsServer);

STDAPI RegisterFilters(BOOL bRegister)
{
	WCHAR achFileName[MAX_PATH];
    char achTemp[MAX_PATH];
    ASSERT(g_hInst != 0);

    if (0 == GetModuleFileNameA(g_hInst, achTemp, sizeof(achTemp)))
        return AmHresultFromWin32(GetLastError());

    MultiByteToWideChar(CP_ACP, 0L, achTemp, lstrlenA(achTemp) + 1, achFileName, NUMELMS(achFileName));

    HRESULT hr = CoInitialize(0);
    if (bRegister)
    {
        hr = RegisterAllServers(achFileName, TRUE);
    }

    if (SUCCEEDED(hr))
    {
        IFilterMapper2* fm = 0;
        hr = CreateComObject(CLSID_FilterMapper2, IID_IFilterMapper2, fm)
        if (SUCCEEDED(hr))
        {
            if (bRegister)
            {
                REGFILTER2 videoFilter;
                videoFilter.dwVersion = 1;
                videoFilter.dwMerit = MERIT_DO_NOT_USE;
                videoFilter.cPins = 1;
                videoFilter.rgPins = &sMIPPins[0];
                hr = fm->RegisterFilter(CLSID_MWCAPTURE_FILTER, FILTER_NAME, nullptr, &CLSID_VideoInputDeviceCategory, nullptr, &videoFilter);
            }
            else
            {
                hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, nullptr, CLSID_MWCAPTURE_FILTER);
            }
        }

        if (fm)
            fm->Release();
    }

    if (!bRegister)
        hr += RegisterAllServers(achFileName, FALSE);

    CoFreeUnusedLibraries();
    CoUninitialize();
    return hr;
}

STDAPI DllRegisterServer()
{
    return RegisterFilters(TRUE);
}

STDAPI DllUnregisterServer()
{
    return RegisterFilters(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved)
{
    return DllEntryPoint(static_cast<HINSTANCE>(hModule), dwReason, lpReserved);
}
