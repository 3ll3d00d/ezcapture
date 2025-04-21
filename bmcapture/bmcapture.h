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

#include <functional>
#include <utility>
#include <atlcomcli.h>

#include "capture.h"
#include "DeckLinkAPI_h.h"
#include "VideoFrameWriter.h"

#ifdef NO_QUILL
#include <memory>
#endif

EXTERN_C const GUID CLSID_BMCAPTURE_FILTER;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN24;
EXTERN_C const GUID MEDIASUBTYPE_PCM_IN32;
EXTERN_C const GUID MEDIASUBTYPE_PCM_SOWT;
EXTERN_C const AMOVIESETUP_PIN sMIPPins[];

const std::function DeleteString = SysFreeString;

const std::function BSTRToStdString = [](BSTR dl_str) -> std::string
{
	int wlen = ::SysStringLen(dl_str);
	int mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, nullptr, 0, nullptr, nullptr);

	std::string ret_str(mblen, '\0');
	mblen = WideCharToMultiByte(CP_ACP, 0, dl_str, wlen, &ret_str[0], mblen, nullptr, nullptr);

	return ret_str;
};

const std::function StdStringToBSTR = [](std::string std_str) -> BSTR
{
	int wlen = MultiByteToWideChar(CP_ACP, 0, std_str.data(), static_cast<int>(std_str.length()), nullptr, 0);

	BSTR ret_str = ::SysAllocStringLen(nullptr, wlen);
	MultiByteToWideChar(CP_ACP, 0, std_str.data(), static_cast<int>(std_str.length()), ret_str, wlen);

	return ret_str;
};

inline bool isInCieRange(double value)
{
	return value >= 0 && value <= 1.1;
}

constexpr int64_t invalidFrameTime = std::numeric_limits<int64_t>::lowest();
constexpr BMDAudioSampleType audioBitDepth = bmdAudioSampleType16bitInteger;

enum FrameConversionStrategy :uint8_t
{
	ANY_RGB,
	YUV2_YV16,
	V210_P210
};

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
	std::string colourFormat{"YUV"};
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
	           uint64_t frameIndex, IDeckLinkAudioInputPacket* packet):
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
		mLogData(std::move(logData))
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

	void CopyData(BYTE* dst) const
	{
		mBuffer->StartAccess(bmdBufferAccessRead);

		void* data;
		mBuffer->GetBytes(&data);
		memcpy(dst, data, mLength);

		mBuffer->EndAccess(bmdBufferAccessRead);
	}

	void WithData(void** dst) const
	{
		mBuffer->StartAccess(bmdBufferAccessRead);
		mBuffer->GetBytes(dst);
	}

	uint64_t GetFrameIndex() const { return mFrameIndex; }

	int64_t GetCaptureTime() const { return mCaptureTime; }

	int64_t GetFrameTime() const { return mFrameTime; }

	int64_t GetFrameDuration() const { return mFrameDuration; }

	VIDEO_FORMAT GetVideoFormat() const { return mFormat; }

	long GetLength() const { return mLength; }

private:
	VIDEO_FORMAT mFormat{};
	int64_t mCaptureTime{0};
	int64_t mFrameTime{0};
	int64_t mFrameDuration{0};
	uint64_t mFrameIndex{0};
	long mLength{0};
	IDeckLinkVideoBuffer* mBuffer = nullptr;
	log_data mLogData;
};

