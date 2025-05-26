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
#include "mwdomain.h"

class StraightThrough : public IVideoFrameWriter<VideoSampleBuffer>
{
public:
	StraightThrough(const log_data& pLogData, int pX, int pY, const pixel_format* pPixelFormat)
		: IVideoFrameWriter(pLogData, pX, pY, pPixelFormat)
	{
	}

	~StraightThrough() override = default;

	HRESULT WriteTo(VideoSampleBuffer* srcFrame, IMediaSample* dstFrame) override
	{
		if (S_FALSE == CheckFrameSizes(srcFrame->GetFrameIndex(), mOutputImageSize, dstFrame))
		{
			return S_FALSE;
		}
		// TODO handle padding?
		return S_OK;
	}
};
