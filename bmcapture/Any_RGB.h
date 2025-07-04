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
#ifndef ANY_RGB_HEADER
#define ANY_RGB_HEADER

#include "VideoFrameWriter.h"
#include <atlcomcli.h>
#include "video_frame.h"

class any_rgb : public IVideoFrameWriter<video_frame>
{
public:
	any_rgb(const log_data& pLogData, uint32_t pX, uint32_t pY) :
		IVideoFrameWriter(pLogData, pX, pY, &RGBA)
	{
		auto result = mConverter.CoCreateInstance(CLSID_CDeckLinkVideoConversion, nullptr);
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Created Frame Converter for frame {} x {}", mLogData.prefix, pX, pY);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Failed to create Frame Converter {:#08x}", mLogData.prefix, result);
			#endif
		}
		DWORD b;
		RGBA.GetImageDimensions(pX, pY, &b, &mOutputImageSize);
	}

	~any_rgb() override = default;

	HRESULT WriteTo(video_frame* srcFrame, IMediaSample* dstFrame) override;

private:
	CComPtr<IDeckLinkVideoConversion> mConverter;
};
#endif