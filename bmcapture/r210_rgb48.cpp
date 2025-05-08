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

#include "r210_rgb48.h"
#include <atlcomcli.h>
#include <cstdint>
#include <quill/StopWatch.h>
#include <span>
#ifdef RECORD_RAW
#include <filesystem>
#endif

namespace
{
	bool convert(const uint8_t* src, uint16_t* dst, size_t width, size_t height)
	{
		// Each row starts on 256-byte boundary
		size_t srcStride = (width * 4 + 255) / 256 * 256;
		const uint8_t* srcRow = src;
		uint16_t* dstPix = dst;

		for (size_t y = 0; y < height; ++y)
		{
			const uint32_t* srcPixelBE = reinterpret_cast<const uint32_t*>(srcRow);

			for (size_t x = 0; x < width; ++x)
			{
				// r210 is simply BGR once swapped to little endian
				const auto r210Pixel = srcPixelBE[x];
				const auto srcPixel = _byteswap_ulong(r210Pixel);

				const auto red = (srcPixel & 0xFFC) << 4;
				*dstPix = red; 
				dstPix++;

				const auto green = (srcPixel & 0x3FF000) >> 6;
				*dstPix = green;
				dstPix++;

				const auto blue = (srcPixel & 0xFFC00000) >> 16;
				*dstPix = blue;
				dstPix++;
			}

			srcRow += srcStride;
		}
		return true;
	}
}

HRESULT r210_rgb48::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
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

	#ifdef RECORD_RAW
	if (++mFrameCounter % 60 == 0)
	{
		char filename[MAX_PATH];
		strcpy_s(filename, std::filesystem::temp_directory_path().string().c_str());
		CHAR rawFileName[128];
		sprintf_s(rawFileName, "\\r210-%d.raw", mFrameCounter);
		strcat_s(filename, rawFileName);
		FILE* file;
		if (fopen_s(&file, filename, "wb") != 0)
		{
			LOG_WARNING(mLogData.logger, "[{}] Failed to open {}", mLogData.prefix, filename);
		}
		else
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] Wrote input frame {} to raw file", mLogData.prefix, mFrameCounter);
			fwrite(sourceData, srcFrame->GetLength(), 1, file);
			fclose(file);
		}
	}
	#endif

	#ifndef NO_QUILL
	const quill::StopWatchTsc swt;
	#endif

	convert(sourceData, reinterpret_cast<uint16_t*>(outData), width, height);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to RGB48 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
