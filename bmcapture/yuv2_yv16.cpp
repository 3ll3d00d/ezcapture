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

#include "yuv2_yv16.h"
#include <atlcomcli.h>
#include <cstdint>
#include <quill/StopWatch.h>
#include <span>

namespace
{
	bool convert(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width, int height)
	{
		auto srcStride = width * 2;
		auto yStride = width;
		auto uvStride = width / 2;

		for (int y = 0; y < height; ++y)
		{
			const uint8_t* srcRow = src + y * srcStride;
			uint8_t* y_row = yPlane + y * yStride;
			uint8_t* u_row = uPlane + (y / 2) * uvStride;
			uint8_t* v_row = vPlane + (y / 2) * uvStride;

			for (int x = 0; x < width; x += 2)
			{
				const uint8_t* pixel = srcRow + x * 2;

				// Extract Y values
				y_row[x] = pixel[1];
				if (x + 1 < width)
				{
					y_row[x + 1] = pixel[3];
				}

				// Process U and V only for even rows (vertical subsampling)
				if (y % 2 == 0)
				{
					u_row[x / 2] = pixel[0];
					v_row[x / 2] = pixel[2];
				}
			}
		}
		return true;
	}
}

HRESULT yuv2_yv16::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
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
	auto dstSize = dstFrame->GetSize();

	// YV16 format: 
	auto ySize = pixelCount;
	auto uvSize = pixelCount / 2;
	// split the dst buffer into chunks for alignment purposes (to account for possible padding by the renderer)
	auto yChunk = dstSize / 2;
	auto uvChunk = yChunk / 2;

	auto outSpan = std::span(outData, dstSize);
	uint8_t* yPlane = outSpan.subspan(0, ySize).data();
	uint8_t* uPlane = outSpan.subspan(yChunk, uvSize).data();
	uint8_t* vPlane = outSpan.subspan(yChunk + uvChunk, uvSize).data();

	#ifndef NO_QUILL
	auto delta = yChunk + uvChunk * 2 - dstSize;
	if (delta != 0)
	{
		LOG_TRACE_L2(mLogData.logger, "[{}] Plane length mismatch in YUV2 - YV16 conversion {} + {} * 2 - {} = {}",
		             mLogData.prefix, yChunk, uvChunk, dstSize, delta);
	}
	#endif

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convert(sourceData, yPlane, uPlane, vPlane, width, srcFrame->GetVideoFormat().cy);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to YV16 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
