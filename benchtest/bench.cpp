#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <immintrin.h>
#include <format>
#include <filesystem>
#include <fstream>
#include <vector>

// Helper functions to calculate buffer sizes
constexpr size_t CalculateV210BufferSize(int width, int height)
{
	// V210 requires width to be aligned to 6 samples (6 Y values per 4 32-bit word block)
	int alignedWidth = (width + 5) / 6 * 6;

	// Each aligned row holds alignedWidth/6 * 4 32-bit words
	int bytesPerRow = ((alignedWidth * 8) / 3 + 15) & ~15; // 16-byte alignment

	return bytesPerRow * height;
}

struct strides
{
	int srcStride;
	int dstYStride;
	int dstUVStride;
};

constexpr int ALIGNMENT = 64;

inline int Align(int value, int align)
{
	return (value + align - 1) & ~(align - 1);
}

inline strides CalculateAlignedV210P210Strides(int inWidth, int outWidth)
{
	strides s;
	int rawSrcStride = ((inWidth + 5) / 6) * 16;
	int rawDstYStride = outWidth * 2;
	int rawDstUVStride = outWidth * 2;

	s.srcStride = Align(rawSrcStride, ALIGNMENT);
	s.dstYStride = Align(rawDstYStride, ALIGNMENT);
	s.dstUVStride = Align(rawDstUVStride, ALIGNMENT);

	return s;
}

