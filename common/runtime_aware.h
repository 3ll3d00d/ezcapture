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
#ifndef RUNTIME_AWARE_HEADER
#define RUNTIME_AWARE_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "domain.h"
#include "logging.h"

class runtime_aware
{
public:
	void SetStartTime(int64_t streamStartTime)
	{
		mStreamStartTime = streamStartTime;
		mFrameTs.initialise(streamStartTime);

		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetStartTime at {}", mLogData.prefix, streamStartTime);
		#endif
	}

	virtual void SetStopTime(int64_t streamStopTime)
	{
		mStreamStopTime = streamStopTime;

		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] CapturePin::SetStopTime at {}", mLogData.prefix, streamStopTime);
		#endif
	}

	bool IsStreamStarted() const
	{
		return mStreamStartTime > mStreamStopTime;
	}

	bool IsStreamStopped() const
	{
		return !IsStreamStarted();
	}

protected:
	runtime_aware(const std::string& pLogPrefix)
	{
		mLogData.init(pLogPrefix);
	}

	log_data mLogData{};
	int64_t mStreamStartTime{-1LL};
	int64_t mStreamStopTime{-1LL};
	frame_ts mFrameTs{};
};

#endif
