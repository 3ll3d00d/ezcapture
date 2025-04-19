/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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
#include <atlcomcli.h>
#include "DeckLinkAPI_h.h"
// #include "domain.h"

class AnyRGBVideoFrameWriter : public IVideoFrameWriter
{
public:
	HRESULT Write(IDeckLinkVideoFrame* srcFrame) override;

protected:
	~AnyRGBVideoFrameWriter() = default;

private:
	AnyRGBVideoFrameWriter(const log_data& pLogData) : IVideoFrameWriter(pLogData)
	{
		auto result = mConverter.CoCreateInstance(CLSID_CDeckLinkVideoConversion, nullptr);
		if (S_OK == result)
		{
			#ifndef NO_QUILL
			LOG_INFO(mLogData.logger, "[{}] Created Frame Converter", mLogData.prefix);
			#endif
		}
		else
		{
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Failed to create Frame Converter {:#08x}", mLogData.prefix, result);
			#endif
		}
	}

	CComPtr<IDeckLinkVideoConversion> mConverter;
};