namespace
{
	bool convert_avx_pack(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                      int padWidth,
	                      std::chrono::time_point<std::chrono::steady_clock>* t1,
	                      std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const int groupsPerLine = width / 12;

		// Pre-compute constants once outside the loops
		const __m256i mask10 = _mm256_set1_epi32(0x3FF);
		const __m256i zeroes = _mm256_setzero_si256();
		const __m256i s2_shuffleMask = _mm256_setr_epi8(
			-1, -1, 0, 1, 2, 3, -1, -1, 4, 5, 6, 7, -1, -1, -1, -1,
			-1, -1, 0, 1, 2, 3, -1, -1, 4, 5, 6, 7, -1, -1, -1, -1
		);
		const __m256i s1_shuffleMask = _mm256_setr_epi8(
			0, 1, -1, -1, 2, 3, 4, 5, -1, -1, 6, 7, -1, -1, -1, -1,
			0, 1, -1, -1, 2, 3, 4, 5, -1, -1, 6, 7, -1, -1, -1, -1
		);
		const __m256i s0_shuffleMask = _mm256_setr_epi8(
			0, 1, 2, 3, -1, -1, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1,
			0, 1, 2, 3, -1, -1, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1
		);
		const uint8_t y_blend_mask_1 = 0b00001001;
		const uint8_t y_blend_mask_2 = 0b00011011;
		const uint8_t uv_blend_mask_1 = 0b00110110;
		const uint8_t uv_blend_mask_2 = 0b00101101;

		*t1 = std::chrono::high_resolution_clock::now();
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

				// transpose to extract a 10-bit component into a 32bit slot spread across 3 registers
				//     s2  s1  s0
				//     ----------
				// 0:  V0  Y0  U0
				// 1:  xx  xx  xx
				// 2:  Y2  U2  Y1
				// 3:  xx  xx  xx
				// 4:  U4  Y3  V2
				// 5:  xx  xx  xx
				// 6:  Y5  V4  Y4
				// 7:  xx  xx  xx
				// 8:  V6  Y6  U6
				// 9:  xx  xx  xx
				// 10: Y8  U8  Y7
				// 11: xx  xx  xx
				// 12: U10 Y9  V8
				// 13: xx  xx  xx
				// 14: Y11 V10 Y10
				// 15: xx  xx  xx
				//
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0�9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

				// pack down to 16bit and fill the remainder with zeroes
				__m256i s0_16 = _mm256_packs_epi32(s0_32, zeroes);
				__m256i s1_16 = _mm256_packs_epi32(s1_32, zeroes);
				__m256i s2_16 = _mm256_packs_epi32(s2_32, zeroes);

				// shuffle to prepare for blending
				__m256i s0_16_shuffled = _mm256_shuffle_epi8(s0_16, s0_shuffleMask);
				__m256i s1_16_shuffled = _mm256_shuffle_epi8(s1_16, s1_shuffleMask);
				__m256i s2_16_shuffled = _mm256_shuffle_epi8(s2_16, s2_shuffleMask);

				// blend to y and uv
				__m256i y_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, y_blend_mask_1);
				__m256i uv_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, uv_blend_mask_1);
				__m256i y = _mm256_blend_epi16(s2_16_shuffled, y_tmp, y_blend_mask_2);
				__m256i uv = _mm256_blend_epi16(s2_16_shuffled, uv_tmp, uv_blend_mask_2);

				// scale
				__m256i y_scaled = _mm256_slli_epi16(y, 6);
				// write 96 bits from each lane
				__m128i y_lo = _mm256_extracti128_si256(y_scaled, 0);
				__m128i y_hi = _mm256_extracti128_si256(y_scaled, 1);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineY), y_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineY + 6), y_hi);

				// scale
				__m256i uv_scaled = _mm256_slli_epi16(uv, 6);
				// write 96 bits from each lane
				__m128i uv_lo = _mm256_extracti128_si256(uv_scaled, 0);
				__m128i uv_hi = _mm256_extracti128_si256(uv_scaled, 1);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineUV), uv_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineUV + 6), uv_hi);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}

			// Handle last group for the last line with potential overflow
			if (lineNo == height - 1 && g < groupsPerLine)
			{
				// Load 8 dwords
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				// transpose to extract a 10-bit component into a 32bit slot spread across 3 registers
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0�9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

				// pack down to 16bit and fill the remainder with zeroes
				__m256i s0_16 = _mm256_packs_epi32(s0_32, zeroes);
				__m256i s1_16 = _mm256_packs_epi32(s1_32, zeroes);
				__m256i s2_16 = _mm256_packs_epi32(s2_32, zeroes);

				// shuffle to prepare for blending
				__m256i s0_16_shuffled = _mm256_shuffle_epi8(s0_16, s0_shuffleMask);
				__m256i s1_16_shuffled = _mm256_shuffle_epi8(s1_16, s1_shuffleMask);
				__m256i s2_16_shuffled = _mm256_shuffle_epi8(s2_16, s2_shuffleMask);

				// blend to y and uv
				__m256i y_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, y_blend_mask_1);
				__m256i uv_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, uv_blend_mask_1);
				__m256i y = _mm256_blend_epi16(s2_16_shuffled, y_tmp, y_blend_mask_2);
				__m256i uv = _mm256_blend_epi16(s2_16_shuffled, uv_tmp, uv_blend_mask_2);

				// scale
				__m256i y_scaled = _mm256_slli_epi16(y, 6);
				// write 96 bits from each lane
				__m128i y_lo = _mm256_extracti128_si256(y_scaled, 0);
				__m128i y_hi = _mm256_extracti128_si256(y_scaled, 1);

				alignas(32) uint16_t tmpY[16] = {0};
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpY), y_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpY + 6), y_hi);

				// scale
				__m256i uv_scaled = _mm256_slli_epi16(uv, 6);
				// write 96 bits from each lane
				__m128i uv_lo = _mm256_extracti128_si256(uv_scaled, 0);
				__m128i uv_hi = _mm256_extracti128_si256(uv_scaled, 1);
				alignas(32) uint16_t tmpUV[16] = {0};
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpUV), uv_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpUV + 6), uv_hi);

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
		*t2 = std::chrono::high_resolution_clock::now();

		return true;
	}

	bool convert_avx_no_pack(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                         int padWidth,
	                         std::chrono::time_point<std::chrono::steady_clock>* t1,
	                         std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const int groupsPerLine = width / 12;

		// Pre-compute constants once outside the loops
		const __m256i mask10 = _mm256_set1_epi32(0x3FF);
		const __m256i zeroes = _mm256_setzero_si256();
		const __m256i s2_shuffleMask = _mm256_setr_epi8(
			-1, -1, 0, 1, 4, 5, -1, -1, 8, 9, 12, 13, -1, -1, -1, -1,
			-1, -1, 0, 1, 4, 5, -1, -1, 8, 9, 12, 13, -1, -1, -1, -1
		);
		const __m256i s1_shuffleMask = _mm256_setr_epi8(
			0, 1, -1, -1, 4, 5, 8, 9, -1, -1, 12, 13, -1, -1, -1, -1,
			0, 1, -1, -1, 4, 5, 8, 9, -1, -1, 12, 13, -1, -1, -1, -1
		);
		const __m256i s0_shuffleMask = _mm256_setr_epi8(
			0, 1, 4, 5, -1, -1, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1,
			0, 1, 4, 5, -1, -1, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1
		);
		const uint8_t y_blend_mask_1 = 0b00001001;
		const uint8_t y_blend_mask_2 = 0b00011011;
		const uint8_t uv_blend_mask_1 = 0b00110110;
		const uint8_t uv_blend_mask_2 = 0b00101101;

		*t1 = std::chrono::high_resolution_clock::now();
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

				// transpose to extract a 10-bit component into a 32bit slot spread across 3 registers
				//     s2  s1  s0
				//     ----------
				// 0:  V0  Y0  U0
				// 1:  xx  xx  xx
				// 2:  Y2  U2  Y1
				// 3:  xx  xx  xx
				// 4:  U4  Y3  V2
				// 5:  xx  xx  xx
				// 6:  Y5  V4  Y4
				// 7:  xx  xx  xx
				// 8:  V6  Y6  U6
				// 9:  xx  xx  xx
				// 10: Y8  U8  Y7
				// 11: xx  xx  xx
				// 12: U10 Y9  V8
				// 13: xx  xx  xx
				// 14: Y11 V10 Y10
				// 15: xx  xx  xx
				//
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0�9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

				// shuffle to prepare for blending
				__m256i s0_16_shuffled = _mm256_shuffle_epi8(s0_32, s0_shuffleMask);
				__m256i s1_16_shuffled = _mm256_shuffle_epi8(s1_32, s1_shuffleMask);
				__m256i s2_16_shuffled = _mm256_shuffle_epi8(s2_32, s2_shuffleMask);

				// blend to y and uv
				__m256i y_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, y_blend_mask_1);
				__m256i uv_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, uv_blend_mask_1);
				__m256i y = _mm256_blend_epi16(s2_16_shuffled, y_tmp, y_blend_mask_2);
				__m256i uv = _mm256_blend_epi16(s2_16_shuffled, uv_tmp, uv_blend_mask_2);

				// scale
				__m256i y_scaled = _mm256_slli_epi16(y, 6);
				// write 96 bits from each lane
				__m128i y_lo = _mm256_extracti128_si256(y_scaled, 0);
				__m128i y_hi = _mm256_extracti128_si256(y_scaled, 1);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineY), y_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineY + 6), y_hi);

				// scale
				__m256i uv_scaled = _mm256_slli_epi16(uv, 6);
				// write 96 bits from each lane
				__m128i uv_lo = _mm256_extracti128_si256(uv_scaled, 0);
				__m128i uv_hi = _mm256_extracti128_si256(uv_scaled, 1);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineUV), uv_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dstLineUV + 6), uv_hi);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}

			// Handle last group for the last line with potential overflow
			if (lineNo == height - 1 && g < groupsPerLine)
			{
				// Load 8 dwords
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				// transpose to extract a 10-bit component into a 32bit slot spread across 3 registers
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0�9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

				// pack down to 16bit and fill the remainder with zeroes
				__m256i s0_16 = _mm256_packs_epi32(s0_32, zeroes);
				__m256i s1_16 = _mm256_packs_epi32(s1_32, zeroes);
				__m256i s2_16 = _mm256_packs_epi32(s2_32, zeroes);

				// shuffle to prepare for blending
				__m256i s0_16_shuffled = _mm256_shuffle_epi8(s0_16, s0_shuffleMask);
				__m256i s1_16_shuffled = _mm256_shuffle_epi8(s1_16, s1_shuffleMask);
				__m256i s2_16_shuffled = _mm256_shuffle_epi8(s2_16, s2_shuffleMask);

				// blend to y and uv
				__m256i y_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, y_blend_mask_1);
				__m256i uv_tmp = _mm256_blend_epi16(s0_16_shuffled, s1_16_shuffled, uv_blend_mask_1);
				__m256i y = _mm256_blend_epi16(s2_16_shuffled, y_tmp, y_blend_mask_2);
				__m256i uv = _mm256_blend_epi16(s2_16_shuffled, uv_tmp, uv_blend_mask_2);

				// scale
				__m256i y_scaled = _mm256_slli_epi16(y, 6);
				// write 96 bits from each lane
				__m128i y_lo = _mm256_extracti128_si256(y_scaled, 0);
				__m128i y_hi = _mm256_extracti128_si256(y_scaled, 1);

				alignas(32) uint16_t tmpY[16] = {0};
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpY), y_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpY + 6), y_hi);

				// scale
				__m256i uv_scaled = _mm256_slli_epi16(uv, 6);
				// write 96 bits from each lane
				__m128i uv_lo = _mm256_extracti128_si256(uv_scaled, 0);
				__m128i uv_hi = _mm256_extracti128_si256(uv_scaled, 1);
				alignas(32) uint16_t tmpUV[16] = {0};
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpUV), uv_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(tmpUV + 6), uv_hi);

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
		*t2 = std::chrono::high_resolution_clock::now();

		return true;
	}

	bool convert_avx_so1(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                     int padWidth,
	                     std::chrono::time_point<std::chrono::steady_clock>* t1,
	                     std::chrono::time_point<std::chrono::steady_clock>* t2)
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

		*t1 = std::chrono::high_resolution_clock::now();
		auto effectiveWidth = width + padWidth;

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

		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_avx_so2(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                     int padWidth,
	                     std::chrono::time_point<std::chrono::steady_clock>* t1,
	                     std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const int groupsPerLine = width / 12;

		__m256i UV_mask = _mm256_set1_epi64x(0x000FFC003FF003FF);
		__m256i UV_shuf = _mm256_setr_epi8(
			0, 1, 2, 3, 5, 6, 8, 9, 10, 11, 13, 14, -1, -1, -1, -1,
			0, 1, 2, 3, 5, 6, 8, 9, 10, 11, 13, 14, -1, -1, -1, -1);
		__m256i UV_shift = _mm256_setr_epi16(
			64, 4, 16, 64, 4, 16, 0, 0,
			64, 4, 16, 64, 4, 16, 0, 0);
		__m256i skip3perm = _mm256_setr_epi32(0, 1, 2, 4, 5, 6, 7, 7);
		__m256i Y_mask = _mm256_set1_epi64x(0x3FF003FF000FFC00);
		__m256i Y_shuf = _mm256_setr_epi8(
			1, 2, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, -1, -1, -1, -1,
			1, 2, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, -1, -1, -1, -1);
		__m256i Y_shift = _mm256_setr_epi16(
			16, 64, 4, 16, 64, 4, 0, 0,
			16, 64, 4, 16, 64, 4, 0, 0);

		*t1 = std::chrono::high_resolution_clock::now();

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

				__m256i UVs = _mm256_and_si256(dwords, UV_mask);
				UVs = _mm256_shuffle_epi8(UVs, UV_shuf);
				UVs = _mm256_mullo_epi16(UVs, UV_shift);
				UVs = _mm256_permutevar8x32_epi32(UVs, skip3perm);

				__m256i Ys = _mm256_and_si256(dwords, Y_mask);
				Ys = _mm256_shuffle_epi8(Ys, Y_shuf);
				Ys = _mm256_mullo_epi16(Ys, Y_shift);
				Ys = _mm256_permutevar8x32_epi32(Ys, skip3perm);

				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineY), Ys);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineUV), UVs);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}

			// Handle last group for the last line with potential overflow
			if (lineNo == height - 1 && g < groupsPerLine)
			{
				// Load 8 dwords
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				__m256i UVs = _mm256_and_si256(dwords, UV_mask);
				UVs = _mm256_shuffle_epi8(UVs, UV_shuf);
				UVs = _mm256_mullo_epi16(UVs, UV_shift);
				UVs = _mm256_permutevar8x32_epi32(UVs, skip3perm);

				__m256i Ys = _mm256_and_si256(dwords, Y_mask);
				Ys = _mm256_shuffle_epi8(Ys, Y_shuf);
				Ys = _mm256_mullo_epi16(Ys, Y_shift);
				Ys = _mm256_permutevar8x32_epi32(Ys, skip3perm);

				alignas(32) uint16_t tmpY[16] = {0};
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmpY), Ys);

				alignas(32) uint16_t tmpUV[16] = {0};
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmpUV), UVs);

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

		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	// terrible implementation just to illustrate how much slower you can make it
	bool convert_avx_naive(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                       int padWidth,
	                       std::chrono::time_point<std::chrono::steady_clock>* t1,
	                       std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const int groupsPerLine = width / 12;

		// last line will overflow on the last group so handle separately to avoid branch in the for loop
		*t1 = std::chrono::high_resolution_clock::now();
		int lineNo = 0;
		for (; lineNo < height - 1; ++lineNo)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + lineNo * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * width * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * width * 2);

			for (int g = 0; g < groupsPerLine; ++g)
			{
				// Load 2 blocks into a 256 bit register which allows 12 pixels to be processed in each pass
				__m256i dwords = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine));

				// Split the packed data into 3 lanes, masking/shifting each 32bit value to produce a 10bit value
				// 12 pixels per pass which means we use all 256 bits as follows
				//
				//     s2  s1  s0
				//     ----------
				// 0:  V0  Y0  U0
				// 1:  xx  xx  xx
				// 2:  Y2  U2  Y1
				// 3:  xx  xx  xx
				// 4:  U4  Y3  V2
				// 5:  xx  xx  xx
				// 6:  Y5  V4  Y4
				// 7:  xx  xx  xx
				// 8:  V6  Y6  U6
				// 9:  xx  xx  xx
				// 10: Y8  U8  Y7
				// 11: xx  xx  xx
				// 12: U10 Y9  V8
				// 13: xx  xx  xx
				// 14: Y11 V10 Y10
				// 15: xx  xx  xx
				//
				__m256i mask10 = _mm256_set1_epi32(0x3FF);
				__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0�9
				__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
				__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

				// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
				__m256i yPlane = _mm256_set_epi16(
					0, 0, 0, 0, // padding
					_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
					_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
					_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8), // Y7,  Y6
					_mm256_extract_epi16(s2, 6), _mm256_extract_epi16(s0, 6), // Y5,  Y4
					_mm256_extract_epi16(s1, 4), _mm256_extract_epi16(s2, 2), // Y3,  Y2
					_mm256_extract_epi16(s0, 2), _mm256_extract_epi16(s1, 0) // Y1,  Y0
				);
				__m256i uvPlane = _mm256_set_epi16(
					0, 0, 0, 0, // padding
					_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
					_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
					_mm256_extract_epi16(s2, 8), _mm256_extract_epi16(s0, 8), // V6,  U6
					_mm256_extract_epi16(s1, 6), _mm256_extract_epi16(s2, 4), // V4,  U4
					_mm256_extract_epi16(s0, 4), _mm256_extract_epi16(s1, 2), // V2,  U2
					_mm256_extract_epi16(s2, 0), _mm256_extract_epi16(s0, 0) // V0,  U0
				);

				// left shift from 10 to 16 bit full scale
				__m256i ySamples = _mm256_slli_epi16(yPlane, 6);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineY), ySamples);

				__m256i uvSamples = _mm256_slli_epi16(uvPlane, 6);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineUV), uvSamples);

				dstLineY += 12;
				dstLineUV += 12;
				srcLine += 8;
			}
		}
		const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + lineNo * srcStride);
		uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * width * 2);
		uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * width * 2);

		int g = 0;
		for (; g < groupsPerLine - 1; ++g)
		{
			// Load 2 blocks into a 256 bit register which allows 12 pixels to be processed in each pass
			__m256i dwords = _mm256_set_epi32(
				srcLine[7], srcLine[6], srcLine[5], srcLine[4],
				srcLine[3], srcLine[2], srcLine[1], srcLine[0]
			);

			// Split the packed data into 3 lanes, masking/shifting each 32bit value to produce a 10bit value
			// 12 pixels per pass which means we use all 256 bits as follows
			//
			//     s2  s1  s0
			//     ----------
			// 0:  V0  Y0  U0
			// 1:  xx  xx  xx
			// 2:  Y2  U2  Y1
			// 3:  xx  xx  xx
			// 4:  U4  Y3  V2
			// 5:  xx  xx  xx
			// 6:  Y5  V4  Y4
			// 7:  xx  xx  xx
			// 8:  V6  Y6  U6
			// 9:  xx  xx  xx
			// 10: Y8  U8  Y7
			// 11: xx  xx  xx
			// 12: U10 Y9  V8
			// 13: xx  xx  xx
			// 14: Y11 V10 Y10
			// 15: xx  xx  xx
			//
			__m256i mask10 = _mm256_set1_epi32(0x3FF);
			__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0�9
			__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
			__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

			// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
			__m256i yPlane = _mm256_set_epi16(
				0, 0, 0, 0, // padding
				_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
				_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
				_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8), // Y7,  Y6
				_mm256_extract_epi16(s2, 6), _mm256_extract_epi16(s0, 6), // Y5,  Y4
				_mm256_extract_epi16(s1, 4), _mm256_extract_epi16(s2, 2), // Y3,  Y2
				_mm256_extract_epi16(s0, 2), _mm256_extract_epi16(s1, 0) // Y1,  Y0
			);
			__m256i uvPlane = _mm256_set_epi16(
				0, 0, 0, 0, // padding
				_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
				_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
				_mm256_extract_epi16(s2, 8), _mm256_extract_epi16(s0, 8), // V6,  U6
				_mm256_extract_epi16(s1, 6), _mm256_extract_epi16(s2, 4), // V4,  U4
				_mm256_extract_epi16(s0, 4), _mm256_extract_epi16(s1, 2), // V2,  U2
				_mm256_extract_epi16(s2, 0), _mm256_extract_epi16(s0, 0) // V0,  U0
			);

			// left shift from 10 to 16 bit full scale
			__m256i ySamples = _mm256_slli_epi16(yPlane, 6);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineY), ySamples);

			__m256i uvSamples = _mm256_slli_epi16(uvPlane, 6);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstLineUV), uvSamples);

			dstLineY += 12;
			dstLineUV += 12;
			srcLine += 8;
		}

		// very last group can overflow so use a tmp array

		// Load 2 blocks into a 256 bit register which allows 12 pixels to be processed in each pass
		__m256i dwords = _mm256_set_epi32(
			srcLine[7], srcLine[6], srcLine[5], srcLine[4],
			srcLine[3], srcLine[2], srcLine[1], srcLine[0]
		);

		// Split the packed data into 3 lanes, masking/shifting each 32bit value to produce a 10bit value
		// 12 pixels per pass which means we use all 256 bits as follows
		//
		//     s2  s1  s0
		//     ----------
		// 0:  V0  Y0  U0
		// 1:  xx  xx  xx
		// 2:  Y2  U2  Y1
		// 3:  xx  xx  xx
		// 4:  U4  Y3  V2
		// 5:  xx  xx  xx
		// 6:  Y5  V4  Y4
		// 7:  xx  xx  xx
		// 8:  V6  Y6  U6
		// 9:  xx  xx  xx
		// 10: Y8  U8  Y7
		// 11: xx  xx  xx
		// 12: U10 Y9  V8
		// 13: xx  xx  xx
		// 14: Y11 V10 Y10
		// 15: xx  xx  xx
		//
		__m256i mask10 = _mm256_set1_epi32(0x3FF);
		__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0�9
		__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10�19
		__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20�29

		// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
		__m256i yPlane = _mm256_set_epi16(
			0, 0, 0, 0, // padding
			_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
			_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
			_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8), // Y7,  Y6
			_mm256_extract_epi16(s2, 6), _mm256_extract_epi16(s0, 6), // Y5,  Y4
			_mm256_extract_epi16(s1, 4), _mm256_extract_epi16(s2, 2), // Y3,  Y2
			_mm256_extract_epi16(s0, 2), _mm256_extract_epi16(s1, 0) // Y1,  Y0
		);
		__m256i uvPlane = _mm256_set_epi16(
			0, 0, 0, 0, // padding
			_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
			_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
			_mm256_extract_epi16(s2, 8), _mm256_extract_epi16(s0, 8), // V6,  U6
			_mm256_extract_epi16(s1, 6), _mm256_extract_epi16(s2, 4), // V4,  U4
			_mm256_extract_epi16(s0, 4), _mm256_extract_epi16(s1, 2), // V2,  U2
			_mm256_extract_epi16(s2, 0), _mm256_extract_epi16(s0, 0) // V0,  U0
		);

		alignas(16) uint16_t tmp[16];

		// left shift from 10 to 16 bit full scale
		__m256i ySamples = _mm256_slli_epi16(yPlane, 6);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp), ySamples);
		std::memcpy(dstLineY, tmp, 24);

		__m256i uvSamples = _mm256_slli_epi16(uvPlane, 6);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp), uvSamples);
		std::memcpy(dstLineUV, tmp, 24);

		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_scalar(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                    int padWidth,
	                    std::chrono::time_point<std::chrono::steady_clock>* t1,
	                    std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each group of 16 bytes contains 6 pixels (YUVYUV)
		const int groupsPerLine = width / 6;
		auto effectiveWidth = width + padWidth;

		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; y++)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + y * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + y * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + y * effectiveWidth * 2);
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
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_scalar_rgb(const uint8_t* src, uint16_t* dst, size_t width, size_t height,
	                        int padWidth,
	                        std::chrono::time_point<std::chrono::steady_clock>* t1,
	                        std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		*t1 = std::chrono::high_resolution_clock::now();
		const int dstPadding = padWidth * 3;
		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				// r210 is simply BGR once swapped to little endian
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

		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_scalar_avx_load_rgb(const uint8_t* src, uint16_t* dst, size_t width, size_t height,
	                                 int padWidth,
	                                 std::chrono::time_point<std::chrono::steady_clock>* t1,
	                                 std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		__m128i pixelEndianSwap = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;
		size_t blocks = width / 4;
		*t1 = std::chrono::high_resolution_clock::now();
		const int dstPadding = padWidth * 3;
		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < blocks; ++x)
			{
				// NB: 256bit avx2 load seems marginally slower than 128bit
				// xmm is physically the lower lane of ymm so we can treat this as a ymm register going forward
				__m128i pixelBlockBE = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcPixelBE));
				// swap to produce 10bit BGR
				__m128i pixelBlockLE = _mm_shuffle_epi8(pixelBlockBE, pixelEndianSwap);
				const uint32_t* p = reinterpret_cast<const uint32_t*>(&pixelBlockLE);

				dstPix[0] = (p[0] & 0x3FF00000) >> 14; // red
				dstPix[1] = (p[0] & 0xFFC00) >> 4; // green
				dstPix[2] = (p[0] & 0x3FF) << 6; // blue
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
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_avx2_rgb(const uint8_t* src, uint16_t* dst, size_t width, size_t height,
	                      int padWidth,
	                      std::chrono::time_point<std::chrono::steady_clock>* t1,
	                      std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each row starts on 256-byte boundary 
		size_t srcStride = (width * 4 + 255) / 256 * 256;

		__m128i pixelEndianSwap = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
		const __m256i mask_R_B = _mm256_set1_epi32(0x3FF003FF);
		const __m256i shift_R_B = _mm256_set1_epi32(0x00040040);
		const __m256i mask_G = _mm256_set1_epi32(0x000FFC00);
		const int lane_cross = 0b11011000; // shift upper 64bit of lower lane into lower 64bit of upper lane
		const __m256i split_red_blue = _mm256_setr_epi8(
			2, 3, -1, -1, 0, 1, 6, 7, -1, -1, 4, 5, -1, -1, -1, -1,
			2, 3, -1, -1, 0, 1, 6, 7, -1, -1, 4, 5, -1, -1, -1, -1
		);
		const __m256i shift_green = _mm256_setr_epi8(
			-1, -1, 0, 1, -1, -1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1,
			-1, -1, 0, 1, -1, -1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1
		);
		const int blend_rgb = 0b11010010;
		const int dstPadding = padWidth * 6;

		// process in 4 pixel (128 bit) blocks which produces 192 bits of output
		const int blocks = width / 4;
		*t1 = std::chrono::high_resolution_clock::now();
		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(src + y * srcStride);

			for (int x = 0; x < blocks; ++x)
			{
				// xmm is physically the lower lane of ymm so we can treat this as a ymm register going forward
				__m128i pixelBlockBE = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcRow));
				// swap to produce 10bit BGR
				__m256i pixelBlockLE = _mm256_castsi128_si256(_mm_shuffle_epi8(pixelBlockBE, pixelEndianSwap));
				// extract & align 10-bit components across 2 vectors shifted to 16bit values
				__m256i r_b = _mm256_mullo_epi16(_mm256_and_si256(pixelBlockLE, mask_R_B), shift_R_B);
				// R - B interleaved 16bit values
				__m256i g = _mm256_srli_epi32(_mm256_and_si256(pixelBlockLE, mask_G), 4); // G 
				// blend & shuffle & permute to align samples in lower 96bits of each lane
				__m256i r_b_split = _mm256_permute4x64_epi64(r_b, lane_cross);
				__m256i g_split = _mm256_permute4x64_epi64(g, lane_cross);
				__m256i r_b_align = _mm256_shuffle_epi8(r_b_split, split_red_blue);
				__m256i g_align = _mm256_shuffle_epi8(g_split, shift_green);
				__m256i rgb = _mm256_blend_epi16(r_b_align, g_align, blend_rgb);

				// write 96 bits from each lane
				__m128i rgb_lo = _mm256_extracti128_si256(rgb, 0);
				__m128i rgb_hi = _mm256_extracti128_si256(rgb, 1);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), rgb_lo);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 6), rgb_hi);
				srcRow += 4;
				dst += 12;
			}
			dst += dstPadding;
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_avx2_shift_rgb(const uint8_t* src, uint16_t* dst, size_t width, size_t height,
	                            int padWidth,
	                            std::chrono::time_point<std::chrono::steady_clock>* t1,
	                            std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each row starts on 256-byte boundary 
		size_t srcStride = (width * 4 + 255) / 256 * 256;

		__m256i pixelEndianSwap = _mm256_set_epi8(
			12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3,
			12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
		);
		const __m256i mask_R_B = _mm256_set1_epi32(0x3FF003FF);
		const __m256i shift_R_B = _mm256_set1_epi32(0x00040040);
		const __m256i mask_G = _mm256_set1_epi32(0x000FFC00);

		const int blocks = width / 8;
		const int dstPadding = padWidth * 6;
		*t1 = std::chrono::high_resolution_clock::now();
		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(src + y * srcStride);

			for (int x = 0; x < blocks; ++x)
			{
				__m256i pixelBlockBE = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcRow));
				// swap to produce 10bit BGR
				__m256i pixelBlockLE = _mm256_shuffle_epi8(pixelBlockBE, pixelEndianSwap);
				// extract & align 10-bit components across 2 vectors shifted to 16bit values
				__m256i r_b_vector = _mm256_mullo_epi16(_mm256_and_si256(pixelBlockLE, mask_R_B), shift_R_B);
				__m256i g_vector = _mm256_srli_epi32(_mm256_and_si256(pixelBlockLE, mask_G), 4);
				const uint16_t* r_b = reinterpret_cast<const uint16_t*>(&r_b_vector);
				const uint16_t* g = reinterpret_cast<const uint16_t*>(&g_vector);
				// scalar write to the output
				for (int z = 0; z < 16; z += 2)
				{
					dst[0] = r_b[z + 1]; // red
					dst[1] = g[z];
					dst[2] = r_b[z]; // blue
					dst += 3;
				}
				srcRow += 8;
			}
			dst += dstPadding;
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_yuv2_scalar(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int padWidth,
	                         std::chrono::time_point<std::chrono::steady_clock>* t1,
	                         std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			auto offset = y * (width + padWidth);
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
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_yuy2_scalar(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int padWidth,
	                         std::chrono::time_point<std::chrono::steady_clock>* t1,
	                         std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			auto offset = y * (width + padWidth);
			uint8_t* yOut = yPlane + offset;
			uint8_t* uOut = uPlane + offset / 2;
			uint8_t* vOut = vPlane + offset / 2;

			for (int x = 0; x < width; x += 2) // 2 pixels per pass
			{
				vOut[0] = src[0];
				yOut[0] = src[1];
				uOut[0] = src[2];
				yOut[1] = src[3];

				yOut += 2;
				uOut++;
				vOut++;
				src += 4;
			}
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_uyvy_scalar(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                         int height, int padWidth,
	                         std::chrono::time_point<std::chrono::steady_clock>* t1,
	                         std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			auto offset = y * (width + padWidth);
			uint8_t* yOut = yPlane + offset;
			uint8_t* uOut = uPlane + offset / 2;
			uint8_t* vOut = vPlane + offset / 2;

			for (int x = 0; x < width; x += 2) // 2 pixels per pass
			{
				yOut[1] = src[0];
				vOut[0] = src[1];
				yOut[0] = src[2];
				uOut[0] = src[3];

				yOut += 2;
				uOut++;
				vOut++;
				src += 4;
			}
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_yuv2_avx(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                      int height, int padWidth, std::chrono::time_point<std::chrono::steady_clock>* t1,
	                      std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15,
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + padWidth;
		const int uvWidth = yWidth / 2;
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + (y * uvWidth));
			for (int x = 0; x < width; x += 16) // 16 bits per pixel in 256 bit chunks = 16 pixels per pass
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
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_yuy2_avx(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                      int height, int padWidth, std::chrono::time_point<std::chrono::steady_clock>* t1,
	                      std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			3, 7, 11, 15, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14,
			3, 7, 11, 15, 1, 5, 9, 13, 0, 2, 4, 6, 8, 10, 12, 14
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + padWidth;
		const int uvWidth = yWidth / 2;
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + (y * uvWidth));
			for (int x = 0; x < width; x += 16) // 16 bits per pixel in 256 bit chunks = 16 pixels per pass
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
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}

	bool convert_uyvy_avx(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width,
	                      int height, int padWidth, std::chrono::time_point<std::chrono::steady_clock>* t1,
	                      std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15,
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + padWidth;
		const int uvWidth = yWidth / 2;
		*t1 = std::chrono::high_resolution_clock::now();
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + (y * uvWidth));
			for (int x = 0; x < width; x += 16) // 16 bits per pixel in 256 bit chunks = 16 pixels per pass
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
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}
}

