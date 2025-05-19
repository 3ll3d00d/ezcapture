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
	int64_t pcieLinkWidth;
	int64_t pcieLinkSpeed;
	int64_t temperature;
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
	bool locked{false};
};

struct AUDIO_SIGNAL
{
	uint8_t channelCount{2};
	uint8_t bitDepth{16};
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

inline const char* to_string(BMDDisplayMode e)
{
	switch (e)
	{
	case bmdModeNTSC: return "NTSC";
	case bmdModeNTSC2398: return "NTSC2398";
	case bmdModePAL: return "PAL";
	case bmdModeNTSCp: return "NTSCp";
	case bmdModePALp: return "PALp";
	case bmdModeHD1080p2398: return "HD1080p2398";
	case bmdModeHD1080p24: return "HD1080p24";
	case bmdModeHD1080p25: return "HD1080p25";
	case bmdModeHD1080p2997: return "HD1080p2997";
	case bmdModeHD1080p30: return "HD1080p30";
	case bmdModeHD1080p4795: return "HD1080p4795";
	case bmdModeHD1080p48: return "HD1080p48";
	case bmdModeHD1080p50: return "HD1080p50";
	case bmdModeHD1080p5994: return "HD1080p5994";
	case bmdModeHD1080p6000: return "HD1080p6000";
	case bmdModeHD1080p9590: return "HD1080p9590";
	case bmdModeHD1080p96: return "HD1080p96";
	case bmdModeHD1080p100: return "HD1080p100";
	case bmdModeHD1080p11988: return "HD1080p11988";
	case bmdModeHD1080p120: return "HD1080p120";
	case bmdModeHD1080i50: return "HD1080i50";
	case bmdModeHD1080i5994: return "HD1080i5994";
	case bmdModeHD1080i6000: return "HD1080i6000";
	case bmdModeHD720p50: return "HD720p50";
	case bmdModeHD720p5994: return "HD720p5994";
	case bmdModeHD720p60: return "HD720p60";
	case bmdMode2k2398: return "2k2398";
	case bmdMode2k24: return "2k24";
	case bmdMode2k25: return "2k25";
	case bmdMode2kDCI2398: return "2kDCI2398";
	case bmdMode2kDCI24: return "2kDCI24";
	case bmdMode2kDCI25: return "2kDCI25";
	case bmdMode2kDCI2997: return "2kDCI2997";
	case bmdMode2kDCI30: return "2kDCI30";
	case bmdMode2kDCI4795: return "2kDCI4795";
	case bmdMode2kDCI48: return "2kDCI48";
	case bmdMode2kDCI50: return "2kDCI50";
	case bmdMode2kDCI5994: return "2kDCI5994";
	case bmdMode2kDCI60: return "2kDCI60";
	case bmdMode2kDCI9590: return "2kDCI9590";
	case bmdMode2kDCI96: return "2kDCI96";
	case bmdMode2kDCI100: return "2kDCI100";
	case bmdMode2kDCI11988: return "2kDCI11988";
	case bmdMode2kDCI120: return "2kDCI120";
	case bmdMode4K2160p2398: return "4K2160p2398";
	case bmdMode4K2160p24: return "4K2160p24";
	case bmdMode4K2160p25: return "4K2160p25";
	case bmdMode4K2160p2997: return "4K2160p2997";
	case bmdMode4K2160p30: return "4K2160p30";
	case bmdMode4K2160p4795: return "4K2160p4795";
	case bmdMode4K2160p48: return "4K2160p48";
	case bmdMode4K2160p50: return "4K2160p50";
	case bmdMode4K2160p5994: return "4K2160p5994";
	case bmdMode4K2160p60: return "4K2160p60";
	case bmdMode4K2160p9590: return "4K2160p9590";
	case bmdMode4K2160p96: return "4K2160p96";
	case bmdMode4K2160p100: return "4K2160p100";
	case bmdMode4K2160p11988: return "4K2160p11988";
	case bmdMode4K2160p120: return "4K2160p120";
	case bmdMode4kDCI2398: return "4kDCI2398";
	case bmdMode4kDCI24: return "4kDCI24";
	case bmdMode4kDCI25: return "4kDCI25";
	case bmdMode4kDCI2997: return "4kDCI2997";
	case bmdMode4kDCI30: return "4kDCI30";
	case bmdMode4kDCI4795: return "4kDCI4795";
	case bmdMode4kDCI48: return "4kDCI48";
	case bmdMode4kDCI50: return "4kDCI50";
	case bmdMode4kDCI5994: return "4kDCI5994";
	case bmdMode4kDCI60: return "4kDCI60";
	case bmdMode4kDCI9590: return "4kDCI9590";
	case bmdMode4kDCI96: return "4kDCI96";
	case bmdMode4kDCI100: return "4kDCI100";
	case bmdMode4kDCI11988: return "4kDCI11988";
	case bmdMode4kDCI120: return "4kDCI120";
	case bmdMode8K4320p2398: return "8K4320p2398";
	case bmdMode8K4320p24: return "8K4320p24";
	case bmdMode8K4320p25: return "8K4320p25";
	case bmdMode8K4320p2997: return "8K4320p2997";
	case bmdMode8K4320p30: return "8K4320p30";
	case bmdMode8K4320p4795: return "8K4320p4795";
	case bmdMode8K4320p48: return "8K4320p48";
	case bmdMode8K4320p50: return "8K4320p50";
	case bmdMode8K4320p5994: return "8K4320p5994";
	case bmdMode8K4320p60: return "8K4320p60";
	case bmdMode8kDCI2398: return "8kDCI2398";
	case bmdMode8kDCI24: return "8kDCI24";
	case bmdMode8kDCI25: return "8kDCI25";
	case bmdMode8kDCI2997: return "8kDCI2997";
	case bmdMode8kDCI30: return "8kDCI30";
	case bmdMode8kDCI4795: return "8kDCI4795";
	case bmdMode8kDCI48: return "8kDCI48";
	case bmdMode8kDCI50: return "8kDCI50";
	case bmdMode8kDCI5994: return "8kDCI5994";
	case bmdMode8kDCI60: return "8kDCI60";
	case bmdMode640x480p60: return "640x480p60";
	case bmdMode800x600p60: return "800x600p60";
	case bmdMode1440x900p50: return "1440x900p50";
	case bmdMode1440x900p60: return "1440x900p60";
	case bmdMode1440x1080p50: return "1440x1080p50";
	case bmdMode1440x1080p60: return "1440x1080p60";
	case bmdMode1600x1200p50: return "1600x1200p50";
	case bmdMode1600x1200p60: return "1600x1200p60";
	case bmdMode1920x1200p50: return "1920x1200p50";
	case bmdMode1920x1200p60: return "1920x1200p60";
	case bmdMode1920x1440p50: return "1920x1440p50";
	case bmdMode1920x1440p60: return "1920x1440p60";
	case bmdMode2560x1440p50: return "2560x1440p50";
	case bmdMode2560x1440p60: return "2560x1440p60";
	case bmdMode2560x1600p50: return "2560x1600p50";
	case bmdMode2560x1600p60: return "2560x1600p60";
	case bmdModeUnknown: return "Unknown";
	default: return "unknown";
	}
}

inline const char* to_string(BMDColorspace e)
{
	switch (e)
	{
	case bmdColorspaceRec601: return "Rec601";
	case bmdColorspaceRec709: return "Rec709";
	case bmdColorspaceRec2020: return "Rec2020";
	case bmdColorspaceDolbyVisionNative: return "DolbyVisionNative";
	case bmdColorspaceP3D65: return "P3D65";
	case bmdColorspaceUnknown: return "Unknown";
	default: return "unknown";
	}
}

inline const char* to_string(BMDPixelFormat e)
{
	switch (e)
	{
	case bmdFormatUnspecified: return "Unspecified";
	case bmdFormat8BitYUV: return "8BitYUV";
	case bmdFormat10BitYUV: return "10BitYUV";
	case bmdFormat10BitYUVA: return "10BitYUVA";
	case bmdFormat8BitARGB: return "8BitARGB";
	case bmdFormat8BitBGRA: return "8BitBGRA";
	case bmdFormat10BitRGB: return "10BitRGB";
	case bmdFormat12BitRGB: return "12BitRGB";
	case bmdFormat12BitRGBLE: return "12BitRGBLE";
	case bmdFormat10BitRGBXLE: return "10BitRGBXLE";
	case bmdFormat10BitRGBX: return "10BitRGBX";
	case bmdFormatH265: return "H265";
	case bmdFormatDNxHR: return "DNxHR";
	default: return "unknown";
	}
}
