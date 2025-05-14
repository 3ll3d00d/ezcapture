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
#include <immintrin.h>

namespace
{
	// v210 format
	// 12 10-bit unsigned components are packed into four 32-bit little-endian words hence
	// each block specifies the following samples in decreasing address order
	// V0 Y0 U0
	// Y2 U2 Y1
	// U4 Y3 V2
	// Y5 V4 Y4
	// which needs to get written out as
	// y plane  : Y0 Y1 Y2 Y3 Y4 Y5
	// uv plane : U0 V0 U2 V2 U4 V4
	// each line of video is aligned on a 128 byte boundary & 6 pixels fit into 16 bytes so 48 pixels fit in 128 bytes
	#ifdef __AVX__
	bool convert(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height)
	{
		const int groupsPerLine = width / 12;

		const __m256i mask_s0_s2 = _mm256_set1_epi32(0x3FF003FF);
		const __m256i shift_s0_s2 = _mm256_set1_epi32(0x00040040);

		const __m256i mask_s1 = _mm256_set1_epi32(0x000FFC00);

		const uint8_t y_blend_mask = 0b01010101;
		const __m256i y_shuffle_mask = _mm256_setr_epi8(
			0, 1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1,
			0, 1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1
		);

		const uint8_t uv_blend_mask = 0b10101010;
		const __m256i uv_shuffle_mask = _mm256_setr_epi8(
			0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1,
			0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1
		);

		const __m256i lower_192_perm = _mm256_setr_epi32(0, 1, 2, 4, 5, 6, 7, 7);

		// Process all lines with a single loop implementation
		for (int lineNo = 0; lineNo < height; ++lineNo)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + lineNo * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * width * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * width * 2);

			// Process all complete groups
			int g = 0;
			for (; g < groupsPerLine - (lineNo == height - 1 ? 1 : 0); ++g)
			{
				// Load 8 dwords
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				// extract & align 10-bit components across 2 vectors
				__m256i s0_s2 = _mm256_mullo_epi16(_mm256_and_si256(dwords, mask_s0_s2), shift_s0_s2);
				__m256i s1 = _mm256_srli_epi32(_mm256_and_si256(dwords, mask_s1), 4);

				// blend & shuffle & permute to align samples in lower 192bits
				__m256i y_s = _mm256_shuffle_epi8(_mm256_blend_epi32(s0_s2, s1, y_blend_mask), y_shuffle_mask);
				__m256i y = _mm256_permutevar8x32_epi32(y_s, lower_192_perm);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineY), y);

				__m256i uv_s = _mm256_shuffle_epi8(_mm256_blend_epi32(s0_s2, s1, uv_blend_mask), uv_shuffle_mask);
				__m256i uv = _mm256_permutevar8x32_epi32(uv_s, lower_192_perm);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineUV), uv);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}

			// Handle last group for the last line with potential overflow
			if (lineNo == height - 1 && g < groupsPerLine)
			{
				// Load 8 dwords
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				// extract & align 10-bit components across 2 vectors
				__m256i s0_s2 = _mm256_mullo_epi16(_mm256_and_si256(dwords, mask_s0_s2), shift_s0_s2);
				__m256i s1 = _mm256_srli_epi32(_mm256_and_si256(dwords, mask_s1), 4);

				// blend & shuffle & permute to align samples in lower 192bits
				__m256i y_s = _mm256_shuffle_epi8(_mm256_blend_epi32(s0_s2, s1, y_blend_mask), y_shuffle_mask);
				__m256i y = _mm256_permutevar8x32_epi32(y_s, lower_192_perm);
				alignas(32) uint16_t tmpY[16] = { 0 };
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmpY), y);

				__m256i uv_s = _mm256_shuffle_epi8(_mm256_blend_epi32(s0_s2, s1, uv_blend_mask), uv_shuffle_mask);
				__m256i uv = _mm256_permutevar8x32_epi32(uv_s, lower_192_perm);
				alignas(32) uint16_t tmpUV[16] = { 0 };
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmpUV), uv);

				// Calculate remaining pixels to avoid writing past the end of the buffer
				const int remainingPixels = width - g * 12;
				const size_t bytesToCopy = std::min(24, remainingPixels * 2); // 2 bytes per pixel

				std::memcpy(dstLineY, tmpY, bytesToCopy);
				std::memcpy(dstLineUV, tmpUV, bytesToCopy);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}
		}

		return true;
	}
	#else
	bool convert(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height)
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

				// reorder each 32bit block from
				// V0 Y0 U0 
				// Y2 U2 Y1
				// U4 Y3 V2
				// Y5 V4 Y4
				// to
				// [U0, Y0, V0, Y1, U2, Y2, V2, Y3, U4, Y4, V4, Y5]
				samples[0] = p[0] & 0x3FF;			// U0
				samples[1] = (p[0] >> 10) & 0x3FF;  // Y0
				samples[2] = (p[0] >> 20) & 0x3FF;  // V0

				samples[3] = p[1] & 0x3FF;          // Y1
				samples[4] = (p[1] >> 10) & 0x3FF;  // U2
				samples[5] = (p[1] >> 20) & 0x3FF;  // Y2

				samples[6] = p[2] & 0x3FF;          // V2
				samples[7] = (p[2] >> 10) & 0x3FF;  // Y3
				samples[8] = (p[2] >> 20) & 0x3FF;  // U4

				samples[9] = p[3] & 0x3FF;          // Y4
				samples[10] = (p[3] >> 10) & 0x3FF; // V4
				samples[11] = (p[3] >> 20) & 0x3FF; // Y5

				// shift to 16bit and store Y plane
				dstLineY[0] = samples[1] << 6; // Y0
				dstLineY[1] = samples[3] << 6; // Y1
				dstLineY[2] = samples[5] << 6; // Y2
				dstLineY[3] = samples[7] << 6; // Y3
				dstLineY[4] = samples[9] << 6; // Y4
				dstLineY[5] = samples[11] << 6; // Y5

				// shift to 16bit and store UV plane
				dstLineUV[0] = samples[0] << 6; // U0
				dstLineUV[1] = samples[2] << 6; // V0
				dstLineUV[2] = samples[4] << 6; // U2
				dstLineUV[3] = samples[6] << 6; // V2
				dstLineUV[4] = samples[8] << 6; // U4
				dstLineUV[5] = samples[10] << 6; // V4

				// shift the pointer to the next group
				dstLineY += 6;
				dstLineUV += 6;
				srcLine += 4;
			}
		}
		return true;
	}
	#endif
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

	convert(sourceData, srcStride, yPlane, uvPlane, width, height);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to P210 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
