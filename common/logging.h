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
#ifndef LOGGING_HEADER
#define LOGGING_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>

// Quill is always compiled in because it carries the (opt-in) Prometheus metrics. NO_LOGGING only
// compiles out the log statements and the log file sinks.
#include <quill/Logger.h>
#include <quill/Frontend.h>
#include "quill/LogMacros.h"
#include "quill/std/WideString.h"

inline constexpr std::string_view audioLatencyLoggerName{"aLat"};
inline constexpr std::string_view videoLatencyLoggerName{"vLat"};
inline constexpr std::string_view filterLoggerName{"filter"};

template <uint32_t QueueCapacity>
struct EzFrontendOptions
{
	static constexpr quill::QueueType queue_type{quill::QueueType::BoundedDropping};
	static constexpr uint32_t initial_queue_capacity{QueueCapacity};
	static constexpr uint32_t blocking_queue_retry_interval_ns{800};
	static constexpr size_t unbounded_queue_max_capacity = 2ull * 1024u * 1024u * 1024u;
	static constexpr quill::HugePagesPolicy huge_pages_policy = quill::HugePagesPolicy::Never;
};

// Distinct types (rather than one type whose capacity depends on NO_LOGGING) so translation units built
// with different NO_LOGGING settings (e.g. the tests) never share mismatched quill template instantiations.
#ifdef NO_LOGGING
// only metric samples (a few small records per frame) are ever queued
using CustomFrontendOptions = EzFrontendOptions<1024 * 1024>; // 1MiB
#else
using CustomFrontendOptions = EzFrontendOptions<64 * 1024 * 1024>; // 64MiB
#endif

using CustomFrontend = quill::FrontendImpl<CustomFrontendOptions>;
using CustomLogger = quill::LoggerImpl<CustomFrontendOptions>;

struct log_data
{
	#ifndef NO_LOGGING
	CustomLogger* logger = nullptr;
	CustomLogger* audioLat = nullptr;
	CustomLogger* videoLat = nullptr;
	#endif
	std::string prefix{};

	void init(const std::string& pPrefix)
	{
		prefix = pPrefix;
		#ifndef NO_LOGGING
		logger = CustomFrontend::get_logger(std::string{filterLoggerName});
		audioLat = CustomFrontend::get_logger(std::string{audioLatencyLoggerName});
		videoLat = CustomFrontend::get_logger(std::string{videoLatencyLoggerName});
		#endif
	}
};
#endif
