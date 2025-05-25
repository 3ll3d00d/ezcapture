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
#include <cstdint>
#include <intrin.h>

 // same mechanism as bgr10 but with a byte swap to flip endianness and different component order
namespace r210
{
	#ifdef __AVX2__
	bool convert(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
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
			dstPix += dstPadding;
			srcRow += srcStride;
		}
		return true;
	}
	#else
	bool convert(const uint8_t* src, uint16_t* dst, size_t width, size_t height, int pixelsToPad)
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
}
