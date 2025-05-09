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

inline strides CalculateAlignedV210P210Strides(int width)
{
	strides s;
	int rawSrcStride = ((width + 5) / 6) * 16;
	int rawDstYStride = width * 2;
	int rawDstUVStride = width * 2;

	s.srcStride = Align(rawSrcStride, ALIGNMENT);
	s.dstYStride = Align(rawDstYStride, ALIGNMENT);
	s.dstUVStride = Align(rawDstUVStride, ALIGNMENT);

	return s;
}

namespace
{
	bool convert_avx_pack(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
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
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0–9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0–9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0–9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
				__m256i s0_32 = _mm256_and_si256(dwords, mask10); // bits 0–9
				__m256i s1_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
				__m256i s2_32 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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

	bool convert_avx_naive(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
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
				__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0–9
				__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
				__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
			__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0–9
			__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
			__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
		__m256i s0 = _mm256_and_si256(dwords, mask10); // bits 0–9
		__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10); // bits 10–19
		__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10); // bits 20–29

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
	                    std::chrono::time_point<std::chrono::steady_clock>* t1,
	                    std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each group of 16 bytes contains 6 pixels (YUVYUV)
		const int groupsPerLine = width / 6;
		uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY);
		uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV);

		*t1 = std::chrono::high_resolution_clock::now();
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
	                        std::chrono::time_point<std::chrono::steady_clock>* t1,
	                        std::chrono::time_point<std::chrono::steady_clock>* t2)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;

		*t1 = std::chrono::high_resolution_clock::now();
		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				// r210 is simply BGR once swapped to little endian
				const auto r210Pixel = srcPixelBE[x];
				const auto srcPixel = _byteswap_ulong(r210Pixel);

				const auto red_10 = srcPixel & 0x3FF00000;
				const uint16_t red_16 = red_10 >> 14;
				*dstPix = red_16;
				dstPix++;

				const auto green_10 = srcPixel & 0xFFC00;
				const uint16_t green_16 = green_10 >> 4;
				*dstPix = green_16;
				dstPix++;

				const auto blue_10 = srcPixel & 0x3FF;
				const uint16_t blue_16 = blue_10 << 6;
				*dstPix = blue_16;
				dstPix++;
			}

			srcRow += srcStride;
		}
		*t2 = std::chrono::high_resolution_clock::now();
		return true;
	}
}

enum bench_fmt:uint8_t
{
	v210,
	r210,
	yuv2
};

const char* to_string(bench_fmt e)
{
	switch (e)
	{
	case v210: return "v210";
	case r210: return "r210";
	case yuv2: return "yuv2";
	default: return "unknown";
	}
}

enum bench_mode:uint8_t
{
	scalar,
	v210_avx_pack,
	v210_avx_no_pack,
	v210_avx_so1,
	v210_avx_so2,
	v210_avx_naive,
	r210_avx
};

const char* to_string(bench_mode e)
{
	switch (e)
	{
	case v210_avx_pack: return "v210_avx_pack";
	case v210_avx_no_pack: return "v210_avx_no_pack";
	case v210_avx_so1: return "v210_avx_so1";
	case v210_avx_so2: return "v210_avx_so2";
	case v210_avx_naive: return "v210_avx_naive";
	case r210_avx: return "r210_avx";
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
	                  const std::filesystem::path& outputFile_rgb,
	                  const std::filesystem::path& outputFile_stats,
	                  int width, int height, bench_mode mode, bench_fmt bfmt)
	{
		if (width <= 0 || height <= 0)
		{
			return false;
		}

		try
		{
			using std::chrono::high_resolution_clock;
			using std::chrono::duration_cast;
			using std::chrono::microseconds;
			std::ofstream stats(outputFile_stats);
			stats << "mode,frame,micros\n";
			auto frame = 0;
			uint64_t total = 0;
			// Read input file using standard file streams
			std::ifstream inFile(inputFile, std::ios::binary);
			if (!inFile)
			{
				throw std::runtime_error(std::format("Failed to open input file: {}", inputFile.string()));
			}
			if (bfmt == v210)
			{
				strides strides = CalculateAlignedV210P210Strides(width);
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
						convert_avx_no_pack(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1,
						                    &t2);
						break;
					case v210_avx_pack:
						convert_avx_pack(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1, &t2);
						break;
					case v210_avx_so1:
						convert_avx_so1(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1, &t2);
						break;
					case v210_avx_so2:
						convert_avx_so2(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1, &t2);
						break;
					case v210_avx_naive:
						convert_avx_naive(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1, &t2);
						break;
					case scalar:
						convert_scalar(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height, &t1, &t2);
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
				std::vector<uint16_t> rgbBuffer(width * height * 6);

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
						convert_scalar_rgb(r210Buffer.data(), rgbBuffer.data(), width, height, &t1, &t2);
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
				fprintf(stdout, "Mean: %.3f\n", static_cast<double>(total) / (frame - 50));
				return true;
			}
		}
		catch (const std::exception& e)
		{
			// Print error message
			fprintf(stderr, "Error: %s\n", e.what());
			return false;
		}
	}
};

int main(int argc, char* argv[])
{
	auto bench_fmt = r210;
	auto bench_mode = scalar;
	std::size_t pos;
	bench_fmt = static_cast<::bench_fmt>(std::stoi(argv[1], &pos));
	auto i = std::stoi(argv[2], &pos);
	if (bench_fmt == r210 && i != 0)
	{
		i += 5;
	}
	bench_mode = static_cast<::bench_mode>(i);
	auto width = std::stoi(argv[3], &pos);
	auto height = std::stoi(argv[4], &pos);
	// auto inp = "demo.dat";
	auto inp = "bench." + std::string(to_string(bench_fmt));
	auto suffix = "-" + std::string(to_string(bench_fmt)) + "." + std::string(to_string(bench_mode));
	auto out_y = "y" + suffix;
	auto out_rgb = "rgb" + suffix;
	auto out_uv = "uv" + suffix;
	auto cwd = std::filesystem::current_path();
	const auto inputFile = std::filesystem::path(inp);
	const auto outputFile_y = std::filesystem::path(out_y);
	const auto outputFile_uv = std::filesystem::path(out_uv);
	const auto outputFile_rgb = std::filesystem::path(out_rgb);
	const auto statsFile = std::filesystem::path("stats_" + suffix + ".csv");

	if (width <= 0 || height <= 0)
	{
		fprintf(stderr, "Invalid dimensions: width=%d, height=%d\n", width, height);
		return 1;
	}

	printf("Converting %s using %s\n", inputFile.string().c_str(), suffix.c_str());

	if (Benchmark::bench(inputFile, outputFile_y, outputFile_uv, outputFile_rgb, statsFile, width, height,
	                     bench_mode, bench_fmt))
	{
		printf("Successfully converted %s to %s %s\n", inputFile.string().c_str(), outputFile_y.string().c_str(),
		       outputFile_uv.string().c_str());
		return 0;
	}
	fprintf(stderr, "Conversion failed\n");
	return 1;
}
