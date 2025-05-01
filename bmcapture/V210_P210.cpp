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

#include "v210_p210.h"
#include <atlcomcli.h>
#include <cstdint>
#include <quill/StopWatch.h>
#include <span>

namespace
{
	bool convertScalar(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height)
	{
		// Each group of 16 bytes contains 6 pixels (YUVYUV)
		const int groupsPerLine = width / 6;
		uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY);
		uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV);

		for (int y = 0; y < height; y++)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + y * srcStride);
			for (int g = 0; g < groupsPerLine; ++g)
			{
				// Load 4 dwords (16 bytes = 128 bits)
				__m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcLine));

				// Unpack 12 16 bit samples to a 10 bit value manually
				alignas(32) uint16_t samples[12];
				const uint32_t* p = reinterpret_cast<const uint32_t*>(&v);

				samples[0] = p[0] & 0x3FF;
				samples[1] = (p[0] >> 10) & 0x3FF;
				samples[2] = (p[0] >> 20) & 0x3FF;

				samples[3] = p[1] & 0x3FF;
				samples[4] = (p[1] >> 10) & 0x3FF;
				samples[5] = (p[1] >> 20) & 0x3FF;

				samples[6] = p[2] & 0x3FF;
				samples[7] = (p[2] >> 10) & 0x3FF;
				samples[8] = (p[2] >> 20) & 0x3FF;

				samples[9] = p[3] & 0x3FF;
				samples[10] = (p[3] >> 10) & 0x3FF;
				samples[11] = (p[3] >> 20) & 0x3FF;

				// Now samples array has: [U0, Y0, V0, Y1, U2, Y2, V2, Y3, U4, Y4, V4, Y5]

				// Arrange and store Y plane
				dstLineY[0] = samples[1] << 6; // Y0
				dstLineY[1] = samples[3] << 6; // Y1
				dstLineY[2] = samples[5] << 6; // Y2
				dstLineY[3] = samples[7] << 6; // Y3
				dstLineY[4] = samples[9] << 6; // Y4
				dstLineY[5] = samples[11] << 6; // Y5

				// Arrange and store UV interleaved plane
				dstLineUV[0] = samples[0] << 6; // U0
				dstLineUV[1] = samples[2] << 6; // V0
				dstLineUV[2] = samples[4] << 6; // U2
				dstLineUV[3] = samples[6] << 6; // V2
				dstLineUV[4] = samples[8] << 6; // U4
				dstLineUV[5] = samples[10] << 6; // V4

				dstLineY += 6;
				dstLineUV += 6;
				srcLine += 4;
			}
		}
		return true;
	}
}

HRESULT v210_p210::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	if (S_OK != CheckFrameSizes(srcFrame->GetFrameIndex(), mExpectedImageSize, dstFrame))
	{
		return S_FALSE;
	}

	const auto width = srcFrame->GetVideoFormat().cx;
	const auto height = srcFrame->GetVideoFormat().cy;

	void* d;
	srcFrame->Start(&d);
	const uint8_t* sourceData = static_cast<const uint8_t*>(d);

	BYTE* outData;
	dstFrame->GetPointer(&outData);
	auto dstSize = dstFrame->GetSize();

	// P210 format: 16-bit samples, full res Y plane, half-width U and V planes
	auto outSpan = std::span(outData, dstSize);
	auto planeSize = dstSize / 2;
	// cut the destination into 2 halves, assumes that if the destination is larger than we expect it's because the renderer
	// (aka JRVR) has padded the buffer for alignment reasons and we can just ignore writing to that region of memory
	uint8_t* yPlane = outSpan.subspan(0, planeSize).data();
	uint8_t* uvPlane = outSpan.subspan(planeSize, planeSize).data();

	#ifndef NO_QUILL
	auto delta = planeSize * 2 - dstSize;
	if (delta != 0)
	{
		LOG_TRACE_L2(mLogData.logger, "[{}] Plane length mismatch in V210 - P210 conversion {} * 2 - {} = {}",
		             mLogData.prefix, planeSize, dstSize, delta);
	}
	#endif

	auto alignedWidth = (width + 47) / 48 * 48;
	auto srcStride = alignedWidth * 8 / 3;

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convertScalar(sourceData, srcStride, yPlane, uvPlane, width, height);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to P210 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
