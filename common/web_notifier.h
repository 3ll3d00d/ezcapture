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
#ifndef WEB_NOTIFIER_HEADER
#define WEB_NOTIFIER_HEADER

#include "status_snapshot.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

typedef void* HINTERNET;
typedef struct _TP_TIMER TP_TIMER, *PTP_TIMER;
typedef struct _TP_CALLBACK_INSTANCE TP_CALLBACK_INSTANCE, *PTP_CALLBACK_INSTANCE;

// what a webhook is sent
struct notify_event
{
	status_summary summary;
	filter_run_state state{filter_run_state::STOPPED};

	bool operator==(const notify_event&) const = default;
};

/*
 * POSTs the summary to a URL, e.g. a Home Assistant webhook, when it changes. Notify only records the latest event
 * and arms a timer so it can be called from any thread, with locks held; the send happens on the Windows thread
 * pool, debounced so a signal that drops for a moment sends one update. There are no retries, a failure is logged
 * and shown in Status().
 */
class web_notifier
{
public:
	/**
	 * @param debounce how long to wait after a change for more changes before sending.
	 * @param timeout the connect, send and receive timeout.
	 */
	web_notifier(std::string filterId, std::string filterName,
	             std::chrono::milliseconds debounce = std::chrono::milliseconds{500},
	             std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});
	web_notifier(const web_notifier&) = delete;
	web_notifier& operator=(const web_notifier&) = delete;
	// aborts a send in progress rather than waiting for it to time out
	~web_notifier();

	// an empty string on success, otherwise why the URL cannot be used
	std::string Start(const std::string& url);

	void Notify(const notify_event& event);

	// e.g. "Last sent 200" or the last error
	std::string Status() const;

	// the request body
	static std::string Payload(const std::string& filterId, const std::string& filterName, const notify_event& event);

private:
	static void __stdcall OnTimer(PTP_CALLBACK_INSTANCE instance, void* context, PTP_TIMER timer);
	void Send();

	std::string mFilterId;
	std::string mFilterName;
	std::chrono::milliseconds mDebounce;
	std::chrono::milliseconds mTimeout;

	// the parsed URL
	std::wstring mHost;
	std::wstring mPath;
	uint16_t mPort{0};
	bool mSecure{false};

	HINTERNET mSession{nullptr};
	PTP_TIMER mTimer{nullptr};

	// one send at a time, a timer callback that overlaps a send waits and then sends the latest event
	std::mutex mSendLock;

	mutable std::mutex mLock;
	// guarded by mLock
	std::optional<notify_event> mPending;
	std::optional<notify_event> mLastSent;
	bool mArmed{false};
	bool mStopping{false};
	HINTERNET mRequest{nullptr};
	std::string mStatus{"Nothing sent yet"};
};

#endif
