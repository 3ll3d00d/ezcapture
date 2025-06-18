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

AsyncModeSwitcher::AsyncModeSwitcher(const std::string& pLogPrefix,
                                     std::optional<std::function<void(mode_switch_result)>> pOnModeSwitch) :
	mOnModeSwitch(std::move(pOnModeSwitch))
{
	mLogData.init(pLogPrefix);
}

LRESULT AsyncModeSwitcher::ThreadMessageProc(UINT uMsg, DWORD dwFlags, LPVOID lpParam, CAMEvent* pEvent)
{
	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] AsyncModeSwitcher::ThreadMessageProc {}", mLogData.prefix, dwFlags);
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
	return S_OK;
}

void AsyncModeSwitcher::OnThreadInit() 
{
	#ifndef NO_QUILL
	CustomFrontend::preallocate();

	LOG_INFO(mLogData.logger, "[{}] AsyncModeSwitcher::OnThreadInit", mLogData.prefix);
	#endif
}