enum bench_fmt:uint8_t
{
	v210,
	r210,
	yuv2,
	yuy2,
	uyvy
};

const char* to_string(bench_fmt e)
{
	switch (e)
	{
	case v210: return "v210";
	case r210: return "r210";
	case yuv2: return "yuv2";
	case yuy2: return "yuy2";
	case uyvy: return "uyvy";
	default: return "unknown";
	}
}

enum bench_mode:uint8_t
{
	scalar,
	avx,
	v210_avx_pack,
	v210_avx_no_pack,
	v210_avx_so2,
	v210_avx_naive,
	r210_avx_load_only,
	r210_avx_shift
};

const char* to_string(bench_mode e)
{
	switch (e)
	{
	case v210_avx_pack: return "v210_avx_pack";
	case v210_avx_no_pack: return "v210_avx_no_pack";
	case avx: return "avx";
	case v210_avx_so2: return "v210_avx_so2";
	case v210_avx_naive: return "v210_avx_naive";
	case r210_avx_load_only: return "r210_avx_load";
	case r210_avx_shift: return "r210_shift";
	case scalar: return "scalar";
	default: return "unknown";
	}
}

class Benchmark
{
public:
	static bool bench(const std::filesystem::path& inputFile,
	                  const std::filesystem::path& outputFile_y,
	                  const std::filesystem::path& outputFile_uv,
	                  const std::filesystem::path& outputFile_u,
	                  const std::filesystem::path& outputFile_v,
	                  const std::filesystem::path& outputFile_rgb,
	                  const std::filesystem::path& outputFile_stats,
	                  int width, int height, int padWidth, bench_mode mode, bench_fmt bfmt)
	{
		if (width <= 0 || height <= 0)
		{
			return false;
		}
		auto frame = 0;
		uint64_t total = 0;

		try
		{
			using std::chrono::high_resolution_clock;
			using std::chrono::duration_cast;
			using std::chrono::microseconds;
			std::ofstream stats(outputFile_stats);
			stats << "mode,frame,micros\n";
			// Read input file using standard file streams
			std::ifstream inFile(inputFile, std::ios::binary);
			if (!inFile)
			{
				throw std::runtime_error(std::format("Failed to open input file: {}", inputFile.string()));
			}
			if (bfmt == v210)
			{
				strides strides = CalculateAlignedV210P210Strides(width, width + padWidth);
				size_t v210Size = CalculateV210BufferSize(width, height);

				std::vector<uint8_t> v210Buffer(v210Size);
				auto planeSize = strides.dstYStride * height * 2;
				std::vector<uint8_t> p210Buffer(planeSize);
				auto p210buffer_y = std::span(p210Buffer).subspan(0, planeSize / 2);
				uint8_t* p210Y = p210buffer_y.data();
				auto p210buffer_uv = std::span(p210Buffer).subspan(planeSize / 2, planeSize / 2);
				uint8_t* p210UV = p210buffer_uv.data();

				while (!inFile.eof())
				{
					inFile.read(reinterpret_cast<char*>(v210Buffer.data()), v210Size);
					std::streamsize s = inFile.gcount();
					if (s == 0)
					{
						break;
					}
					if (s != v210Size)
					{
						throw std::runtime_error("Failed to read V210 data");
					}
					std::chrono::time_point<std::chrono::steady_clock> t1;
					std::chrono::time_point<std::chrono::steady_clock> t2;
					switch (mode)
					{
					case v210_avx_no_pack:
						convert_avx_no_pack(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height,
						                    padWidth, &t1, &t2);
						break;
					case v210_avx_pack:
						convert_avx_pack(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, padWidth,
						                 &t1, &t2);
						break;
					case avx:
						convert_avx_so1(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, padWidth,
						                &t1, &t2);
						break;
					case v210_avx_so2:
						convert_avx_so2(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, padWidth,
						                &t1, &t2);
						break;
					case v210_avx_naive:
						convert_avx_naive(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, padWidth,
						                  &t1, &t2);
						break;
					case scalar:
						convert_scalar(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, padWidth,
						               &t1, &t2);
						break;
					}
					auto mics = duration_cast<microseconds>(t2 - t1);
					if (frame > 50) total += mics.count();
					stats << mode << "," << frame++ << "," << mics.count() << "\n";

					// Write output files
					std::ofstream outFile_y(outputFile_y, std::ios::binary);
					if (!outFile_y)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_y.string()));
					}
					std::ofstream outFile_uv(outputFile_uv, std::ios::binary);
					if (!outFile_uv)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_uv.string()));
					}

					outFile_y.write(reinterpret_cast<const char*>(p210buffer_y.data()), p210buffer_y.size());
					outFile_uv.write(reinterpret_cast<const char*>(p210buffer_uv.data()), p210buffer_uv.size());
				}
			}
			else if (bfmt == r210)
			{
				size_t r210Size = (width + 63) / 64 * 256 * height;

				std::vector<uint8_t> r210Buffer(r210Size);
				std::vector<uint16_t> rgbBuffer((width + padWidth) * height * 6);

				while (!inFile.eof())
				{
					inFile.read(reinterpret_cast<char*>(r210Buffer.data()), r210Size);
					std::streamsize s = inFile.gcount();
					if (s == 0)
					{
						break;
					}
					if (s != r210Size)
					{
						throw std::runtime_error("Failed to read R210 data");
					}
					std::chrono::time_point<std::chrono::steady_clock> t1;
					std::chrono::time_point<std::chrono::steady_clock> t2;
					switch (mode)
					{
					case scalar:
						convert_scalar_rgb(r210Buffer.data(), rgbBuffer.data(), width, height, padWidth, &t1, &t2);
						break;
					case avx:
						convert_avx2_rgb(r210Buffer.data(), rgbBuffer.data(), width, height, padWidth, &t1, &t2);
						break;
					case r210_avx_load_only:
						convert_scalar_avx_load_rgb(r210Buffer.data(), rgbBuffer.data(), width, height, padWidth, &t1,
						                            &t2);
						break;
					case r210_avx_shift:
						convert_avx2_shift_rgb(r210Buffer.data(), rgbBuffer.data(), width, height, padWidth, &t1, &t2);
						break;
					}
					auto mics = duration_cast<microseconds>(t2 - t1);
					if (frame > 50) total += mics.count();
					stats << mode << "," << frame++ << "," << mics.count() << "\n";

					// Write output files
					std::ofstream outFile_rgb(outputFile_rgb, std::ios::binary);
					if (!outFile_rgb)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_y.string()));
					}

					outFile_rgb.write(reinterpret_cast<const char*>(rgbBuffer.data()), rgbBuffer.size());
				}
			}
			else if (bfmt == yuv2 || bfmt == yuy2 || bfmt == uyvy)
			{
				auto ySize = (width + padWidth) * height;
				auto uvSize = ySize / 2;

				std::vector<uint8_t> yuv2Buffer(ySize * 2);
				yuv2Buffer.reserve(ySize * 2);
				std::vector<uint8_t> yv16Buffer(ySize * 2);
				auto yv16buffer_y = std::span(yv16Buffer).subspan(0, ySize);
				uint8_t* yv16_y = yv16buffer_y.data();
				auto yv16buffer_u = std::span(yv16Buffer).subspan(ySize, uvSize);
				uint8_t* yv16_u = yv16buffer_u.data();
				auto yv16buffer_v = std::span(yv16Buffer).subspan(ySize + uvSize, uvSize);
				uint8_t* yv16_v = yv16buffer_v.data();

				while (!inFile.eof())
				{
					if (frame == -1)
					{
						for (int i = 0; i < ySize * 2; ++i)
						{
							yuv2Buffer[i] = i % 255;
						}
					}
					else
					{
						inFile.read(reinterpret_cast<char*>(yuv2Buffer.data()), ySize * 2);
						std::streamsize s = inFile.gcount();
						if (s == 0)
						{
							break;
						}
						if (s < ySize * 2)
						{
							break;
						}
					}
					std::chrono::time_point<std::chrono::steady_clock> t1;
					std::chrono::time_point<std::chrono::steady_clock> t2;
					switch (mode)
					{
					case scalar:
						if (bfmt == yuv2)
							convert_yuv2_scalar(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						else if (bfmt == yuy2)
							convert_yuy2_scalar(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						else if (bfmt == uyvy)
							convert_uyvy_scalar(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						break;
					case avx:
						if (bfmt == yuv2)
							convert_yuv2_avx(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						else if (bfmt == yuy2)
							convert_yuy2_avx(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						else if (bfmt == uyvy)
							convert_uyvy_avx(yuv2Buffer.data(), yv16_y, yv16_u, yv16_v, width, height, padWidth, &t1, &t2);
						break;
					}
					auto mics = duration_cast<microseconds>(t2 - t1);
					if (frame > 50) total += mics.count();
					stats << mode << "," << frame++ << "," << mics.count() << "\n";

					// Write output files
					std::ofstream outFile_y(outputFile_y, std::ios::binary);
					if (!outFile_y)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_y.string()));
					}
					std::ofstream outFile_u(outputFile_u, std::ios::binary);
					if (!outFile_u)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_u.string()));
					}
					std::ofstream outFile_v(outputFile_v, std::ios::binary);
					if (!outFile_v)
					{
						throw std::runtime_error(std::format("Failed to open output file: {}", outputFile_v.string()));
					}

					outFile_y.write(reinterpret_cast<const char*>(yv16buffer_y.data()), yv16buffer_y.size());
					outFile_u.write(reinterpret_cast<const char*>(yv16buffer_u.data()), yv16buffer_u.size());
					outFile_v.write(reinterpret_cast<const char*>(yv16buffer_v.data()), yv16buffer_v.size());
				}
			}
		}
		catch (const std::exception& e)
		{
			// Print error message
			fprintf(stderr, "Error: %s\n", e.what());
			return false;
		}
		fprintf(stdout, "Mean: %.3f\n", static_cast<double>(total) / (frame - 50));
		return true;
	}
};

