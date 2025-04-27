/**
 * V210 to P210 Format Converter using C++20 and AVX2
 *
 * This code converts video frames from V210 format (10-bit 4:2:2 YUV packed)
 * to P210 format (10-bit 4:2:2 YUV planar) using AVX2 SIMD instructions.
 *
 * V210 format packs 6 Y, 3 U, and 3 V 10-bit samples into a 128-bit block.
 * P210 format stores Y, U, and V samples in separate planes, each sample as 16-bit.
 */

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <concepts>
#include <immintrin.h> // For AVX2 intrinsics
#include <format>
#include <filesystem>
#include <fstream>
#include <vector>

// Concept to ensure integral types
template <typename T>
concept Integral = std::is_integral_v<T>;

// Helper functions to calculate buffer sizes
constexpr size_t CalculateV210BufferSize(int width, int height)
{
	// V210 requires width to be aligned to 6 samples (6 Y values per 4 32-bit word block)
	int alignedWidth = (width + 5) / 6 * 6;

	// Each aligned row holds alignedWidth/6 * 4 32-bit words
	int bytesPerRow = ((alignedWidth * 8) / 3 + 15) & ~15; // 16-byte alignment

	return bytesPerRow * height;
}

constexpr void CalculateP210BufferSizes(int width, int height, size_t& ySize, size_t& uSize, size_t& vSize)
{
	// P210 format: 16-bit samples, full res Y plane, half-width U and V planes
	ySize = width * height * sizeof(uint16_t);
	uSize = width / 2 * height * sizeof(uint16_t);
	vSize = width / 2 * height * sizeof(uint16_t);
}

/**
 * Converts V210 format to P210 format using AVX2 instructions
 *
 * @param v210Data Pointer to V210 input data
 * @param p210Y Pointer to P210 Y plane output
 * @param p210U Pointer to P210 U plane output
 * @param p210V Pointer to P210 V plane output
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param v210Stride Stride of V210 data in bytes (0 for automatic calculation)
 * @return true if conversion is successful
 */
