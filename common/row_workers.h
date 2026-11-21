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
#ifndef ROW_WORKERS_HEADER
#define ROW_WORKERS_HEADER

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

/**
 * A small pool of persistent threads that splits work on a frame into bands of rows. run() gives the caller the first
 * band and each worker one of the others, and returns once every band is done. Threads stay alive between frames so
 * there is no thread creation per frame, only a wake up.
 */
class row_workers
{
public:
	explicit row_workers(unsigned workers)
	{
		mThreads.reserve(workers);
		for (unsigned i = 0; i < workers; ++i)
		{
			mThreads.emplace_back([this, i] { work(i + 1); });
		}
	}

	~row_workers()
	{
		{
			std::lock_guard lock{mLock};
			mStop = true;
		}
		mWake.notify_all();
		for (auto& t : mThreads)
		{
			t.join();
		}
	}

	row_workers(const row_workers&) = delete;
	row_workers& operator=(const row_workers&) = delete;

	// e.g. THREAD_PRIORITY_TIME_CRITICAL to match a capture thread
	void set_priority(int priority)
	{
		for (auto& t : mThreads)
		{
			SetThreadPriority(t.native_handle(), priority);
		}
	}

	unsigned bands() const { return static_cast<unsigned>(mThreads.size()) + 1; }

	// calls fn(begin, end) for every band of [0, rows), concurrently, and waits for all of them
	void run(uint32_t rows, const std::function<void(uint32_t, uint32_t)>& fn)
	{
		{
			std::lock_guard lock{mLock};
			mJob = &fn;
			mRows = rows;
			mPending = static_cast<unsigned>(mThreads.size());
			++mGeneration;
		}
		mWake.notify_all();
		do_band(0, fn, rows);
		std::unique_lock lock{mLock};
		mDone.wait(lock, [this] { return mPending == 0; });
		mJob = nullptr;
	}

private:
	void do_band(unsigned band, const std::function<void(uint32_t, uint32_t)>& fn, uint32_t rows) const
	{
		const auto n = static_cast<uint64_t>(bands());
		const auto begin = static_cast<uint32_t>(rows * band / n);
		const auto end = static_cast<uint32_t>(rows * (band + 1) / n);
		if (begin < end)
		{
			fn(begin, end);
		}
	}

	void work(unsigned band)
	{
		uint64_t seen = 0;
		while (true)
		{
			const std::function<void(uint32_t, uint32_t)>* job;
			uint32_t rows;
			{
				std::unique_lock lock{mLock};
				mWake.wait(lock, [this, seen] { return mStop || mGeneration != seen; });
				if (mStop)
				{
					return;
				}
				seen = mGeneration;
				job = mJob;
				rows = mRows;
			}
			do_band(band, *job, rows);
			{
				std::lock_guard lock{mLock};
				if (--mPending == 0)
				{
					mDone.notify_one();
				}
			}
		}
	}

	std::vector<std::thread> mThreads;
	std::mutex mLock;
	std::condition_variable mWake;
	std::condition_variable mDone;
	const std::function<void(uint32_t, uint32_t)>* mJob{nullptr};
	uint32_t mRows{0};
	unsigned mPending{0};
	uint64_t mGeneration{0};
	bool mStop{false};
};

#endif
