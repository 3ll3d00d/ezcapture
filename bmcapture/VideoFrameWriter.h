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
#define NOMINMAX // quill does not compile without this

#include <intsafe.h>
#include <strmif.h>

#include "bmdomain.h"

class IVideoFrameWriter
{
public:
	IVideoFrameWriter(log_data pLogData) : mLogData(std::move(pLogData))
	{
	}

	virtual ~IVideoFrameWriter() = default;

	virtual HRESULT WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame) = 0;

protected:
	HRESULT CheckFrameSizes(uint64_t frameIndex, long srcSize, IMediaSample* dstFrame)
	{
		auto sizeDelta = srcSize - dstFrame->GetSize();
		if (sizeDelta > 0)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Buffer for frame {} too small, failing (frame: {}, buffer: {})",
				mLogData.prefix, frameIndex, srcSize, dstFrame->GetSize());
			#endif

			return S_FALSE;
		}
		if (sizeDelta < 0)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger,
				"[{}] Buffer for frame {} too large, setting ActualDataLength (frame: {}, buffer: {})",
				mLogData.prefix, frameIndex, srcSize, dstFrame->GetSize());
			#endif

			dstFrame->SetActualDataLength(srcSize);
		}
		return S_OK;
	}

	log_data mLogData;
};
