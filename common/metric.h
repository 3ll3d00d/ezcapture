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
#ifndef METRIC_HEADER
#define METRIC_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <algorithm>
#include <cstdint>

class metric
{
public:
	metric(uint16_t sz = 24) : mSize(0), mCapacity(sz)
	{
	}

	void resize(uint16_t sz)
	{
		mCapacity = sz;
	}

	bool sample(uint64_t sample)
	{
		if (sample <= 0) return false;
		mMin = std::min(sample, mMin);
		mMax = std::max(sample, mMax);
		mSum += sample;
		if (++mSize >= mCapacity)
		{
			mSnappedMin = mMin;
			mSnappedMean = static_cast<double>(mSum) / mCapacity;
			mSnappedMax = mMax;
			mSize = 0;
			mSum = mMax = 0;
			mMin = std::numeric_limits<uint64_t>::max();
			return true;
		}
		return false;
	}

	uint64_t min() const
	{
		return mSnappedMin;
	}

	double mean() const
	{
		return mSnappedMean;
	}

	uint64_t max() const
	{
		return mSnappedMax;
	}

private:
	uint16_t mSize;
	uint16_t mCapacity;
	uint64_t mMin{std::numeric_limits<uint64_t>::max()};
	uint64_t mMax{0};
	uint64_t mSum{0};
	uint64_t mSnappedMin{0};
	uint64_t mSnappedMax{0};
	double mSnappedMean{0.0};
};
#endif
