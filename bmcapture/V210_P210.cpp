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

#include "v210_p210.h"
#include "v210.h"
#include <atlcomcli.h>
#include <quill/StopWatch.h>
#include <span>

HRESULT v210_p210::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	const auto width = srcFrame->GetVideoFormat().cx;
	const auto height = srcFrame->GetVideoFormat().cy;

	auto hr = DetectPadding(srcFrame->GetFrameIndex(), width, dstFrame);
	if (S_FALSE == hr)
	{
		return S_FALSE;
	}
	auto actualWidth = width + mPixelsToPad;

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

	v210::convert(sourceData, srcStride, yPlane, uvPlane, width, height, mPixelsToPad);

	#ifndef NO_QUILL
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
	LOG_TRACE_L3(mLogData.logger, "[{}] Converted frame to P210 in {:.3f} ms", mLogData.prefix, execTime);
	#endif

	srcFrame->End();

	return S_OK;
}