bool ConvertV210ToP210AVX2(const uint8_t* v210Data, uint16_t* p210Y, uint16_t* p210U, uint16_t* p210V, int width, int height, int v210Stride = 0)
{
    int alignedWidth = (width + 5) / 6 * 6;

    // Calculate stride if not provided
    if (v210Stride == 0)
        v210Stride = ((alignedWidth * 8) / 3 + 15) & ~15; // Aligned to 16 bytes

    // Constants for extraction and shifting (AVX2)
    const __m256i mask_10bit = _mm256_set1_epi32(0x3FF);

    // Process each row
    for (int y = 0; y < height; y++) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(v210Data + y * v210Stride);
        uint16_t* dstY = p210Y + y * width;
        uint16_t* dstU = p210U + y * (width / 2);
        uint16_t* dstV = p210V + y * (width / 2);

        // Process blocks of 12 pixels (2 V210 blocks) at a time using AVX2
        // Each V210 block is 4 32-bit words containing 6 Y, 3 U, and 3 V samples
        int x = 0;
        for (; x + 12 <= width; x += 12) {
            // Load 8 32-bit words (2 complete V210 blocks)
            __m256i words = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
            src += 8;

            // First V210 block (128 bits, 4 words)
            // Word 0: Cr0 (9:0), Y0 (19:10), Cb0 (29:20)
            // Word 1: Y1 (9:0), Cb1 (19:10), Y2 (29:20)
            // Word 2: Cb2 (9:0), Y3 (19:10), Cr1 (29:20)
            // Word 3: Y4 (9:0), Cr2 (19:10), Y5 (29:20)

            // Second V210 block (128 bits, 4 words)
            // Word 4: Cr3 (9:0), Y6 (19:10), Cb3 (29:20)
            // Word 5: Y7 (9:0), Cb4 (19:10), Y8 (29:20)
            // Word 6: Cb5 (9:0), Y9 (19:10), Cr4 (29:20)
            // Word 7: Y10 (9:0), Cr5 (19:10), Y11 (29:20)

            // Extract Y samples (12 total)
            __m256i y_0_6 = _mm256_and_si256(_mm256_srli_epi32(words, 10), mask_10bit);    // Y0, Y6
            __m256i y_1_7 = _mm256_and_si256(words, mask_10bit);                           // Y1, Y7
            __m256i y_2_8 = _mm256_and_si256(_mm256_srli_epi32(words, 20), mask_10bit);    // Y2, Y8

            // Shift to get remaining words into position
            __m256i shifted_words = _mm256_srli_epi32(words, 30);
            shifted_words = _mm256_or_si256(shifted_words, _mm256_slli_epi32(_mm256_srli_si256(words, 4), 2));

            __m256i y_3_9 = _mm256_and_si256(_mm256_srli_epi32(shifted_words, 10), mask_10bit);   // Y3, Y9
            __m256i y_4_10 = _mm256_and_si256(shifted_words, mask_10bit);                         // Y4, Y10
            __m256i y_5_11 = _mm256_and_si256(_mm256_srli_epi32(shifted_words, 20), mask_10bit);  // Y5, Y11

            // Extract U samples (6 total)
            __m256i u_0_3 = _mm256_and_si256(_mm256_srli_epi32(words, 20), mask_10bit);           // U0, U3
            __m256i u_1_4 = _mm256_and_si256(_mm256_srli_epi32(words, 10), mask_10bit);           // U1, U4

            // Shift second set of words
            __m256i u_words = _mm256_srli_epi32(words, 30);
            u_words = _mm256_or_si256(u_words, _mm256_slli_epi32(_mm256_srli_si256(words, 4), 2));
            __m256i u_2_5 = _mm256_and_si256(u_words, mask_10bit);                                // U2, U5

            // Extract V samples (6 total)
            __m256i v_0_3 = _mm256_and_si256(words, mask_10bit);                                  // V0, V3

            // Shift second set of words
            __m256i v_words = _mm256_srli_epi32(words, 30);
            v_words = _mm256_or_si256(v_words, _mm256_slli_epi32(_mm256_srli_si256(words, 4), 2));
            __m256i v_1_4 = _mm256_and_si256(_mm256_srli_epi32(v_words, 20), mask_10bit);         // V1, V4
            __m256i v_2_5 = _mm256_and_si256(_mm256_srli_epi32(v_words, 10), mask_10bit);         // V2, V5

            // Scale 10-bit values to 16-bit (shift left by 6)
            y_0_6 = _mm256_slli_epi32(y_0_6, 6);
            y_1_7 = _mm256_slli_epi32(y_1_7, 6);
            y_2_8 = _mm256_slli_epi32(y_2_8, 6);
            y_3_9 = _mm256_slli_epi32(y_3_9, 6);
            y_4_10 = _mm256_slli_epi32(y_4_10, 6);
            y_5_11 = _mm256_slli_epi32(y_5_11, 6);

            u_0_3 = _mm256_slli_epi32(u_0_3, 6);
            u_1_4 = _mm256_slli_epi32(u_1_4, 6);
            u_2_5 = _mm256_slli_epi32(u_2_5, 6);

            v_0_3 = _mm256_slli_epi32(v_0_3, 6);
            v_1_4 = _mm256_slli_epi32(v_1_4, 6);
            v_2_5 = _mm256_slli_epi32(v_2_5, 6);

            // Pack and store Y samples (12 samples)
            __m256i y_packed_low = _mm256_packus_epi32(y_0_6, y_1_7);
            __m256i y_packed_mid = _mm256_packus_epi32(y_2_8, y_3_9);
            __m256i y_packed_high = _mm256_packus_epi32(y_4_10, y_5_11);

            // Permute to get values in correct order
            y_packed_low = _mm256_permute4x64_epi64(y_packed_low, 0xD8);  // 0b11011000
            y_packed_mid = _mm256_permute4x64_epi64(y_packed_mid, 0xD8);  // 0b11011000
            y_packed_high = _mm256_permute4x64_epi64(y_packed_high, 0xD8); // 0b11011000

            // Store Y values
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dstY), _mm256_castsi256_si128(y_packed_low));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dstY + 4), _mm256_castsi256_si128(y_packed_mid));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dstY + 8), _mm256_castsi256_si128(y_packed_high));

            // Pack and store U samples (6 samples)
            __m256i u_packed = _mm256_packus_epi32(u_0_3, u_1_4);
            __m256i u_final = _mm256_packus_epi32(u_2_5, _mm256_setzero_si256());

            u_packed = _mm256_permute4x64_epi64(u_packed, 0xD8);  // 0b11011000
            u_final = _mm256_permute4x64_epi64(u_final, 0xD8);    // 0b11011000

            // Store U values - full 6 values are guaranteed to fit
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dstU), _mm256_castsi256_si128(u_packed));
            _mm_storel_epi64(reinterpret_cast<__m128i*>(dstU + 4), _mm256_castsi256_si128(u_final));

            // Pack and store V samples (6 samples)
            __m256i v_packed = _mm256_packus_epi32(v_0_3, v_1_4);
            __m256i v_final = _mm256_packus_epi32(v_2_5, _mm256_setzero_si256());

            v_packed = _mm256_permute4x64_epi64(v_packed, 0xD8);  // 0b11011000
            v_final = _mm256_permute4x64_epi64(v_final, 0xD8);    // 0b11011000

            // Store V values - full 6 values are guaranteed to fit
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dstV), _mm256_castsi256_si128(v_packed));
            _mm_storel_epi64(reinterpret_cast<__m128i*>(dstV + 4), _mm256_castsi256_si128(v_final));

            dstY += 12;
            dstU += 6;
            dstV += 6;
        }

        // Process remaining pixels using scalar code
        for (; x < width; x += 6) {
            // V210 block (128 bits, 4 words) containing 6 Y, 3 U, and 3 V samples
            // Process one 128-bit block (32 bits * 4)

            // Word 0: Cr0 (9:0), Y0 (19:10), Cb0 (29:20)
            uint32_t word0 = *src++;
            uint16_t u0 = static_cast<uint16_t>((word0 >> 20) & 0x3FF);  // Cb0
            uint16_t y0 = static_cast<uint16_t>((word0 >> 10) & 0x3FF);  // Y0
            uint16_t v0 = static_cast<uint16_t>(word0 & 0x3FF);          // Cr0

            // Word 1: Y1 (9:0), Cb1 (19:10), Y2 (29:20)
            uint32_t word1 = *src++;
            uint16_t y2 = static_cast<uint16_t>((word1 >> 20) & 0x3FF);  // Y2
            uint16_t u1 = static_cast<uint16_t>((word1 >> 10) & 0x3FF);  // Cb1
            uint16_t y1 = static_cast<uint16_t>(word1 & 0x3FF);          // Y1

            // Word 2: Cb2 (9:0), Y3 (19:10), Cr1 (29:20)
            uint32_t word2 = *src++;
            uint16_t v1 = static_cast<uint16_t>((word2 >> 20) & 0x3FF);  // Cr1
            uint16_t y3 = static_cast<uint16_t>((word2 >> 10) & 0x3FF);  // Y3
            uint16_t u2 = static_cast<uint16_t>(word2 & 0x3FF);          // Cb2

            // Word 3: Y4 (9:0), Cr2 (19:10), Y5 (29:20)
            uint32_t word3 = *src++;
            uint16_t y5 = static_cast<uint16_t>((word3 >> 20) & 0x3FF);  // Y5
            uint16_t v2 = static_cast<uint16_t>((word3 >> 10) & 0x3FF);  // Cr2
            uint16_t y4 = static_cast<uint16_t>(word3 & 0x3FF);          // Y4

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

            // Store the luma samples, checking bounds
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

            // Add only the number of items we actually stored
            int yAdvance = std::min(6, width - x);
            int uvAdvance = std::min(3, (width - x + 1) / 2); // Ceiling division by 2

            dstY += yAdvance;
            dstU += uvAdvance;
            dstV += uvAdvance;
        }
    }

	return true;
}

