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
#ifndef CONVERSION_KERNELS_HEADER
#define CONVERSION_KERNELS_HEADER

// Pixel format conversion kernels used by the IVideoFrameWriter implementations.
// Standalone (no DirectShow / logging dependencies) so they can also be exercised
// directly by mwcapture-test and benchtest. The AVX2 vs scalar variant of each
// kernel is selected at compile time by /arch:AVX2 (see EZ_ENABLE_AVX2).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>

namespace conversion
{
	#ifdef __AVX2__
	inline bool uyvy_to_yv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int pixelsToPad)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15,
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + pixelsToPad;
		const int uvWidth = yWidth / 2;
		const int vectorWidth = width & ~15; // pixels converted 16 per pass
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + (y * uvWidth));
			for (int x = 0; x < vectorWidth; x += 16) // 16 bits per pixel in 256 bit chunks = 16 pixels per pass
			{
				__m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
				__m256i shuffled = _mm256_shuffle_epi8(pixels, shuffle_1);
				__m256i permuted = _mm256_permutevar8x32_epi32(shuffled, permute);
				const uint64_t* vals = reinterpret_cast<const uint64_t*>(&permuted);

				v_out[0] = vals[0]; // 8 bytes each of UV
				v_out++;
				u_out[0] = vals[1];
				u_out++;

				y_out[0] = vals[2]; // 16 bytes of Y
				y_out[1] = vals[3];
				y_out += 2;

				src += 32; // 32 bytes read
			}
			// scalar tail for widths that are not a multiple of 16
			if (vectorWidth < width)
			{
				uint8_t* yTail = yPlane + (y * yWidth) + vectorWidth;
				uint8_t* uTail = uPlane + (y * uvWidth) + vectorWidth / 2;
				uint8_t* vTail = vPlane + (y * uvWidth) + vectorWidth / 2;
				for (int x = vectorWidth; x < width; x += 2)
				{
					uTail[0] = src[0];
					yTail[0] = src[1];
					vTail[0] = src[2];
					yTail[1] = src[3];

					yTail += 2;
					uTail++;
					vTail++;
					src += 4;
				}
			}
		}
		return true;
	}
	#else
	inline bool uyvy_to_yv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int pixelsToPad)
	{
		for (int y = 0; y < height; ++y)
		{
			auto offset = (y * (width + pixelsToPad));
			uint8_t* yOut = yPlane + offset;
			uint8_t* uOut = uPlane + offset / 2;
			uint8_t* vOut = vPlane + offset / 2;

			for (int x = 0; x < width; x += 2) // 2 pixels per pass
			{
				uOut[0] = src[0];
				yOut[0] = src[1];
				vOut[0] = src[2];
				yOut[1] = src[3];

				yOut += 2;
				uOut++;
				vOut++;
				src += 4;
			}
		}
		return true;
	}
	#endif

	#ifdef __AVX2__
	inline bool yuy2_to_yv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int pixelsToPad)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			3, 7, 11, 15, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14,
			3, 7, 11, 15, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + pixelsToPad;
		const int uvWidth = yWidth / 2;
		const int vectorWidth = width & ~15; // pixels converted 16 per pass
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + (y * uvWidth));
			for (int x = 0; x < vectorWidth; x += 16) // 16 bits per pixel in 256 bit chunks = 16 pixels per pass
			{
				__m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
				__m256i shuffled = _mm256_shuffle_epi8(pixels, shuffle_1);
				__m256i permuted = _mm256_permutevar8x32_epi32(shuffled, permute);
				const uint64_t* vals = reinterpret_cast<const uint64_t*>(&permuted);

				v_out[0] = vals[0]; // 8 bytes each of UV
				v_out++;
				u_out[0] = vals[1];
				u_out++;

				y_out[0] = vals[2]; // 16 bytes of Y
				y_out[1] = vals[3];
				y_out += 2;

				src += 32; // 32 bytes read
			}
			// scalar tail for widths that are not a multiple of 16
			if (vectorWidth < width)
			{
				uint8_t* yTail = yPlane + (y * yWidth) + vectorWidth;
				uint8_t* uTail = uPlane + (y * uvWidth) + vectorWidth / 2;
				uint8_t* vTail = vPlane + (y * uvWidth) + vectorWidth / 2;
				for (int x = vectorWidth; x < width; x += 2)
				{
					yTail[0] = src[0];
					uTail[0] = src[1];
					yTail[1] = src[2];
					vTail[0] = src[3];

					yTail += 2;
					uTail++;
					vTail++;
					src += 4;
				}
			}
		}
		return true;
	}
	#else
	inline bool yuy2_to_yv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int pixelsToPad)
	{
		for (int y = 0; y < height; ++y)
		{
			auto offset = (y * (width + pixelsToPad));
			uint8_t* yOut = yPlane + offset;
			uint8_t* uOut = uPlane + offset / 2;
			uint8_t* vOut = vPlane + offset / 2;

			for (int x = 0; x < width; x += 2) // 2 pixels per pass
			{
				yOut[0] = src[0];
				uOut[0] = src[1];
				yOut[1] = src[2];
				vOut[0] = src[3];

				yOut += 2;
				uOut++;
				vOut++;
				src += 4;
			}
		}
		return true;
	}
	#endif

	// fourcc 2VUY has the same byte layout as UYVY (U0 Y0 V0 Y1)
	inline bool yuv2_to_yv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int pixelsToPad)
	{
		return uyvy_to_yv16(src, yPlane, uPlane, vPlane, width, height, pixelsToPad);
	}

	#ifdef __AVX2__
	inline bool y210_to_p210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                         int pixelsToPad)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13,
			2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13
		);
		const __m256i permute = _mm256_setr_epi32(0, 1, 4, 5, 2, 3, 6, 7);
		const int effectiveWidth = width + pixelsToPad;

		for (int lineNo = 0; lineNo < height; ++lineNo)
		{
			const uint8_t* srcLine = src + static_cast<ptrdiff_t>(lineNo) * srcStride;
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * effectiveWidth * 2);

			for (int x = 0; x < width; x += 8) // 32 bits per pixel in 256 bit chunks = 8 pixels per pass
			{
				__m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));
				__m256i shuffled = _mm256_shuffle_epi8(pixels, shuffle_1);
				__m256i permuted = _mm256_permutevar8x32_epi32(shuffled, permute);
				// low lane = 8 UV samples, high lane = 8 Y samples, 16 bytes to each plane
				_mm256_storeu2_m128i(reinterpret_cast<__m128i*>(dstLineY), reinterpret_cast<__m128i*>(dstLineUV),
				                     permuted);
				dstLineUV += 8;
				dstLineY += 8;
				srcLine += 32; // 32 bytes read
			}
		}
		return true;
	}
	#else
	inline bool y210_to_p210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                         int pixelsToPad)
	{
		auto effectiveWidth = width + pixelsToPad;

		for (int y = 0; y < height; y++)
		{
			const uint16_t* srcLine = reinterpret_cast<const uint16_t*>(src + static_cast<ptrdiff_t>(y) * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + y * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + y * effectiveWidth * 2);

			for (int x = 0; x < width; x += 2) // 2 pixels per pass
			{
				dstLineY[0] = srcLine[0];
				dstLineY[1] = srcLine[2];
				dstLineUV[0] = srcLine[1];
				dstLineUV[1] = srcLine[3];

				dstLineY += 2;
				dstLineUV += 2;
				srcLine += 4;
			}
		}
		return true;
	}
	#endif

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
	#ifdef __AVX2__
	inline bool v210_to_p210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                         int pixelsToPad)
	{
		// round up so a width that is not a multiple of 12 still converts its
		// final partial group; the source row is always long enough to load a
		// full 32 byte group (rows are aligned to 48 pixels / 128 bytes) and a
		// mid-line overspill lands at the start of the next row which is
		// overwritten immediately afterwards, so only the last line needs the
		// clamped store below
		const int groupsPerLine = (width + 11) / 12;
		const int effectiveWidth = width + pixelsToPad;

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
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * effectiveWidth * 2);

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
				alignas(32) uint16_t tmpY[16] = {0};
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmpY), y);

				__m256i uv_s = _mm256_shuffle_epi8(_mm256_blend_epi32(s0_s2, s1, uv_blend_mask), uv_shuffle_mask);
				__m256i uv = _mm256_permutevar8x32_epi32(uv_s, lower_192_perm);
				alignas(32) uint16_t tmpUV[16] = {0};
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
	inline bool v210_to_p210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                         int pixelsToPad)
	{
		// Each group of 4 dwords (16 bytes) contains 6 pixels; round up so a
		// width that is not a multiple of 6 converts its final partial group
		// (source rows are padded to 48 pixels so the read is always in bounds)
		const int groupsPerLine = (width + 5) / 6;
		auto effectiveWidth = width + pixelsToPad;

		for (int y = 0; y < height; y++)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + static_cast<ptrdiff_t>(y) * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + y * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + y * effectiveWidth * 2);
			for (int g = 0; g < groupsPerLine; ++g)
			{
				const uint32_t* p = srcLine;

				// Unpack 12 16 bit samples to a 10 bit value manually
				uint16_t samples[12];

				// reorder each 32bit block from
				// V0 Y0 U0
				// Y2 U2 Y1
				// U4 Y3 V2
				// Y5 V4 Y4
				// to
				// [U0, Y0, V0, Y1, U2, Y2, V2, Y3, U4, Y4, V4, Y5]
				samples[0] = p[0] & 0x3FF; // U0
				samples[1] = (p[0] >> 10) & 0x3FF; // Y0
				samples[2] = (p[0] >> 20) & 0x3FF; // V0

				samples[3] = p[1] & 0x3FF; // Y1
				samples[4] = (p[1] >> 10) & 0x3FF; // U2
				samples[5] = (p[1] >> 20) & 0x3FF; // Y2

				samples[6] = p[2] & 0x3FF; // V2
				samples[7] = (p[2] >> 10) & 0x3FF; // Y3
				samples[8] = (p[2] >> 20) & 0x3FF; // U4

				samples[9] = p[3] & 0x3FF; // Y4
				samples[10] = (p[3] >> 10) & 0x3FF; // V4
				samples[11] = (p[3] >> 20) & 0x3FF; // Y5

				// shift to 16bit and store, clamped to the pixels the row has left
				const int n = std::min(6, width - g * 6);
				for (int i = 0; i < n; ++i)
				{
					dstLineY[i] = samples[i * 2 + 1] << 6; // Y0..Y5
					dstLineUV[i] = samples[i * 2] << 6; // U0 V0 U2 V2 U4 V4
				}

				// shift the pointer to the next group
				dstLineY += 6;
				dstLineUV += 6;
				srcLine += 4;
			}
		}
		return true;
	}
	#endif

	#ifdef __AVX2__
	inline bool r210_to_rgb48(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
	{
		__m128i pixelEndianSwap = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		size_t blocks = width / 4;
		const int dstPadding = pixelsToPad * 3;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < blocks; ++x)
			{
				// xmm is physically the lower lane of ymm so we can treat this as a ymm register going forward
				__m128i pixelBlockBE = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcPixelBE));
				// swap to produce 10bit BGR
				__m128i pixelBlockLE = _mm_shuffle_epi8(pixelBlockBE, pixelEndianSwap);
				const uint32_t* p = reinterpret_cast<const uint32_t*>(&pixelBlockLE);

				dstPix[0] = (p[0] & 0x3FF00000) >> 14;
				dstPix[1] = (p[0] & 0xFFC00) >> 4;
				dstPix[2] = (p[0] & 0x3FF) << 6;
				dstPix[3] = (p[1] & 0x3FF00000) >> 14;
				dstPix[4] = (p[1] & 0xFFC00) >> 4;
				dstPix[5] = (p[1] & 0x3FF) << 6;
				dstPix[6] = (p[2] & 0x3FF00000) >> 14;
				dstPix[7] = (p[2] & 0xFFC00) >> 4;
				dstPix[8] = (p[2] & 0x3FF) << 6;
				dstPix[9] = (p[3] & 0x3FF00000) >> 14;
				dstPix[10] = (p[3] & 0xFFC00) >> 4;
				dstPix[11] = (p[3] & 0x3FF) << 6;
				dstPix += 12;
				srcPixelBE += 4;
			}
			// scalar tail for widths that are not a multiple of 4
			for (size_t x = blocks * 4; x < width; ++x)
			{
				const uint32_t v = _byteswap_ulong(*srcPixelBE++);
				dstPix[0] = static_cast<uint16_t>((v & 0x3FF00000) >> 14);
				dstPix[1] = static_cast<uint16_t>((v & 0xFFC00) >> 4);
				dstPix[2] = static_cast<uint16_t>((v & 0x3FF) << 6);
				dstPix += 3;
			}
			dstPix += dstPadding;
			srcRow += srcStride;
		}
		return true;
	}
	#else
	inline bool r210_to_rgb48(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		const int dstPadding = pixelsToPad * 3;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				const auto srcPixel = _byteswap_ulong(srcPixelBE[x]);

				const uint16_t red_16 = (srcPixel & 0x3FF00000) >> 14;
				const uint16_t green_16 = (srcPixel & 0xFFC00) >> 4;
				const uint16_t blue_16 = (srcPixel & 0x3FF) << 6;

				dstPix[0] = red_16;
				dstPix[1] = green_16;
				dstPix[2] = blue_16;
				dstPix += 3;
			}
			srcRow += srcStride;
			dstPix += dstPadding;
		}
		return true;
	}
	#endif

	#ifdef __AVX2__
	inline bool bgr10_to_rgb48(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		size_t blocks = width / 4;
		const int dstPadding = pixelsToPad * 3;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixel = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < blocks; ++x)
			{
				// xmm is physically the lower lane of ymm so we can treat this as a ymm register going forward
				__m128i pixelBlockLE = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcPixel));
				const uint32_t* p = reinterpret_cast<const uint32_t*>(&pixelBlockLE);

				dstPix[0] = (p[0] & 0x3FF) << 6;
				dstPix[1] = (p[0] & 0xFFC00) >> 4;
				dstPix[2] = (p[0] & 0x3FF00000) >> 14;
				dstPix[3] = (p[1] & 0x3FF) << 6;
				dstPix[4] = (p[1] & 0xFFC00) >> 4;
				dstPix[5] = (p[1] & 0x3FF00000) >> 14;
				dstPix[6] = (p[2] & 0x3FF) << 6;
				dstPix[7] = (p[2] & 0xFFC00) >> 4;
				dstPix[8] = (p[2] & 0x3FF00000) >> 14;
				dstPix[9] = (p[3] & 0x3FF) << 6;
				dstPix[10] = (p[3] & 0xFFC00) >> 4;
				dstPix[11] = (p[3] & 0x3FF00000) >> 14;
				dstPix += 12;
				srcPixel += 4;
			}
			// scalar tail for widths that are not a multiple of 4
			for (size_t x = blocks * 4; x < width; ++x)
			{
				const uint32_t v = *srcPixel++;
				dstPix[0] = static_cast<uint16_t>((v & 0x3FF) << 6);
				dstPix[1] = static_cast<uint16_t>((v & 0xFFC00) >> 4);
				dstPix[2] = static_cast<uint16_t>((v & 0x3FF00000) >> 14);
				dstPix += 3;
			}
			dstPix += dstPadding;
			srcRow += srcStride;
		}
		return true;
	}
	#else
	inline bool bgr10_to_rgb48(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		const int dstPadding = pixelsToPad * 3;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelLE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				const auto srcPixel = srcPixelLE[x];

				const uint16_t red_16 = (srcPixel & 0x3FF) << 6;
				const uint16_t green_16 = (srcPixel & 0xFFC00) >> 4;
				const uint16_t blue_16 = (srcPixel & 0x3FF00000) >> 14;

				dstPix[0] = red_16;
				dstPix[1] = green_16;
				dstPix[2] = blue_16;
				dstPix += 3;
			}
			srcRow += srcStride;
			dstPix += dstPadding;
		}
		return true;
	}
	#endif
}
#endif
