#pragma once

#include <string>
#include <utility>

#ifndef NO_QUILL
#include <quill/Logger.h>
#include <quill/Frontend.h>
#include "quill/LogMacros.h"
#include "quill/std/WideString.h"

struct CustomFrontendOptions
{
	static constexpr quill::QueueType queue_type{ quill::QueueType::BoundedDropping };
	static constexpr uint32_t initial_queue_capacity{ 64 * 1024 * 1024 }; // 64MiB
	static constexpr uint32_t blocking_queue_retry_interval_ns{ 800 };
	static constexpr size_t unbounded_queue_max_capacity = 2ull * 1024u * 1024u * 1024u;
	static constexpr quill::HugePagesPolicy huge_pages_policy = quill::HugePagesPolicy::Never;
};

using CustomFrontend = quill::FrontendImpl<CustomFrontendOptions>;
using CustomLogger = quill::LoggerImpl<CustomFrontendOptions>;
#endif

struct log_data
{
	#ifndef NO_QUILL
	CustomLogger* logger = nullptr;
	#endif
	std::string prefix{};
};
