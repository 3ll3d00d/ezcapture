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

#include "DeckLinkAPI_h.h"
#include "logging.h"

class IVideoFrameWriter
{
public:
	virtual HRESULT WriteTo(IDeckLinkVideoFrame* srcFrame) = 0;

protected:
	IVideoFrameWriter(log_data pLogData) : mLogData(std::move(pLogData))
	{
	}

	~IVideoFrameWriter() = default;

	log_data mLogData;
};
