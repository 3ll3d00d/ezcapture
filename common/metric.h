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
#include <numeric>
#include <vector>

class metric
{
public:
    metric(uint8_t sz = 24)
    {
        mSamples.reserve(sz);
        mSamples.resize(sz);
        mCapacity = sz;
    }

    bool sample(uint64_t sample)
    {
        mSamples[mIdx++] = sample;
        if (mIdx == mSamples.capacity())
        {
            mIdx = 0;
            return true;
        }
        return false;
    }

    double mean() const
    {
        return static_cast<double>(std::reduce(mSamples.begin(), mSamples.end())) / mCapacity;
    }

    uint64_t min() const
    {
        return *std::ranges::min_element(mSamples);
    }

    uint64_t max() const
    {
        return *std::ranges::max_element(mSamples);
    }

private:
    std::vector<uint64_t> mSamples;
    uint8_t mIdx{0};
    uint8_t mCapacity;
};
