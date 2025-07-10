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
#ifndef BM_VIDEO_FRAME_HEADER
#define BM_VIDEO_FRAME_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <DeckLinkAPI_h.h>
#include "domain.h"
#include "logging.h"
#include <strmif.h>

class video_frame
{
public:
	video_frame(log_data logData, video_format format, int64_t captureTime, int64_t frameTime, int64_t duration,
	            uint64_t index, IDeckLinkVideoFrame* frame) :
		mFormat(std::move(format)),
		mCaptureTime(captureTime),
		mFrameTime(frameTime),
		mFrameDuration(duration),
		mFrameIndex(index),
		mLogData(std::move(logData)),
		mFrame(frame)
	{
		frame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void**>(&mBuffer));

		#ifndef NO_QUILL
		// hack to get current ref count
		frame->AddRef();
		auto ct = frame->Release();
		LOG_TRACE_L3(mLogData.logger, "[{}] VideoFrame Access (new) {} {}", mLogData.prefix, index, ct);
		#endif

		mLength = frame->GetRowBytes() * mFormat.cy;
	}

	~video_frame()
	{
		auto ct = mBuffer->Release();
		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] VideoFrame Access (del) {} {}", mLogData.prefix, mFrameIndex, ct);
		#endif
	}

	HRESULT CopyData(IMediaSample* dst) const
	{
		BYTE* out;
		auto hr = dst->GetPointer(&out);
		if (FAILED(hr))
		{
			#ifndef NO_QUILL
			LOG_WARNING(mLogData.logger, "[{}] Unable to fill buffer , can't get pointer to output buffer [{:#08x}]",
			            mLogData.prefix, hr);
			#endif

			return S_FALSE;
		}

		mBuffer->StartAccess(bmdBufferAccessRead);

		void* data;
		mBuffer->GetBytes(&data);
		memcpy(out, data, mLength);

		mBuffer->EndAccess(bmdBufferAccessRead);

		return S_OK;
	}

	void Start(void** data) const
	{
		mBuffer->StartAccess(bmdBufferAccessRead);
		mBuffer->GetBytes(data);
	}

	void End() const
	{
		mBuffer->EndAccess(bmdBufferAccessRead);
	}

	uint64_t GetFrameIndex() const { return mFrameIndex; }

	int64_t GetCaptureTime() const { return mCaptureTime; }

	int64_t GetFrameTime() const { return mFrameTime; }

	int64_t GetFrameDuration() const { return mFrameDuration; }

	video_format GetVideoFormat() const { return mFormat; }

	int GetWidth() const { return mFormat.cx; }

	int GetHeight() const { return mFormat.cy; }

	long GetLength() const { return mLength; }

	IDeckLinkVideoFrame* GetRawFrame() const { return mFrame; }

private:
	video_format mFormat{};
	int64_t mCaptureTime{0};
	int64_t mFrameTime{0};
	int64_t mFrameDuration{0};
	uint64_t mFrameIndex{0};
	long mLength{0};
	IDeckLinkVideoFrame* mFrame = nullptr;
	IDeckLinkVideoBuffer* mBuffer = nullptr;
	log_data mLogData;
};

#endif
