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

// Micro-benchmark for the production conversion kernels in
// common/conversion_kernels.h (the code the filters actually ship, unlike
// bench.cpp which hosts local research variants). Frames are synthesised in
// memory, so no input file is needed, and the source rotates through several
// buffers so successive frames are not artificially cache-resident.
//
// usage: kernelbench <uyvy|yuy2|v210|y210|r210|bgr10> <width> <height> [frames] [pad]

#include "../common/conversion_kernels.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	constexpr int WARMUP_FRAMES = 50;
	constexpr int SRC_ROTATION = 4;

	std::vector<uint8_t> randomBuffer(size_t bytes, uint32_t seed)
	{
		std::vector<uint8_t> v(bytes);
		uint32_t s = seed;
		for (auto& b : v)
		{
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			b = static_cast<uint8_t>(s);
		}
		return v;
	}

	struct stats
	{
		double meanMicros;
		double medianMicros;
		double minMicros;
	};

	template <typename F>
	stats run(int frames, const F& convertFrame)
	{
		std::vector<double> micros;
		micros.reserve(frames);
		for (int i = 0; i < WARMUP_FRAMES + frames; ++i)
		{
			const auto t1 = std::chrono::steady_clock::now();
			convertFrame(i % SRC_ROTATION);
			const auto t2 = std::chrono::steady_clock::now();
			if (i >= WARMUP_FRAMES)
			{
				micros.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
			}
		}
		std::sort(micros.begin(), micros.end());
		double total = 0;
		for (const auto m : micros) total += m;
		return {total / micros.size(), micros[micros.size() / 2], micros.front()};
	}
}

int main(int argc, char* argv[])
{
	if (argc < 4)
	{
		fprintf(stderr, "usage: kernelbench <uyvy|yuy2|v210|y210|r210|bgr10> <width> <height> [frames] [pad]\n");
		return 1;
	}
	const std::string fmt = argv[1];
	const int width = std::stoi(argv[2]);
	const int height = std::stoi(argv[3]);
	const int frames = argc > 4 ? std::stoi(argv[4]) : 300;
	const int pad = argc > 5 ? std::stoi(argv[5]) : 0;
	if (width <= 0 || height <= 0 || frames <= 0)
	{
		fprintf(stderr, "invalid dimensions/frames\n");
		return 1;
	}

	std::vector<std::vector<uint8_t>> srcs;
	stats result{};

	if (fmt == "uyvy" || fmt == "yuy2")
	{
		const size_t srcSize = static_cast<size_t>(width) * 2 * height;
		const size_t ySize = static_cast<size_t>(width + pad) * height;
		for (int i = 0; i < SRC_ROTATION; ++i) srcs.push_back(randomBuffer(srcSize, 7u + i));
		std::vector<uint8_t> y(ySize), u(ySize / 2), v(ySize / 2);
		auto kernel = fmt == "uyvy" ? conversion::uyvy_to_yv16 : conversion::yuy2_to_yv16;
		result = run(frames, [&](int i)
		{
			kernel(srcs[i].data(), y.data(), u.data(), v.data(), width, height, pad);
		});
	}
	else if (fmt == "v210")
	{
		const int srcStride = (width + 47) / 48 * 48 * 8 / 3;
		const size_t planeSize = static_cast<size_t>(width + pad) * height * 2;
		for (int i = 0; i < SRC_ROTATION; ++i)
			srcs.push_back(randomBuffer(static_cast<size_t>(srcStride) * height, 7u + i));
		std::vector<uint8_t> y(planeSize), uv(planeSize);
		result = run(frames, [&](int i)
		{
			conversion::v210_to_p210(srcs[i].data(), srcStride, y.data(), uv.data(), width, height, pad);
		});
	}
	else if (fmt == "y210")
	{
		const int srcStride = width * 4;
		const size_t planeSize = static_cast<size_t>(width + pad) * height * 2;
		for (int i = 0; i < SRC_ROTATION; ++i)
			srcs.push_back(randomBuffer(static_cast<size_t>(srcStride) * height, 7u + i));
		std::vector<uint8_t> y(planeSize), uv(planeSize);
		result = run(frames, [&](int i)
		{
			conversion::y210_to_p210(srcs[i].data(), srcStride, y.data(), uv.data(), width, height, pad);
		});
	}
	else if (fmt == "r210" || fmt == "bgr10")
	{
		const size_t srcStride = (static_cast<size_t>(width) * 4 + 255) / 256 * 256;
		const size_t dstWords = static_cast<size_t>(width + pad) * height * 3;
		for (int i = 0; i < SRC_ROTATION; ++i) srcs.push_back(randomBuffer(srcStride * height, 7u + i));
		std::vector<uint16_t> rgb(dstWords);
		auto kernel = fmt == "r210" ? conversion::r210_to_rgb48 : conversion::bgr10_to_rgb48;
		result = run(frames, [&](int i)
		{
			kernel(srcs[i].data(), rgb.data(), width, height, pad);
		});
	}
	else
	{
		fprintf(stderr, "unknown format: %s\n", fmt.c_str());
		return 1;
	}

	printf("%s %dx%d pad=%d frames=%d: mean=%.1fus median=%.1fus min=%.1fus\n",
	       fmt.c_str(), width, height, pad, frames, result.meanMicros, result.medianMicros, result.minMicros);
	return 0;
}
