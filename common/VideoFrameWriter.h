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
#ifndef VIDEO_FRAME_WRITER_HEADER
#define VIDEO_FRAME_WRITER_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <intsafe.h>
#include <strmif.h>
#include <dvdmedia.h>
#include "domain.h"
#include "logging.h"

#define S_PADDING_POSSIBLE    ((HRESULT)200L)

template<typename VF>
class IVideoFrameWriter
{
public:
	IVideoFrameWriter(log_data pLogData, int pX, int pY, const pixel_format* pPixelFormat) : mLogData(std::move(pLogData))
	{
		pPixelFormat->GetImageDimensions(pX, pY, &mOutputRowLength, &mOutputImageSize);
	}

	virtual ~IVideoFrameWriter() = default;

	virtual HRESULT WriteTo(VF* srcFrame, IMediaSample* dstFrame) = 0;

protected:
	HRESULT CheckFrameSizes(uint64_t frameIndex, long srcSize, IMediaSample* dstFrame)
	{
		auto sizeDelta = srcSize - dstFrame->GetSize();
		if (sizeDelta > 0)
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Framebuffer {} too small, failing (src: {}, dst: {})",
				mLogData.prefix, frameIndex, srcSize, dstFrame->GetSize());
			#endif

			return S_FALSE;
		}
		if (sizeDelta < 0)
		{
			return S_PADDING_POSSIBLE;
		}
		return S_OK;
	}

	HRESULT DetectPadding(uint64_t frameIndex, int expectedWidth, IMediaSample* dstFrame)
	{
		auto hr = CheckFrameSizes(frameIndex, mOutputImageSize, dstFrame);
		if (S_FALSE == hr)
		{
			return S_FALSE;
		}
		if (S_PADDING_POSSIBLE == hr)
		{
			AM_MEDIA_TYPE* mt;
			dstFrame->GetMediaType(&mt);
			if (mt)
			{
				auto header = reinterpret_cast<VIDEOINFOHEADER2*>(mt->pbFormat);
				int paddedWidth = header->bmiHeader.biWidth;
				mPixelsToPad = std::max(paddedWidth - expectedWidth, 0);

				#ifndef NO_QUILL
				if (mPixelsToPad > 0)
				{
					LOG_TRACE_L2(mLogData.logger,
						"[{}] Padding requested by render, image width is {} but renderer requests padding to {}",
						mLogData.prefix, expectedWidth, paddedWidth);
				}
				#endif
			}
		}
		return S_OK;
	}

	log_data mLogData;
	DWORD mOutputImageSize{0};
	DWORD mOutputRowLength{0};
	int mPixelsToPad{0};
};
#endif