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

#include "Any_RGBVideoFrameWriter.h"
#include "quill/StopWatch.h"

HRESULT Any_RGBVideoFrameWriter::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	IDeckLinkVideoFrame* convertedFrame;
	const quill::StopWatchTsc swt;
	// TODO convert directly into the media sample?
	auto result = mConverter->ConvertNewFrame(srcFrame->GetRawFrame(), bmdFormat8BitBGRA, bmdColorspaceUnknown, nullptr,
	                                          &convertedFrame);
	auto execMillis = swt.elapsed_as<std::chrono::milliseconds>();
	if (S_OK != result)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Failed to convert frame to BGRA {:#08x}", mLogData.prefix, result);
		#endif

		if (convertedFrame) convertedFrame->Release();

		return S_FALSE;
	}

	#ifndef NO_QUILL
	LOG_TRACE_L2(mLogData.logger, "[{}] Converted frame to BGRA in {:.3f} ms", mLogData.prefix,
	             execMillis);
	#endif

	VideoFrame vf(mLogData, srcFrame->GetVideoFormat(), srcFrame->GetCaptureTime(), srcFrame->GetFrameTime(),
	              srcFrame->GetFrameDuration(), srcFrame->GetFrameIndex(), convertedFrame);
	CheckFrameSizes(vf.GetFrameIndex(), vf.GetLength(), dstFrame);
	auto hr = vf.CopyData(dstFrame);
	convertedFrame->Release();

	return hr;
}
