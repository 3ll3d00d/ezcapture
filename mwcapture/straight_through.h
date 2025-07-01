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
#ifndef MW_STRAIGHT_THROUGH_HEADER
#define MW_STRAIGHT_THROUGH_HEADER

#include "VideoFrameWriter.h"
#include "mw_domain.h"

class straight_through : public IVideoFrameWriter<video_sample_buffer>
{
public:
	straight_through(const log_data& pLogData, int pX, int pY, const pixel_format* pPixelFormat)
		: IVideoFrameWriter(pLogData, pX, pY, pPixelFormat)
	{
	}

	~straight_through() override = default;

	HRESULT WriteTo(video_sample_buffer* srcFrame, IMediaSample* dstFrame) override
	{
		if (S_FALSE == CheckFrameSizes(srcFrame->GetFrameIndex(), mOutputImageSize, dstFrame))
		{
			return S_FALSE;
		}
		// TODO handle padding?
		return S_OK;
	}
};
#endif