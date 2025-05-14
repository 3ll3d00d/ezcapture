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

#include "DeckLinkAPI_h.h"
#include "logging.h"
#include <atomic>
#include <strmif.h>
#include <memory>

class MediaSampleBackedDecklinkBuffer : public IDeckLinkVideoBuffer
{
public:
	MediaSampleBackedDecklinkBuffer(log_data pLogData, IMediaSample* pSample) :
		mLogData(std::move(pLogData)),
		mSample(pSample)
	{
	}

	virtual ~MediaSampleBackedDecklinkBuffer() = default;

	/////////////////////////
	// IUnknown
	/////////////////////////
	HRESULT QueryInterface(const IID& riid, void** ppvObject) override;

	ULONG AddRef() override
	{
		return mRefCount++;
	}

	ULONG Release() override
	{
		auto ct = --mRefCount;
		if (ct <= 0)
		{
			delete this;
		}
		return ct;
	}

	/////////////////////////
	// IDeckLinkVideoBuffer
	/////////////////////////
	HRESULT StartAccess(BMDBufferAccessFlags flags) override
	{
		// nop, seems irrelevant
		return S_OK;
	}

	HRESULT EndAccess(BMDBufferAccessFlags flags) override
	{
		// nop, seems irrelevant
		return S_OK;
	}

	HRESULT GetBytes(void** buffer) override;

private:
	log_data mLogData;
	std::atomic_int mRefCount;
	std::shared_ptr<IMediaSample> mSample;
};
