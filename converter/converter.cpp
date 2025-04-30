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

void convertScalar(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height)
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
			dstLineY[0] = samples[1]; // Y0
			dstLineY[1] = samples[3]; // Y1
			dstLineY[2] = samples[5]; // Y2
			dstLineY[3] = samples[7]; // Y3
			dstLineY[4] = samples[9]; // Y4
			dstLineY[5] = samples[11]; // Y5

			// Arrange and store UV interleaved plane
			dstLineUV[0] = samples[0]; // U0
			dstLineUV[1] = samples[2]; // V0
			dstLineUV[2] = samples[4]; // U2
			dstLineUV[3] = samples[6]; // V2
			dstLineUV[4] = samples[8]; // U4
			dstLineUV[5] = samples[10]; // V4

			dstLineY += 6;
			dstLineUV += 6;
			srcLine += 4;
		}
	}
}

void V210ToP210_AVX2_Fast(
	const uint8_t* src, int srcStride,
	uint8_t* dstY, int dstYStride,
	uint8_t* dstUV, int dstUVStride,
	int width, int height)
{
	const int groupsPerLine = width / 12;
	const int tailPixels = width % 12;

	const __m256i mask_10bit = _mm256_set1_epi32(0x3FF);

	// Dynamically allocate 64-byte aligned memory for temp buffer
	uint16_t* temp = reinterpret_cast<uint16_t*>(_aligned_malloc(64 * sizeof(uint16_t), 32));
	// 64 bytes for temp buffer
	if (!temp)
	{
		// Handle memory allocation failure if necessary
		return;
	}

	for (int y = 0; y < height; ++y)
	{
		const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + y * srcStride);
		uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + y * dstYStride);
		uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + y * dstUVStride);

		int x = 0;
		int uvIndex = 0; // UV plane moves half-speed

		for (int g = 0; g < groupsPerLine; ++g)
		{
			__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine + g * 8));

			__m256i lo10 = _mm256_and_si256(v, mask_10bit);
			__m256i mid10 = _mm256_and_si256(_mm256_srli_epi32(v, 10), mask_10bit);
			__m256i hi10 = _mm256_and_si256(_mm256_srli_epi32(v, 20), mask_10bit);

			__m256i unpack_lo = _mm256_unpacklo_epi32(lo10, mid10);
			__m256i unpack_hi = _mm256_unpackhi_epi32(lo10, mid10);

			__m256i unpack0 = _mm256_unpacklo_epi32(unpack_lo, hi10);
			__m256i unpack1 = _mm256_unpackhi_epi32(unpack_lo, hi10);
			__m256i unpack2 = _mm256_unpacklo_epi32(unpack_hi, hi10);
			__m256i unpack3 = _mm256_unpackhi_epi32(unpack_hi, hi10);

			alignas(32) uint16_t* temp32 = temp;

			_mm256_store_si256(reinterpret_cast<__m256i*>(temp32), unpack0);
			_mm_store_si128(reinterpret_cast<__m128i*>(temp32 + 8), _mm256_castsi256_si128(unpack1));
			_mm_store_si128(reinterpret_cast<__m128i*>(temp32 + 12), _mm256_extracti128_si256(unpack2, 0));
			_mm_store_si128(reinterpret_cast<__m128i*>(temp32 + 16), _mm256_extracti128_si256(unpack3, 0));

			// Write 12 pixels (Y) and 6 UV pairs
			for (int i = 0; i < 12; i += 2)
			{
				// 2 Y samples
				dstLineY[x + i] = temp[1 + i * 2];
				dstLineY[x + i + 1] = temp[3 + i * 2];

				// U and V samples for the pair
				dstLineUV[uvIndex + 0] = temp[0 + i * 2]; // U
				dstLineUV[uvIndex + 1] = temp[2 + i * 2]; // V

				uvIndex += 2;
			}

			x += 12;
		}

		if (tailPixels > 0)
		{
			// Scalar fallback for leftovers
			const uint32_t* srcTail = srcLine + groupsPerLine * 8;
			int tailBytes = ((tailPixels * 2 + tailPixels / 2 + 1) / 2) * 4;
			uint8_t temp[64] = {0};
			std::memcpy(temp, srcTail, tailBytes);

			const uint32_t* p = reinterpret_cast<const uint32_t*>(temp);

			int pixelsDone = 0;
			while (pixelsDone < tailPixels)
			{
				uint16_t u = (p[0] >> 0) & 0x3FF;
				uint16_t y0 = (p[0] >> 10) & 0x3FF;
				uint16_t v = (p[0] >> 20) & 0x3FF;
				uint16_t y1 = (p[1] >> 0) & 0x3FF;

				// Write Y samples
				dstLineY[x + 0] = y0;
				dstLineY[x + 1] = y1;

				// Write UV pair
				dstLineUV[uvIndex + 0] = u;
				dstLineUV[uvIndex + 1] = v;

				pixelsDone += 2;
				x += 2;
				uvIndex += 2;

				p += 1;
			}
		}
	}

	// Free the aligned memory
	_aligned_free(temp);
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
			strides strides = CalculateAlignedV210P210Strides(width);

			// Calculate buffer sizes
			size_t v210Size = CalculateV210BufferSize(width, height);

			// Allocate buffers using smart pointers and vectors for exception safety
			std::vector<uint8_t> v210Buffer(v210Size);
			auto planeSize = strides.dstYStride * height * 2;
			std::vector<uint8_t> p210Buffer(planeSize);
			uint8_t* p210Y = std::span(p210Buffer).subspan(0, planeSize / 2).data();
			uint8_t* p210UV = std::span(p210Buffer).subspan(planeSize / 2, planeSize / 2).data();

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
			convertScalar(v210Buffer.data(), strides.srcStride, p210Y, p210UV, width, height);
			// V210ToP210_AVX2_Fast(v210Buffer.data(), strides.srcStride, p210Y, strides.dstYStride, p210UV,
			// strides.dstUVStride, width, height);

			// Write output file
			std::ofstream outFile(outputFile, std::ios::binary);
			if (!outFile)
			{
				throw std::runtime_error(std::format("Failed to open output file: {}", outputFile.string()));
			}

			// Write P210 data (Y plane followed by UV interleaved plane)
			outFile.write(reinterpret_cast<const char*>(p210Buffer.data()), p210Buffer.size());

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
