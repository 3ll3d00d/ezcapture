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
#define NOMINMAX
#include <streams.h>
#include <winhttp.h>

#include "web_notifier.h"
#include "web_json.h"

#include <format>

#pragma comment(lib, "winhttp.lib")

web_notifier::web_notifier(std::string filterId, std::string filterName, std::chrono::milliseconds debounce,
                           std::chrono::milliseconds timeout) :
	mFilterId(std::move(filterId)),
	mFilterName(std::move(filterName)),
	mDebounce(debounce),
	mTimeout(timeout)
{
}

web_notifier::~web_notifier()
{
	{
		std::lock_guard lock{mLock};
		mStopping = true;
		if (mRequest != nullptr)
		{
			// a blocked synchronous WinHTTP call on another thread fails straight away once its handle is closed
			WinHttpCloseHandle(mRequest);
			mRequest = nullptr;
		}
	}
	if (mTimer != nullptr)
	{
		SetThreadpoolTimer(mTimer, nullptr, 0, 0);
		WaitForThreadpoolTimerCallbacks(mTimer, TRUE);
		CloseThreadpoolTimer(mTimer);
		mTimer = nullptr;
	}
	if (mSession != nullptr)
	{
		WinHttpCloseHandle(mSession);
		mSession = nullptr;
	}
}

std::string web_notifier::Start(const std::string& url)
{
	const std::wstring wide{url.begin(), url.end()};
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = static_cast<DWORD>(-1);
	parts.dwUrlPathLength = static_cast<DWORD>(-1);
	parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (url.empty() || !WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
		(parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) || parts.dwHostNameLength == 0)
	{
		return std::format("The webhook URL {} is not a valid http or https URL", url);
	}
	mHost.assign(parts.lpszHostName, parts.dwHostNameLength);
	mPath.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.dwExtraInfoLength > 0)
	{
		mPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
	}
	if (mPath.empty())
	{
		mPath = L"/";
	}
	mPort = parts.nPort;
	mSecure = parts.nScheme == INTERNET_SCHEME_HTTPS;

	mSession = WinHttpOpen(L"ezcapture", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
	                       WINHTTP_NO_PROXY_BYPASS, 0);
	if (mSession == nullptr)
	{
		return std::format("Unable to start WinHTTP ({})", GetLastError());
	}
	const auto t = static_cast<int>(mTimeout.count());
	WinHttpSetTimeouts(mSession, t, t, t, t);
	mTimer = CreateThreadpoolTimer(&web_notifier::OnTimer, this, nullptr);
	if (mTimer == nullptr)
	{
		return std::format("Unable to create a timer ({})", GetLastError());
	}
	return {};
}

void web_notifier::Notify(const notify_event& event)
{
	std::lock_guard lock{mLock};
	if (mStopping || mTimer == nullptr)
	{
		return;
	}
	mPending = event;
	if (!mArmed)
	{
		mArmed = true;
		// relative due time in 100ns units
		const auto due = -static_cast<int64_t>(mDebounce.count()) * 10000;
		FILETIME ft;
		ft.dwLowDateTime = static_cast<DWORD>(due & 0xFFFFFFFF);
		ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(due) >> 32);
		SetThreadpoolTimer(mTimer, &ft, 0, 0);
	}
}

std::string web_notifier::Status() const
{
	std::lock_guard lock{mLock};
	return mStatus;
}

std::string web_notifier::Payload(const std::string& filterId, const std::string& filterName,
                                  const notify_event& event)
{
	web_json::writer w;
	w.begin_object();
	w.key("filter").value(filterId);
	w.key("name").value(filterName);
	w.key("state").value(to_string(event.state));
	w.key("summary");
	status_snapshot::WriteSummary(w, event.summary);
	w.end_object();
	return w.take();
}

void __stdcall web_notifier::OnTimer(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
{
	static_cast<web_notifier*>(context)->Send();
}

void web_notifier::Send()
{
	std::lock_guard sendLock{mSendLock};
	notify_event event;
	HINTERNET connect;
	HINTERNET request;
	{
		std::lock_guard lock{mLock};
		mArmed = false;
		if (mStopping || !mPending)
		{
			return;
		}
		event = std::move(*mPending);
		mPending.reset();
		if (mLastSent && *mLastSent == event)
		{
			return;
		}
		connect = WinHttpConnect(mSession, mHost.c_str(), mPort, 0);
		if (connect == nullptr)
		{
			mStatus = std::format("Unable to send to the webhook ({})", GetLastError());
			return;
		}
		request = WinHttpOpenRequest(connect, L"POST", mPath.c_str(), nullptr, WINHTTP_NO_REFERER,
		                             WINHTTP_DEFAULT_ACCEPT_TYPES, mSecure ? WINHTTP_FLAG_SECURE : 0);
		if (request == nullptr)
		{
			WinHttpCloseHandle(connect);
			mStatus = std::format("Unable to send to the webhook ({})", GetLastError());
			return;
		}
		// the destructor closes it to abort the send
		mRequest = request;
	}

	auto body = Payload(mFilterId, mFilterName, event);
	DWORD statusCode = 0;
	DWORD error = 0;
	if (WinHttpSendRequest(request, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1L), body.data(),
	                       static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
		WinHttpReceiveResponse(request, nullptr))
	{
		DWORD size = sizeof(statusCode);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		                    &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
	}
	else
	{
		error = GetLastError();
	}

	std::lock_guard lock{mLock};
	if (mRequest != nullptr)
	{
		WinHttpCloseHandle(mRequest);
		mRequest = nullptr;
	}
	WinHttpCloseHandle(connect);
	if (error != 0)
	{
		mStatus = error == ERROR_WINHTTP_TIMEOUT
			          ? "The webhook did not answer in time"
			          : std::format("Unable to send to the webhook ({})", error);
	}
	else if (statusCode < 200 || statusCode >= 300)
	{
		mStatus = std::format("The webhook answered {}", statusCode);
	}
	else
	{
		mStatus = std::format("Last sent, answered {}", statusCode);
		mLastSent = std::move(event);
	}
}
