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
#include "VideoFrameWriter.h"
#include <span>

#ifndef NO_QUILL
#include <quill/StopWatch.h>
#endif

template <typename VF>
class y210_p210 : public IVideoFrameWriter<VF>
{
public:
	y210_p210(const log_data& pLogData, int pX, int pY) : IVideoFrameWriter<VF>(pLogData, pX, pY, &P210)
	{
	}

	~y210_p210() override = default;

	HRESULT WriteTo(VF* srcFrame, IMediaSample* dstFrame) override
	{
		const auto width = srcFrame->GetWidth();
		const auto height = srcFrame->GetHeight();

		auto hr = this->DetectPadding(srcFrame->GetFrameIndex(), width, dstFrame);
		if (S_FALSE == hr)
		{
			return S_FALSE;
		}
		auto actualWidth = width + this->mPixelsToPad;

		void* d;
		srcFrame->Start(&d);
		const uint8_t* sourceData = static_cast<const uint8_t*>(d);

		BYTE* outData;
		dstFrame->GetPointer(&outData);
		auto dstSize = dstFrame->GetSize();

		// P210 format: 16-bit samples, full res Y plane, half-width U and V planes
		auto outSpan = std::span(outData, dstSize);
		auto planeSize = actualWidth * height * 2;

		uint8_t* yPlane = outSpan.subspan(0, planeSize).data();
		uint8_t* uvPlane = outSpan.subspan(planeSize, planeSize).data();

		auto alignedWidth = (width + 47) / 48 * 48;
		auto srcStride = alignedWidth * 8 / 3;

		#ifndef NO_QUILL
		const quill::StopWatchTsc swt;
		#endif

		this->convert(sourceData, srcStride, yPlane, uvPlane, width, height, this->mPixelsToPad);

		#ifndef NO_QUILL
		auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
		LOG_TRACE_L3(this->mLogData.logger, "[{}] Converted frame to P210 in {:.3f} ms", this->mLogData.prefix,
		             execTime);
		#endif

		srcFrame->End();

		return S_OK;
	}

private:
	// same as yuy2 but each individual value occupies 16 bits
	#ifdef __AVX2__
	bool convert(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height, int pixelsToPad)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13,
			2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13
		);
		const __m256i permute = _mm256_setr_epi32(0, 1, 4, 5, 2, 3, 6, 7);
		const int yWidth = width + pixelsToPad;
		const int uvWidth = yWidth / 2;

		for (int lineNo = 0; lineNo < height; ++lineNo)
		{
			const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(src + lineNo * srcStride);
			uint16_t* dstLineY = reinterpret_cast<uint16_t*>(dstY + lineNo * effectiveWidth * 2);
			uint16_t* dstLineUV = reinterpret_cast<uint16_t*>(dstUV + lineNo * effectiveWidth * 2);

			for (int x = 0; x < width; x += 8) // 32 bits per pixel in 256 bit chunks = 8 pixels per pass
			{
				__m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
				__m256i shuffled = _mm256_shuffle_epi8(pixels, shuffle_1);
				__m256i permuted = _mm256_permutevar8x32_epi32(shuffled, permute);
				_mm256_storeu2_m128i(reinterpret_cast<__m128i*>(dstLineY), reinterpret_cast<__m128i*>(dstLineUV), permuted);
				// 32 bytes read
				uv_out += 16
				y_out += 16;
				src += 32; 
			}
		}
		return true;
	}
	#else
	bool convert(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	             int pixelsToPad)
	{
		auto effectiveWidth = width + pixelsToPad;

		for (int y = 0; y < height; y++)
		{
			const uint16_t* srcLine = reinterpret_cast<const uint16_t*>(src + y * srcStride);
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
				src += 4;
			}
		}
		return true;
	}
	#endif
};
