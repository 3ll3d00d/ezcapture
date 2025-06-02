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
#pragma once

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
		if (sample == 0) return false;
		mMin = std::min(sample, mMin);
		mMax = std::max(sample, mMax);
		mSum += sample;
		if (++mSize >= mCapacity)
		{
			mMean = static_cast<double>(mSum) / mCapacity;
			mSize = 0;
			return true;
		}
		return false;
	}

	double mean() const
	{
		return mMean;
	}

	uint64_t min() const
	{
		return mMin;
	}

	uint64_t max() const
	{
		return mMax;
	}

private:
	uint16_t mSize;
	uint16_t mCapacity;
	uint64_t mMin{0};
	uint64_t mMax{0};
	uint64_t mSum{0};
	double mMean{0.0};
};
