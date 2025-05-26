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

#include "any_rgb.h"

#include "MediaSampleBackedDecklinkBuffer.h"
#include "quill/StopWatch.h"

HRESULT any_rgb::WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame)
{
	if (S_FALSE == CheckFrameSizes(srcFrame->GetFrameIndex(), mOutputImageSize, dstFrame))
	{
		return S_FALSE;
	}

	IDeckLinkVideoFrame* convertedFrame;
	MediaSampleBackedDecklinkBuffer wrapper(mLogData, dstFrame);

	const quill::StopWatchTsc swt;
	auto result = mConverter->ConvertNewFrame(srcFrame->GetRawFrame(), bmdFormat8BitBGRA, bmdColorspaceUnknown,
	                                          &wrapper, &convertedFrame);
	auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;

	if (S_OK != result)
	{
		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] Failed to convert frame to BGRA {:#08x}", mLogData.prefix,
		            static_cast<unsigned long>(result));
		#endif

		if (convertedFrame) convertedFrame->Release();

		return S_FALSE;
	}

	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] Converted frame to BGRA in {:.3f} ms", mLogData.prefix,
	             execTime);
	#endif

	convertedFrame->Release();

	return S_OK;
}
