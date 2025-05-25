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
#include "yuv2.h"
#include <atlcomcli.h>
#include <quill/StopWatch.h>
#include <span>

HRESULT yuv2_yv16::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	const auto width = srcFrame->GetVideoFormat().cx;
	const auto height = srcFrame->GetVideoFormat().cy;

	auto hr = DetectPadding(srcFrame->GetFrameIndex(), width, dstFrame);
	if (S_FALSE == hr)
	{
		return S_FALSE;
	}
	const auto actualWidth = width + mPixelsToPad;
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

	yuv2::convert(sourceData, yPlane, uPlane, vPlane, width, srcFrame->GetVideoFormat().cy, mPixelsToPad);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L3(mLogData.logger, "[{}] Converted frame to YV16 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
