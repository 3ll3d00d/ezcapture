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

#include "V210_P210.h"
#include <atlcomcli.h>
#include <cstdint>
#include <quill/StopWatch.h>
#include <span>

namespace
{
	// Convert V210 to P210 format using AVX2 instructions
	// V210: 10-bit YUV 4:2:2 packed in 32-bit words: [U0 Y0 V0][Y1 U1 Y2][V1 Y3 U2]...
	// P210: 10-bit YUV 4:2:2 planar format in 16-bit containers:
	//       - Y plane: 16-bit values with 10-bit data (LSB aligned)
	//       - U plane: 16-bit values with 10-bit data (LSB aligned)
	//       - V plane: 16-bit values with 10-bit data (LSB aligned)
	//
	bool convert(const uint32_t* src, uint16_t* p210Y, uint16_t* p210U, uint16_t* p210V, int width, int height)
	{
		uint16_t* dstY = p210Y;
		uint16_t* dstU = p210U;
		uint16_t* dstV = p210V;

		// Process each row
		for (int y = 0; y < height; y++)
		{
			// Process remaining pixels using scalar code
			for (int x = 0; x < width; x += 6)
			{
				// Process each 128-bit block (32 bits * 4)
				// Word 0: Cr0 (9:0), Y0 (19:10), Cb0 (29:20)
				uint32_t word0 = *src++;
				uint16_t u0 = static_cast<uint16_t>((word0 >> 20) & 0x3FF); // Cb0
				uint16_t y0 = static_cast<uint16_t>((word0 >> 10) & 0x3FF); // Y0
				uint16_t v0 = static_cast<uint16_t>(word0 & 0x3FF); // Cr0

				// Word 1: Y1 (9:0), Cb1 (19:10), Y2 (29:20)
				uint32_t word1 = *src++;
				uint16_t y2 = static_cast<uint16_t>((word1 >> 20) & 0x3FF); // Y2
				uint16_t u1 = static_cast<uint16_t>((word1 >> 10) & 0x3FF); // Cb1
				uint16_t y1 = static_cast<uint16_t>(word1 & 0x3FF); // Y1

				// Word 2: Cb2 (9:0), Y3 (19:10), Cr1 (29:20)
				uint32_t word2 = *src++;
				uint16_t v1 = static_cast<uint16_t>((word2 >> 20) & 0x3FF); // Cr1
				uint16_t y3 = static_cast<uint16_t>((word2 >> 10) & 0x3FF); // Y3
				uint16_t u2 = static_cast<uint16_t>(word2 & 0x3FF); // Cb2

				// Word 3: Y4 (9:0), Cr2 (19:10), Y5 (29:20)
				uint32_t word3 = *src++;
				uint16_t y5 = static_cast<uint16_t>((word3 >> 20) & 0x3FF); // Y5
				uint16_t v2 = static_cast<uint16_t>((word3 >> 10) & 0x3FF); // Cr2
				uint16_t y4 = static_cast<uint16_t>(word3 & 0x3FF); // Y4

				// Scale 10-bit values to the correct bit position in 16-bit words
				// Left-shift by 6 to align with MSB for P210 format
				y0 <<= 6;
				y1 <<= 6;
				y2 <<= 6;
				y3 <<= 6;
				y4 <<= 6;
				y5 <<= 6;
				u0 <<= 6;
				u1 <<= 6;
				u2 <<= 6;
				v0 <<= 6;
				v1 <<= 6;
				v2 <<= 6;

				// Store the luma samples
				int remaining = width - x;
				if (remaining > 0) dstY[0] = y0;
				if (remaining > 1) dstY[1] = y1;
				if (remaining > 2) dstY[2] = y2;
				if (remaining > 3) dstY[3] = y3;
				if (remaining > 4) dstY[4] = y4;
				if (remaining > 5) dstY[5] = y5;

				// Store the chroma samples (4:2:2 has half the U,V samples)
				remaining = (width - x) / 2;
				if (remaining > 0) dstU[0] = u0;
				if (remaining > 1) dstU[1] = u1;
				if (remaining > 2) dstU[2] = u2;
				if (remaining > 0) dstV[0] = v0;
				if (remaining > 1) dstV[1] = v1;
				if (remaining > 2) dstV[2] = v2;

				dstY += 6;
				dstU += 3;
				dstV += 3;
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
	const auto pixelCount = width * height;

	void* d;
	srcFrame->Start(&d);
	const uint32_t* sourceData = static_cast<const uint32_t*>(d);

	BYTE* outData;
	dstFrame->GetPointer(&outData);
	auto dstSize = dstFrame->GetSize();

	// P210 format: 16-bit samples, full res Y plane, half-width U and V planes
	auto ySize = pixelCount * sizeof(uint16_t);
	auto uSize = pixelCount * sizeof(uint8_t);
	auto vSize = pixelCount * sizeof(uint8_t);

	auto outSpan = std::span(outData, dstSize);
	uint16_t* yPlane = reinterpret_cast<uint16_t*>(outSpan.subspan(0, ySize).data());
	uint16_t* uPlane = reinterpret_cast<uint16_t*>(outSpan.subspan(ySize, uSize).data());
	uint16_t* vPlane = reinterpret_cast<uint16_t*>(outSpan.subspan(ySize + uSize, vSize).data());

	#ifndef NO_QUILL
	auto delta = ySize + uSize + vSize - dstSize;
	if (delta != 0)
	{
		LOG_TRACE_L2(mLogData.logger, "[{}] Plane length mismatch in V210 - P210 conversion {} + {} + {} - {} = {}",
		             mLogData.prefix, ySize, uSize, vSize, dstSize, delta);
	}
	#endif

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convert(sourceData, yPlane, uPlane, vPlane, width, srcFrame->GetVideoFormat().cy);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to P210 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
