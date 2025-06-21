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
#include "modeswitcher.h"

#include <tchar.h>

#include "mccommands.h"

AsyncModeSwitcher::AsyncModeSwitcher(const std::string& pLogPrefix,
                                     std::optional<std::function<void(mode_switch_result)>> pOnModeSwitch) :
	mOnModeSwitch(std::move(pOnModeSwitch))
{
	mLogData.init(pLogPrefix);
}

LRESULT AsyncModeSwitcher::ThreadMessageProc(UINT uMsg, DWORD dwFlags, LPVOID lpParam, CAMEvent* pEvent)
{
	auto hr = S_OK;
	switch (uMsg)
	{
	case REFRESH_RATE:
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Processing REFRESH_RATE switch to {} Hz", mLogData.prefix, dwFlags);
		#endif
		if (S_OK == mode_switch::ChangeResolution(mLogData, dwFlags) && mOnModeSwitch)
		{
			auto values = mode_switch::GetDisplayStatus();
			mode_switch_result result = {
				.request = REFRESH_RATE,
				.rateSwitch = {
					.displayStatus = std::get<0>(values),
					.refreshRate = std::get<1>(values)
				}
			};
			mOnModeSwitch.value()(std::move(result));
		}
		break;
	case MC_PROFILE:
		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Processing MC_PROFILE switch : {}", mLogData.prefix, dwFlags);
		#endif

		if (HWND mcWnd = FindWindow(_T("MJFrame"), nullptr))
		{
			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Sending MCC_JRVR_PROFILE_OUTPUT {}", mLogData.prefix, dwFlags);
			#endif
			if (dwFlags == 0)
			{
				PostMessage(mcWnd, WM_MC_COMMAND, MCC_JRVR_PROFILE_OUTPUT, -1LL);
			}
			else
			{
				PostMessage(mcWnd, WM_MC_COMMAND, MCC_JRVR_PROFILE_OUTPUT, dwFlags);
			}

			#ifndef NO_QUILL
			LOG_TRACE_L2(mLogData.logger, "[{}] Sent MCC_JRVR_PROFILE_OUTPUT {}", mLogData.prefix, dwFlags);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to find MJFrame, command not sent", mLogData.prefix);
			#endif
		}
		break;
	case SHUTDOWN_NOW:
		#ifndef NO_QUILL
		LOG_TRACE_L2(mLogData.logger, "[{}] Shutting down now", mLogData.prefix);
		#endif
		hr = E_ABORT;
		break;
	default:
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Ignoring unknown mode switch request {} {}", mLogData.prefix, uMsg, dwFlags);
		#endif
		break;
	}

	return hr;
}

void AsyncModeSwitcher::OnThreadInit()
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] AsyncModeSwitcher::OnThreadInit", mLogData.prefix);
	#endif
}

void AsyncModeSwitcher::InitIfNecessary()
{
	if (GetThreadHandle() == nullptr)
	{
		if (CreateThread())
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Initialised refresh rate switcher thread with id {}", mLogData.prefix,
			         GetThreadId());
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Failed to initialise refresh rate switcher thread", mLogData.prefix);
			#endif
		}
	}
}
