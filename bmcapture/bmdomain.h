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
#include <string>

#include "DeckLinkAPI_h.h"
#include "domain.h"
#include "logging.h"


struct DEVICE_INFO
{
	std::string name{};
	int apiVersion[3]{0, 0, 0};
	uint8_t audioChannelCount{0};
	bool inputFormatDetection{false};
	bool hdrMetadata{false};
	bool colourspaceMetadata{false};
	bool dynamicRangeMetadata{false};
};

struct VIDEO_SIGNAL
{
	BMDPixelFormat pixelFormat{bmdFormat8BitARGB};
	BMDDisplayMode displayMode{bmdMode4K2160p2398};
	std::string colourFormat{"RGB"};
	std::string displayModeName{"4K2160p23.98"};
	uint8_t bitDepth{8};
	uint32_t frameDuration{1001};
	uint16_t frameDurationScale{24000};
	uint16_t cx{3840};
	uint16_t cy{2160};
	uint8_t aspectX{16};
	uint8_t aspectY{9};
};

struct AUDIO_SIGNAL
{
	uint8_t channelCount{0};
	uint8_t bitDepth{0};
};

class AudioFrame
{
public:
	AudioFrame(log_data logData, int64_t captureTime, int64_t frameTime, void* data, long len, AUDIO_FORMAT fmt,
	           uint64_t frameIndex, IDeckLinkAudioInputPacket* packet) :
		mCaptureTime(captureTime),
		mFrameTime(frameTime),
		mData(data),
		mLength(len),
		mFormat(std::move(fmt)),
		mLogData(std::move(logData)),
		mFrameIndex(frameIndex),
		mPacket(packet)
	{
		auto ct = packet->AddRef();

		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] AudioFrame Access (new) {} {}", mLogData.prefix, mFrameIndex, ct);
		#endif
	}

	~AudioFrame()
	{
		auto ct = mPacket->Release();

		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] AudioFrame Access (del) {} {}", mLogData.prefix, mFrameIndex, ct);
		#endif
	}

	AudioFrame& operator=(const AudioFrame& rhs)
	{
		if (this == &rhs)
			return *this;

		mPacket = rhs.mPacket;
		auto ct = mPacket->AddRef();

		#ifndef NO_QUILL
		LOG_TRACE_L3(mLogData.logger, "[{}] AudioFrame Access (assign) {} {}", mLogData.prefix, mFrameIndex, ct);
		#endif

		mLogData = rhs.mLogData;
		mCaptureTime = rhs.mCaptureTime;
		mFrameTime = rhs.mFrameTime;
		mData = rhs.mData;
		mLength = rhs.mLength;
		mFormat = rhs.mFormat;
		mFrameIndex = rhs.mFrameIndex;

		return *this;
	}

	int64_t GetCaptureTime() const { return mCaptureTime; }

	int64_t GetFrameTime() const { return mFrameTime; }

	void* GetData() const { return mData; }

	long GetLength() const { return mLength; }

	AUDIO_FORMAT GetFormat() const { return mFormat; }

private:
	int64_t mCaptureTime{0};
	int64_t mFrameTime{0};
	void* mData = nullptr;
	long mLength{0};
	AUDIO_FORMAT mFormat{};
	log_data mLogData;
	uint64_t mFrameIndex{0};
	IDeckLinkAudioInputPacket* mPacket;
};

class VideoFrame
{
public:
	VideoFrame(log_data logData, VIDEO_FORMAT format, int64_t captureTime, int64_t frameTime, int64_t duration,
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

	~VideoFrame()
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

	uint64_t GetFrameIndex() const { return mFrameIndex; }

	int64_t GetCaptureTime() const { return mCaptureTime; }

	int64_t GetFrameTime() const { return mFrameTime; }

	int64_t GetFrameDuration() const { return mFrameDuration; }

	VIDEO_FORMAT GetVideoFormat() const { return mFormat; }

	long GetLength() const { return mLength; }

	IDeckLinkVideoFrame* GetRawFrame() const { return mFrame; }

private:
	VIDEO_FORMAT mFormat{};
	int64_t mCaptureTime{0};
	int64_t mFrameTime{0};
	int64_t mFrameDuration{0};
	uint64_t mFrameIndex{0};
	long mLength{0};
	IDeckLinkVideoFrame* mFrame = nullptr;
	IDeckLinkVideoBuffer* mBuffer = nullptr;
	log_data mLogData;
};
