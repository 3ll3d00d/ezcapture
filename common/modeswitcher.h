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
#ifndef MODE_SWITCH_HEADER
#define MODE_SWITCH_HEADER

#define NOMINMAX // quill does not compile without this
#define WIN32_LEAN_AND_MEAN

#include <streams.h> // replace this
#include "logging.h"
#include <functional>
#include <set>
#include <map>
#include <optional>
#include <format>


struct monitor_config
{
	std::set<DWORD> refreshRates;
	std::string ignoredModes;
	std::string supportedModes;
	std::wstring name;
};

namespace mode_switch
{
	inline monitor_config GetAllSupportedRefreshRates()
	{
		HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);

		MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
		DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

		std::set<DWORD> supportedRates;
		std::map<std::string, std::set<DWORD>> ignoredModes;
		std::string supportedModes{};
		std::wstring name{};
		if (GetMonitorInfo(activeMonitor, &monitorInfo))
		{
			name = monitorInfo.szDevice;
			if (EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
			{
				auto width = devMode.dmPelsWidth;
				auto height = devMode.dmPelsHeight;
				auto modeNum = 0;
				while (EnumDisplaySettings(monitorInfo.szDevice, modeNum++, &devMode))
				{
					if (devMode.dmPelsWidth == width && devMode.dmPelsHeight == height)
					{
						auto res = supportedRates.insert(devMode.dmDisplayFrequency);
						if (res.second)
						{
							if (!supportedModes.empty())
							{
								supportedModes += ", ";
							}
							supportedModes += std::format("{}", devMode.dmDisplayFrequency);
						}
					}
					else
					{
						auto formatted = std::format("{}x{}", devMode.dmPelsWidth, devMode.dmPelsHeight);
						ignoredModes[formatted].emplace(devMode.dmDisplayFrequency);
					}
				}
				supportedModes = std::format("{}x{} [{}]", width, height, supportedModes);
			}
		}
		std::string ignoredModesDesc{};
		for (const auto& resRates : ignoredModes)
		{
			if (!ignoredModesDesc.empty())
			{
				ignoredModesDesc += ", ";
			}
			ignoredModesDesc += resRates.first;
			ignoredModesDesc += "[";
			std::string t{};
			for (const auto& rate : resRates.second)
			{
				if (!t.empty())
				{
					t += ", ";
				}
				t += std::format("{}", rate);
			}
			ignoredModesDesc += t;
			ignoredModesDesc += "]";
		}
		return {
			.refreshRates = std::move(supportedRates),
			.ignoredModes = std::format("[{}]", ignoredModesDesc),
			.supportedModes = std::move(supportedModes),
			.name = std::move(name)
		};
	}

	inline std::tuple<std::wstring, int> GetDisplayStatus()
	{
		HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);

		MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
		DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

		if (GetMonitorInfo(activeMonitor, &monitorInfo)
			&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
		{
			auto width = devMode.dmPelsWidth;
			auto height = devMode.dmPelsHeight;
			auto freq = devMode.dmDisplayFrequency;
			auto status = std::wstring{monitorInfo.szDevice};
			status += L" " + std::to_wstring(width) + L" x " + std::to_wstring(height) + L" @ " + std::to_wstring(freq)
				+
				L" Hz";
			return {status, freq};
		}
		return {L"", 0};
	}

	inline HRESULT PrintResolution(const log_data& ld)
	{
		HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);

		MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
		DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

		if (GetMonitorInfo(activeMonitor, &monitorInfo)
			&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
		{
			auto width = devMode.dmPelsWidth;
			auto height = devMode.dmPelsHeight;
			auto freq = devMode.dmDisplayFrequency;
			#ifndef NO_QUILL
			LOG_INFO(ld.logger, "[{}] Current monitor = {} {} x {} @ {} Hz", ld.prefix,
			         std::wstring{ monitorInfo.szDevice },
			         width, height, freq);
			#endif
			return S_OK;
		}
		return E_FAIL;
	}

