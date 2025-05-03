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

		// last line will overflow on the last group so handle separately to avoid branch in the for loop
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
				__m256i s0 = _mm256_and_si256(dwords, mask10);                         // bits 0–9
				__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10);  // bits 10–19
				__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10);  // bits 20–29

				// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
				__m256i yPlane = _mm256_set_epi16(
					0, 0, 0, 0,                                                 // padding
					_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
					_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
					_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8),  // Y7,  Y6
					_mm256_extract_epi16(s2, 6),  _mm256_extract_epi16(s0, 6),  // Y5,  Y4
					_mm256_extract_epi16(s1, 4),  _mm256_extract_epi16(s2, 2),  // Y3,  Y2
					_mm256_extract_epi16(s0, 2),  _mm256_extract_epi16(s1, 0)   // Y1,  Y0
				);
				__m256i uvPlane = _mm256_set_epi16(
					0, 0, 0, 0,                                                 // padding
					_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
					_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
					_mm256_extract_epi16(s2, 8),  _mm256_extract_epi16(s0, 8),  // V6,  U6
					_mm256_extract_epi16(s1, 6),  _mm256_extract_epi16(s2, 4),  // V4,  U4
					_mm256_extract_epi16(s0, 4),  _mm256_extract_epi16(s1, 2),  // V2,  U2
					_mm256_extract_epi16(s2, 0),  _mm256_extract_epi16(s0, 0)   // V0,  U0
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
			__m256i s0 = _mm256_and_si256(dwords, mask10);                         // bits 0–9
			__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10);  // bits 10–19
			__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10);  // bits 20–29

			// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
			__m256i yPlane = _mm256_set_epi16(
				0, 0, 0, 0,                                                 // padding
				_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
				_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
				_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8),  // Y7,  Y6
				_mm256_extract_epi16(s2, 6), _mm256_extract_epi16(s0, 6),  // Y5,  Y4
				_mm256_extract_epi16(s1, 4), _mm256_extract_epi16(s2, 2),  // Y3,  Y2
				_mm256_extract_epi16(s0, 2), _mm256_extract_epi16(s1, 0)   // Y1,  Y0
			);
			__m256i uvPlane = _mm256_set_epi16(
				0, 0, 0, 0,                                                 // padding
				_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
				_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
				_mm256_extract_epi16(s2, 8), _mm256_extract_epi16(s0, 8),  // V6,  U6
				_mm256_extract_epi16(s1, 6), _mm256_extract_epi16(s2, 4),  // V4,  U4
				_mm256_extract_epi16(s0, 4), _mm256_extract_epi16(s1, 2),  // V2,  U2
				_mm256_extract_epi16(s2, 0), _mm256_extract_epi16(s0, 0)   // V0,  U0
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
		__m256i s0 = _mm256_and_si256(dwords, mask10);                         // bits 0–9
		__m256i s1 = _mm256_and_si256(_mm256_srli_epi32(dwords, 10), mask10);  // bits 10–19
		__m256i s2 = _mm256_and_si256(_mm256_srli_epi32(dwords, 20), mask10);  // bits 20–29

		// rearrange into y and uv planes with 12 samples in each, i.e. the lower 192 bits of the register are populated
		__m256i yPlane = _mm256_set_epi16(
			0, 0, 0, 0,                                                 // padding
			_mm256_extract_epi16(s2, 14), _mm256_extract_epi16(s0, 14), // Y11, Y10
			_mm256_extract_epi16(s1, 12), _mm256_extract_epi16(s2, 10), // Y9,  Y8
			_mm256_extract_epi16(s0, 10), _mm256_extract_epi16(s1, 8),  // Y7,  Y6
			_mm256_extract_epi16(s2, 6), _mm256_extract_epi16(s0, 6),   // Y5,  Y4
			_mm256_extract_epi16(s1, 4), _mm256_extract_epi16(s2, 2),   // Y3,  Y2
			_mm256_extract_epi16(s0, 2), _mm256_extract_epi16(s1, 0)    // Y1,  Y0
		);
		__m256i uvPlane = _mm256_set_epi16(
			0, 0, 0, 0,                                                 // padding
			_mm256_extract_epi16(s1, 14), _mm256_extract_epi16(s2, 12), // V10, U10
			_mm256_extract_epi16(s0, 12), _mm256_extract_epi16(s1, 10), // V8,  U8
			_mm256_extract_epi16(s2, 8), _mm256_extract_epi16(s0, 8),  // V6,  U6
			_mm256_extract_epi16(s1, 6), _mm256_extract_epi16(s2, 4),  // V4,  U4
			_mm256_extract_epi16(s0, 4), _mm256_extract_epi16(s1, 2),  // V2,  U2
			_mm256_extract_epi16(s2, 0), _mm256_extract_epi16(s0, 0)   // V0,  U0
		);

		alignas(16) uint16_t tmp[16];

		// left shift from 10 to 16 bit full scale
		__m256i ySamples = _mm256_slli_epi16(yPlane, 6);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp), ySamples);
		std::memcpy(dstLineY, tmp, 24);

		__m256i uvSamples = _mm256_slli_epi16(uvPlane, 6);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(tmp), uvSamples);
		std::memcpy(dstLineUV, tmp, 24);

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
		return true;
	}
	#endif
}

// Class for handling file I/O with C++20 features
class VideoFormatConverter
{
public:
	static bool ConvertFile(const std::filesystem::path& inputFile,
	                        const std::filesystem::path& outputFile_y, const std::filesystem::path& outputFile_uv,
	                        int width, int height)
	{
		if (width <= 0 || height <= 0)
		{
			return false;
		}

		try
		{
			strides strides = CalculateAlignedV210P210Strides(width);

			// Calculate buffer sizes
			size_t v210Size = CalculateV210BufferSize(width, height);

			// Allocate buffers using smart pointers and vectors for exception safety
			std::vector<uint8_t> v210Buffer(v210Size);
			auto planeSize = strides.dstYStride * height * 2;
			std::vector<uint8_t> p210Buffer(planeSize);
			auto p210buffer_y = std::span(p210Buffer).subspan(0, planeSize / 2);
			uint8_t* p210Y = p210buffer_y.data();
			auto p210buffer_uv = std::span(p210Buffer).subspan(planeSize / 2, planeSize / 2);
			uint8_t* p210UV = p210buffer_uv.data();

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
			convert(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height);

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
	#ifdef __AVX__
	auto out_y = "y.avx2";
	auto out_uv = "uv.avx2";
	#else
	auto out_y = "y.scalar";
	auto out_uv = "uv.scalar";
	#endif
	auto cwd = std::filesystem::current_path();
	const auto inputFile = std::filesystem::path(inp);
	const auto outputFile_y = std::filesystem::path(out_y);
	const auto outputFile_uv = std::filesystem::path(out_uv);
	auto width = 3840;
	auto height = 2160;

	if (width <= 0 || height <= 0)
	{
		fprintf(stderr, "Invalid dimensions: width=%d, height=%d\n", width, height);
		return 1;
	}

	if (VideoFormatConverter::ConvertFile(inputFile, outputFile_y, outputFile_uv, width, height))
	{
		printf("Successfully converted %s to %s %s\n", inputFile.string().c_str(), outputFile_y.string().c_str(),
		       outputFile_uv.string().c_str());
		return 0;
	}
	fprintf(stderr, "Conversion failed\n");
	return 1;
}
