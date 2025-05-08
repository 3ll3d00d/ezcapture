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

class r210_rgb48 : public IVideoFrameWriter
{
public:
	r210_rgb48(const log_data& pLogData, uint32_t pX, uint32_t pY) : IVideoFrameWriter(pLogData, pX, pY, &RGB48)
	{
	}

	~r210_rgb48() override = default;

	HRESULT WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame) override;

private:
	#ifdef RECORD_RAW
	uint32_t mFrameCounter;
	#endif
};