class BMReferenceClock final :
	public CBaseReferenceClock
{
public:
	BMReferenceClock(HRESULT* phr, const CComQIPtr<IDeckLinkInput>& pInput)
		: CBaseReferenceClock(L"BMReferenceClock", nullptr, phr, nullptr),
		  mInput(pInput)
	{
	}

	REFERENCE_TIME GetPrivateTime() override
	{
		BMDTimeValue tv, tif, tpf;
		mInput->GetHardwareReferenceClock(dshowTicksPerSecond, &tv, &tif, &tpf);
		return tv;
		// return std::chrono::duration_cast<std::chrono::microseconds>(
		// std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	}

private:
	CComQIPtr<IDeckLinkInput> mInput;
};

/**
 * DirectShow filter which can uses the Blackmagic SDK to receive video and audio from a connected HDMI capture card.
 * Can inject HDR/WCG data if found on the incoming HDMI stream.
 */
class BlackmagicCaptureFilter final :
	public HdmiCaptureFilter<DEVICE_INFO, VIDEO_SIGNAL, AUDIO_SIGNAL>,
	public IDeckLinkInputCallback,
	public IDeckLinkNotificationCallback
{
public:
	// Provide the way for COM to create a Filter object
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

	// Callbacks to update the prop page data
	void OnVideoSignalLoaded(VIDEO_SIGNAL* vs) override;
	void OnAudioSignalLoaded(AUDIO_SIGNAL* as) override;
	void OnDeviceSelected() override;

	HRESULT Reload() override;

	// filter <-> pin communication
	HRESULT PinThreadCreated();
	HRESULT PinThreadDestroyed();

	//////////////////////////////////////////////////////////////////////////
	//  IDeckLinkInputCallback
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents,
	                                                  IDeckLinkDisplayMode* newDisplayMode,
	                                                  BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;

	HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame,
	                                                 IDeckLinkAudioInputPacket* audioPacket) override;

	//////////////////////////////////////////////////////////////////////////
	//  IDeckLinkNotificationCallback
	//////////////////////////////////////////////////////////////////////////
	HRESULT STDMETHODCALLTYPE Notify(BMDNotifications topic, ULONGLONG param1, ULONGLONG param2);

	//////////////////////////////////////////////////////////////////////////
	//  IUnknown
	//////////////////////////////////////////////////////////////////////////
	HRESULT QueryInterface(const IID& riid, void** ppvObject) override;

	ULONG AddRef() override
	{
		return CaptureFilter::AddRef();
	}

	ULONG Release() override
	{
		return CaptureFilter::Release();
	}

	HANDLE GetVideoFrameHandle() const
	{
		return mVideoFrameEvent;
	}

	std::shared_ptr<VideoFrame> GetVideoFrame()
	{
		return mVideoFrame;
	}

	HANDLE GetAudioFrameHandle() const
	{
		return mAudioFrameEvent;
	}

	std::shared_ptr<AudioFrame> GetAudioFrame()
	{
		return mAudioFrame;
	}

protected:
	static void LoadFormat(VIDEO_FORMAT* videoFormat, const VIDEO_SIGNAL* videoSignal);
	static void LoadSignalFromDisplayMode(VIDEO_SIGNAL* newSignal, IDeckLinkDisplayMode* newDisplayMode);

private:
	// Constructor
	BlackmagicCaptureFilter(LPUNKNOWN punk, HRESULT* phr);
	~BlackmagicCaptureFilter() override;

	CComPtr<IDeckLink> mDeckLink;
	CComQIPtr<IDeckLinkInput> mDeckLinkInput;
	CComQIPtr<IDeckLinkNotification> mDeckLinkNotification;
	CComQIPtr<IDeckLinkStatus> mDeckLinkStatus;
	CComQIPtr<IDeckLinkHDMIInputEDID> mDeckLinkHDMIInputEDID;
	CComPtr<IDeckLinkVideoConversion> mDeckLinkFrameConverter;
	CCritSec mFrameSec;
	CCritSec mDeckLinkSec;

	uint8_t mRunningPins{0};
	VIDEO_SIGNAL mVideoSignal{};
	VIDEO_FORMAT mVideoFormat{};

	int64_t mPreviousVideoFrameTime{invalidFrameTime};
	uint64_t mCurrentVideoFrameIndex{0};
	std::shared_ptr<VideoFrame> mVideoFrame;
	HANDLE mVideoFrameEvent;

	int64_t mPreviousAudioFrameTime{invalidFrameTime};
	uint64_t mCurrentAudioFrameIndex{0};
	std::shared_ptr<AudioFrame> mAudioFrame;
	HANDLE mAudioFrameEvent;
};


/**
 * A video stream flowing from the capture device to an output pin.
 */
class BlackmagicVideoCapturePin final :
	public HdmiVideoCapturePin<BlackmagicCaptureFilter>
{
public:
	BlackmagicVideoCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview, VIDEO_FORMAT pVideoFormat);
	~BlackmagicVideoCapturePin() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
	                          __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT FillBuffer(IMediaSample* pms) override;
	HRESULT OnThreadCreate(void) override;

protected:
	void DoThreadDestroy() override;

	void LogHdrMetaIfPresent(VIDEO_FORMAT* newVideoFormat);
	void OnChangeMediaType() override;

	std::shared_ptr<VideoFrame> mCurrentFrame;

private:
	FrameConversionStrategy mFrameConversionStrategy;
	IVideoFrameWriter* mFrameWriter;
};

/**
 * An audio stream flowing from the capture device to an output pin.
 */
class BlackmagicAudioCapturePin final :
	public HdmiAudioCapturePin<BlackmagicCaptureFilter>
{
public:
	BlackmagicAudioCapturePin(HRESULT* phr, BlackmagicCaptureFilter* pParent, bool pPreview);
	~BlackmagicAudioCapturePin() override;

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
	                          __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT OnThreadCreate() override;
	HRESULT FillBuffer(IMediaSample* pms) override;

protected:
	HRESULT DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat);
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
	void DoThreadDestroy() override;

	std::shared_ptr<AudioFrame> mCurrentFrame;
};