	inline HRESULT ChangeResolution(const log_data& ld, DWORD targetRefreshRate)
	{
		if (targetRefreshRate == 0L || targetRefreshRate > 240L)
		{
			#ifndef NO_QUILL
			LOG_ERROR(ld.logger, "[{}] Invalid refresh rate change requested to {} Hz, ignoring", ld.prefix,
			          targetRefreshRate);
			#endif

			return E_FAIL;
		}

		#ifndef NO_QUILL
		const auto t1 = std::chrono::high_resolution_clock::now();
		#endif

		HMONITOR activeMonitor = MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTONEAREST);
		MONITORINFOEX monitorInfo{{.cbSize = sizeof(MONITORINFOEX)}};
		DEVMODE devMode{.dmSize = sizeof(DEVMODE)};

		if (GetMonitorInfo(activeMonitor, &monitorInfo)
			&& EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
		{
			#ifndef NO_QUILL
			const auto t2 = std::chrono::high_resolution_clock::now();
			#endif

			auto width = devMode.dmPelsWidth;
			auto height = devMode.dmPelsHeight;
			auto freq = devMode.dmDisplayFrequency;
			if (freq == targetRefreshRate)
			{
				#ifndef NO_QUILL
				LOG_TRACE_L2(ld.logger, "[{}] No change requested from {} {} x {} @ {} Hz", ld.prefix,
				             std::wstring{ monitorInfo.szDevice }, width, height, freq);
				#endif
				return S_OK;
			}

			#ifndef NO_QUILL
			LOG_INFO(ld.logger, "[{}] Requesting change from {} {} x {} @ {} Hz to {} Hz", ld.prefix,
			         std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate);
			#endif

			devMode.dmDisplayFrequency = targetRefreshRate;

			auto res = ChangeDisplaySettings(&devMode, 0);

			#ifndef NO_QUILL
			const auto t3 = std::chrono::high_resolution_clock::now();
			const auto getLat = duration_cast<std::chrono::microseconds>(t2 - t1).count();
			const auto chgLat = duration_cast<std::chrono::microseconds>(t3 - t2).count();
			#endif

			switch (res)
			{
			case DISP_CHANGE_SUCCESSFUL:
				#ifndef NO_QUILL
				LOG_INFO(ld.logger, "[{}] Completed change from {} {} x {} @ {} Hz to {} Hz ({:.3f}ms / {:.3f}ms)",
				         ld.prefix, std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate,
				         static_cast<double>(getLat) / 1000, static_cast<double>(chgLat) / 1000);
				#endif
				return S_OK;
			default:
				auto reason =
					res == DISP_CHANGE_FAILED
						? "failed"
						: res == DISP_CHANGE_BADMODE
						? "bad mode"
						: res == DISP_CHANGE_NOTUPDATED
						? "not updated"
						: res == DISP_CHANGE_BADFLAGS
						? "bad flags"
						: res == DISP_CHANGE_BADPARAM
						? "bad param"
						: "?";
				#ifndef NO_QUILL
				LOG_INFO(ld.logger,
				         "[{}] Failed to change from {} {} x {} @ {} Hz to {} Hz due to {} / {} ({:.3f}ms / {:.3f}ms)",
				         ld.prefix, std::wstring{ monitorInfo.szDevice }, width, height, freq, targetRefreshRate, res,
				         reason, static_cast<double>(getLat) / 1000, static_cast<double>(chgLat) / 1000);
				#endif
				return E_FAIL;
			}
		}
		return E_FAIL;
	}
}

enum mode_request : uint8_t
{
	REFRESH_RATE,
	MC_PROFILE
};

struct refresh_rate_switch
{
	std::wstring displayStatus{};
	int refreshRate{0};
};

struct mc_profile_switch
{
	std::wstring profile{};
	bool success{false};
};

struct mode_switch_result
{
	mode_request request;
	refresh_rate_switch rateSwitch;
	mc_profile_switch profileSwitch;
};

class AsyncModeSwitcher final : public CMsgThread
{
public:
	AsyncModeSwitcher(const std::string& pLogPrefix,
	                  std::optional<std::function<void(mode_switch_result)>> pOnModeSwitch = {});

	LRESULT ThreadMessageProc(UINT uMsg, DWORD dwFlags, LPVOID lpParam, CAMEvent* pEvent) override;

	void OnThreadInit() override;

	void InitIfNecessary();

private:
	log_data mLogData{};
	std::optional<std::function<void(mode_switch_result)>> mOnModeSwitch;
};
#endif
