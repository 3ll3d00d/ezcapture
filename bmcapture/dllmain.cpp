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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <streams.h>
#include <initguid.h>
#include <dvdmedia.h>
#include <dllsetup.h>
#include "bm_capture_filter.h"

#define CreateComObject(clsid, iid, var) CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, iid, (void **)&(var));

#if CAPTURE_NAME_SUFFIX == 1
#define FILTER_NAME L"Blackmagic Capture (Trace)"
 // {1BCB1E63-505B-4E66-ABEB-95913C7B081D}
DEFINE_GUID(CLSID_BMCAPTURE_FILTER,
    0x1bcb1e63, 0x505b, 0x4e66, 0xab, 0xeb, 0x95, 0x91, 0x3c, 0x7b, 0x8, 0x1d);
#elif CAPTURE_NAME_SUFFIX == 2
#define FILTER_NAME L"Blackmagic Capture (Warn)"
 // {D430B305-857C-475A-96B6-1E8EB86C4BF9}
DEFINE_GUID(CLSID_BMCAPTURE_FILTER,
    0xd430b305, 0x857c, 0x475a, 0x96, 0xb6, 0x1e, 0x8e, 0xb8, 0x6c, 0x4b, 0xf9);
#else
#define FILTER_NAME L"Blackmagic Capture"
 // {64116B3A-1E04-4CA7-BCFE-F42A0CE7BF16}
DEFINE_GUID(CLSID_BMCAPTURE_FILTER,
    0x64116b3a, 0x1e04, 0x4ca7, 0xbc, 0xfe, 0xf4, 0x2a, 0xc, 0xe7, 0xbf, 0x16);
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
    &CLSID_BMCAPTURE_FILTER, // Filter CLSID
    L"BlackmagicCapture", // String name
    MERIT_DO_NOT_USE, // Filter merit
    2, // Number of pins
    sMIPPins // Pin information
};


// List of class IDs and creator functions for the class factory.
CFactoryTemplate g_Templates[] = {
    {
        FILTER_NAME,
        &CLSID_BMCAPTURE_FILTER,
        blackmagic_capture_filter::CreateInstance,
        nullptr,
        &sMIPSetup
    },
    {
        L"bmcapture Properties",
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
                    hr = fm->RegisterFilter(CLSID_BMCAPTURE_FILTER, FILTER_NAME, nullptr, &CLSID_VideoInputDeviceCategory, nullptr, &videoFilter);
                }
                else
                {
                    hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, nullptr, CLSID_BMCAPTURE_FILTER);
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