int main(int argc, char* argv[])
{
	auto bench_fmt = r210;
	auto bench_mode = scalar;
	std::size_t pos;
	bench_fmt = static_cast<::bench_fmt>(std::stoi(argv[1], &pos));
	auto i = std::stoi(argv[2], &pos);
	if (i > 1)
	{
		if (bench_fmt == r210) i += 2;
		if (bench_fmt == yuv2 || bench_fmt == yuy2 || bench_fmt == uyvy) i += 2 + 4;
	}
	bench_mode = static_cast<::bench_mode>(i);
	auto width = std::stoi(argv[3], &pos);
	auto height = std::stoi(argv[4], &pos);
	auto padWidth = 0;
	if (argc > 5)
	{
		padWidth = std::stoi(argv[5], &pos);
	}
	// auto inp = "demo.dat";
	auto inp = "bench." + std::string(to_string(bench_fmt));
	auto suffix = std::string(to_string(bench_fmt)) + "." + std::string(to_string(bench_mode));
	auto out_y = "y-" + suffix;
	auto out_rgb = "rgb-" + suffix;
	auto out_uv = "uv-" + suffix;
	auto out_u = "u-" + suffix;
	auto out_v = "v-" + suffix;
	auto cwd = std::filesystem::current_path();
	const auto inputFile = std::filesystem::path(inp);
	const auto outputFile_y = std::filesystem::path(out_y);
	const auto outputFile_uv = std::filesystem::path(out_uv);
	const auto outputFile_u = std::filesystem::path(out_u);
	const auto outputFile_v = std::filesystem::path(out_v);
	const auto outputFile_rgb = std::filesystem::path(out_rgb);
	const auto statsFile = std::filesystem::path("stats_" + suffix + ".csv");

	if (width <= 0 || height <= 0)
	{
		fprintf(stderr, "Invalid dimensions: width=%d, height=%d\n", width, height);
		return 1;
	}

	printf("Converting %s using %s\n", inputFile.string().c_str(), suffix.c_str());

	if (Benchmark::bench(inputFile, outputFile_y, outputFile_uv, outputFile_u, outputFile_v,
	                     outputFile_rgb, statsFile, width, height, padWidth, bench_mode, bench_fmt))
	{
		printf("Successfully converted %s\n", inputFile.string().c_str());
		return 0;
	}
	fprintf(stderr, "Conversion failed\n");
	return 1;
}
