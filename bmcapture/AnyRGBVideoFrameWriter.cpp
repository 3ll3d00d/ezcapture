/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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

#include "AnyRGBVideoFrameWriter.h"
// #include "quill/std/Chrono.h"

HRESULT AnyRGBVideoFrameWriter::Write(IDeckLinkVideoFrame* srcFrame)
{
	IDeckLinkVideoFrame* convertedFrame;
	auto t1 = std::chrono::high_resolution_clock::now();
	auto result = mConverter->ConvertNewFrame(srcFrame, bmdFormat8BitBGRA, bmdColorspaceUnknown, nullptr,
	                                          &convertedFrame);
	auto t2 = std::chrono::high_resolution_clock::now();

	if (S_OK == result)
	{
		std::chrono::duration<double, std::milli> conversionTime = t2 - t1;
		// #ifndef NO_QUILL
		// LOG_TRACE_L3(mLogData.logger, "[{}] Converted frame to BGRA in {:.3f} ms", mLogData.prefix, conversionTime);
		// #endif
		//
		// 	newVideoFormat.pixelEncoding = RGB_444;
		// 	newVideoFormat.bitCount = BITS_RGBA;
		// 	newVideoFormat.pixelStructure = FOURCC_RGBA;
		// 	newVideoFormat.bitDepth = 8;
		// 	newVideoFormat.pixelStructureName = "RGBA";
		// 	GetImageDimensions(newVideoFormat.pixelStructure, newVideoFormat.cx, newVideoFormat.cy,
		// 		&newVideoFormat.lineLength, &newVideoFormat.imageSize);
		return S_OK;
	}
	else
	{
		// #ifndef NO_QUILL
		// LOG_WARNING(mLogData.logger, "[{}] Failed to convert frame to BGRA {:#08x}", mLogData.prefix, result);
		// #endif
		return E_FAIL;
	}
}
