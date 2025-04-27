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

/**
  * V210 to P210 conversion using AVX2 instructions.
  *
  * V210: 4:2:2 YUV packed format with 10-bit samples in 32-bit words
  * P210: 4:2:2 YUV planar format with 16-bit samples (10-bit in LSBs)
  *
  * V210 packs 6 pixels(6Y, 3U, 3V) components into 4 32-bit words (16 bytes).
  * P210 stores components in separate planes with 16-bit samples.
  */
class v210_p210 : public IVideoFrameWriter
{
public:
	v210_p210(const log_data& pLogData, int pX, int pY) : IVideoFrameWriter(pLogData)
	{
		P210.GetImageDimensions(pX, pY, &mExpectedRowLength, &mExpectedImageSize);
	}

	~v210_p210() override = default;

	HRESULT WriteTo(VideoFrame* srcFrame, IMediaSample* dstFrame) override;

private:
	DWORD mExpectedImageSize;
	DWORD mExpectedRowLength;
};
