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

#include "yuv2_yv16.h"
#include <atlcomcli.h>
#include <cstdint>
#include <dvdmedia.h>
#include <quill/StopWatch.h>
#include <span>

namespace
{
	#ifdef __AVX2__
	bool convert(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width, int height, int pixelsToPad)
	{
		const __m256i shuffle_1 = _mm256_setr_epi8(
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15,
			2, 6, 10, 14, 0, 4, 8, 12, 1, 3, 5, 7, 9, 11, 13, 15
		);
		const __m256i permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 3, 6, 7);
		const int yWidth = width + pixelsToPad;
		const int uvWidth = yWidth / 2;
		for (int y = 0; y < height; ++y)
		{
			uint64_t* y_out = reinterpret_cast<uint64_t*>(yPlane + (y * yWidth));
			uint64_t* u_out = reinterpret_cast<uint64_t*>(uPlane + (y * uvWidth));
			uint64_t* v_out = reinterpret_cast<uint64_t*>(vPlane + ( y * uvWidth));
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
		return true;
	}
	#else
	bool convert(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane, int width, int height, int pixelsToPad)
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
}

HRESULT yuv2_yv16::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	auto hr = CheckFrameSizes(srcFrame->GetFrameIndex(), mExpectedImageSize, dstFrame);
	if (S_FALSE == hr)
	{
		return S_FALSE;
	}
	const auto width = srcFrame->GetVideoFormat().cx;
	const auto height = srcFrame->GetVideoFormat().cy;

	if (S_PADDING_POSSIBLE == hr)
	{
		AM_MEDIA_TYPE* mt;
		dstFrame->GetMediaType(&mt);
		if (mt)
		{
			auto header = reinterpret_cast<VIDEOINFOHEADER2*>(mt->pbFormat);
			int paddedWidth = header->bmiHeader.biWidth;
			mPixelsToPad = std::max(paddedWidth - width, 0);

			#ifndef NO_QUILL
			if (mPixelsToPad > 0)
			{
				LOG_TRACE_L2(mLogData.logger,
					"[{}] Padding requested by render, image width is {} but renderer requests padding to {}",
					mLogData.prefix, width, paddedWidth);
			}
			#endif
		}
	}
	auto actualWidth = width + mPixelsToPad;
	const auto pixelCount = actualWidth * height;

	void* d;
	srcFrame->Start(&d);
	const uint8_t* sourceData = static_cast<const uint8_t*>(d);

	BYTE* outData;
	dstFrame->GetPointer(&outData);
	auto dstSize = dstFrame->GetSize();

	// YV16 format: 
	auto ySize = pixelCount;
	auto uvSize = pixelCount / 2;

	auto outSpan = std::span(outData, dstSize);
	uint8_t* yPlane = outSpan.subspan(0, ySize).data();
	uint8_t* vPlane = outSpan.subspan(ySize, uvSize).data();
	uint8_t* uPlane = outSpan.subspan(ySize + uvSize, uvSize).data();

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convert(sourceData, yPlane, uPlane, vPlane, width, srcFrame->GetVideoFormat().cy, mPixelsToPad);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to YV16 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