// Class for handling file I/O with C++20 features
class VideoFormatConverter
{
public:
	static bool ConvertFile(const std::filesystem::path& inputFile,
	                        const std::filesystem::path& outputFile,
	                        int width, int height)
	{
		if (width <= 0 || height <= 0)
		{
			return false;
		}

		try
		{
			// Calculate buffer sizes
			size_t v210Size = CalculateV210BufferSize(width, height);
			size_t ySize, uSize, vSize;
			CalculateP210BufferSizes(width, height, ySize, uSize, vSize);

			// Allocate buffers using smart pointers and vectors for exception safety
			std::vector<uint8_t> v210Buffer(v210Size);
			std::vector<uint8_t> p210Buffer(ySize + uSize + vSize);
			uint16_t* p210Y = reinterpret_cast<uint16_t*>(std::span(p210Buffer).subspan(0, ySize).data());
			uint16_t* p210U = reinterpret_cast<uint16_t*>(std::span(p210Buffer).subspan(ySize, uSize).data());
			uint16_t* p210V = reinterpret_cast<uint16_t*>(std::span(p210Buffer).subspan(ySize + uSize, vSize).data());
			uint16_t* p210 = reinterpret_cast<uint16_t*>(std::span(p210Buffer).data());
			auto p210Yin = p210;
			auto p210Uin = p210 + (ySize / 2);
			auto p210Vin = p210 + ((ySize + uSize ) / 2);

			// Read input file using standard file streams
			std::ifstream inFile(inputFile, std::ios::binary);
			if (!inFile)
			{
				throw std::runtime_error(std::format("Failed to open input file: {}", inputFile.string()));
			}

			if (!inFile.read(reinterpret_cast<char*>(v210Buffer.data()), v210Size))
			{
				throw std::runtime_error("Failed to read V210 data");
			}

			// Convert the data
			if (!ConvertV210ToP210AVX2(v210Buffer.data(), p210Yin, p210Uin,p210Vin, width, height))
			{
				throw std::runtime_error("Conversion failed");
			}

			// Write output file
			std::ofstream outFile(outputFile, std::ios::binary);
			if (!outFile)
			{
				throw std::runtime_error(std::format("Failed to open output file: {}", outputFile.string()));
			}

			// Write P210 data (Y plane followed by U plane followed by V plane)
			outFile.write(reinterpret_cast<const char*>(p210Y), ySize);
			outFile.write(reinterpret_cast<const char*>(p210U), uSize);
			outFile.write(reinterpret_cast<const char*>(p210V), vSize);

			if (!outFile)
			{
				throw std::runtime_error("Failed to write P210 data");
			}

			return true;
		}
		catch (const std::exception& e)
		{
			// Print error message
			fprintf(stderr, "Error: %s\n", e.what());
			return false;
		}
	}
};

// Main function using C++20 features
int main(int argc, char* argv[])
{
	// if (argc < 5) {
	// fprintf(stderr, "Usage: %s <input_v210_file> <output_p210_file> <width> <height>\n", argv[0]);
	// return 1;
	// }
	// const auto inputFile = std::filesystem::path(argv[1]);
	// const auto outputFile = std::filesystem::path(argv[2]);
	// int width = std::stoi(argv[3]);
	// int height = std::stoi(argv[4]);
	auto inp = "demo.dat";
	auto out = "demo.out";
	auto cwd = std::filesystem::current_path();
	const auto inputFile = std::filesystem::path(inp);
	const auto outputFile = std::filesystem::path(out);
	auto width = 3840;
	auto height = 2160;

	if (width <= 0 || height <= 0)
	{
		fprintf(stderr, "Invalid dimensions: width=%d, height=%d\n", width, height);
		return 1;
	}

	if (VideoFormatConverter::ConvertFile(inputFile, outputFile, width, height))
	{
		printf("Successfully converted %s to %s\n", inputFile.string().c_str(), outputFile.string().c_str());
		return 0;
	}
	fprintf(stderr, "Conversion failed\n");
	return 1;
}
