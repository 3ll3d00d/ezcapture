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

#include "r210_bgr10.h"
#include <atlcomcli.h>
#include <cstdint>
#include <quill/StopWatch.h>
#include <span>

namespace
{
	bool convert(const uint8_t* src, uint32_t* dst, size_t width, size_t height)
	{
		// Each row must start on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint32_t* dstRow = dst;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixel = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				uint32_t packed = srcPixel[x];

				// Unpack red, green, blue from packed bits
				uint32_t red = ((packed >> 22) & 0x3F0) | ((packed >> 16) & 0x00F);
				uint32_t green = ((packed >> 12) & 0x3C0) | ((packed >> 8) & 0x03F);
				uint32_t blue = ((packed >> 6) & 0x300) | ((packed >> 0) & 0x0FF);

				dstRow[x] = ((blue & 0x3FF) << 0)
					| ((green & 0x3FF) << 10)
					| ((red & 0x3FF) << 20);
			}

			srcRow += srcStride;
			dstRow += width;
		}
		return true;
	}
}

HRESULT r210_bgr10::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	if (S_OK != CheckFrameSizes(srcFrame->GetFrameIndex(), mExpectedImageSize, dstFrame))
	{
		return S_FALSE;
	}

	const auto width = srcFrame->GetVideoFormat().cx;
	const auto height = srcFrame->GetVideoFormat().cy;
	const auto pixelCount = width * height;

	void* d;
	srcFrame->Start(&d);
	const uint8_t* sourceData = static_cast<const uint8_t*>(d);

	BYTE* outData;
	dstFrame->GetPointer(&outData);
	uint32_t* dst = reinterpret_cast<uint32_t*>(outData);

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convert(sourceData, dst, width, height);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to BGR10 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
