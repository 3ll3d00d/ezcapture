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

#ifdef NO_QUILL
#include <chrono>
#endif
#include "capture.h"
#include "mwdomain.h"
#include "VideoFrameWriter.h"
#include "LibMWCapture/MWCapture.h"

class MWReferenceClock final :
	public CBaseReferenceClock
{
	HCHANNEL mChannel;
	bool mIsPro;

public:
	MWReferenceClock(HRESULT* phr, HCHANNEL hChannel, bool isProDevice)
		: CBaseReferenceClock(L"MWReferenceClock", nullptr, phr, nullptr),
		  mChannel(hChannel),
		  mIsPro(isProDevice)
	{
	}

	REFERENCE_TIME GetPrivateTime() override
	{
		REFERENCE_TIME t;
		if (mIsPro)
		{
			MWGetDeviceTime(mChannel, &t);
		}
		else
		{
			t = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		}
		return t;
	}
};

/**
 * Directshow filter which can uses the MWCapture SDK to receive video and audio from a HDMI capture card.
 * Can inject HDR/WCG data if found on the incoming HDMI stream.
 */
class MagewellCaptureFilter final :
	public HdmiCaptureFilter<DEVICE_INFO, VIDEO_SIGNAL, AUDIO_SIGNAL>
{
public:
	// Provide the way for COM to create a Filter object
	static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT* phr);

	HCHANNEL GetChannelHandle() const;

	DeviceType GetDeviceType() const;

	void SnapHardwareDetails();

	// Callbacks to update the prop page data
	void OnVideoSignalLoaded(VIDEO_SIGNAL* vs) override;
	void OnAudioSignalLoaded(AUDIO_SIGNAL* as) override;
	void OnDeviceUpdated() override;

private:
	// Constructor
	MagewellCaptureFilter(LPUNKNOWN punk, HRESULT* phr);
	~MagewellCaptureFilter() override;

	BOOL mInited;
};

/**
 * A video stream flowing from the capture device to an output pin.
 */
class MagewellVideoCapturePin final :
	public HdmiVideoCapturePin<MagewellCaptureFilter, VideoSampleBuffer>
{
public:
	MagewellVideoCapturePin(HRESULT* phr, MagewellCaptureFilter* pParent, bool pPreview);
	~MagewellVideoCapturePin() override;

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

	void SnapCaptureTime()
	{
		GetReferenceTime(&mCaptureTime);
		mFrameCounter++;
	}

protected:
	void DoThreadDestroy() override;
	void StopCapture();

	void LoadFormat(VIDEO_FORMAT* videoFormat, VIDEO_SIGNAL* videoSignal, const USB_CAPTURE_FORMATS* captureFormats);
	// USB only
	static void CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void LogHdrMetaIfPresent(const VIDEO_FORMAT* newVideoFormat) override;
	void OnChangeMediaType() override;
	HRESULT LoadSignal(HCHANNEL* pChannel);
	void OnFrameWriterStrategyUpdated() override;

	void SnapTemperatureIfNecessary(LONGLONG endTime)
	{
		if (endTime > mLastTempSnapAt + dshowTicksPerSecond)
		{
			mFilter->SnapHardwareDetails();
			mFilter->OnDeviceUpdated();
			mLastTempSnapAt = endTime;
		}
	}

	// Encapsulates pinning the IMediaSample buffer into video memory (and unpinning on destruct)
	class VideoFrameGrabber
	{
	public:
		VideoFrameGrabber(MagewellVideoCapturePin* pin, HCHANNEL hChannel, DeviceType deviceType, IMediaSample* pms);
		~VideoFrameGrabber();

		VideoFrameGrabber(VideoFrameGrabber const&) = delete;
		VideoFrameGrabber& operator =(VideoFrameGrabber const&) = delete;
		VideoFrameGrabber(VideoFrameGrabber&&) = delete;
		VideoFrameGrabber& operator=(VideoFrameGrabber&&) = delete;

		HRESULT grab() const;

	private:
		log_data mLogData;
		HCHANNEL hChannel;
		DeviceType deviceType;
		MagewellVideoCapturePin* pin;
		IMediaSample* pms;
		BYTE* pmsData;
	};

	// USB only
	class VideoCapture
	{
	public:
		VideoCapture(MagewellVideoCapturePin* pin, HCHANNEL hChannel);
		~VideoCapture();

	private:
		MagewellVideoCapturePin* pin;
		log_data mLogData;
		HANDLE mEvent;
	};

	// Common - temp 
	HNOTIFY mNotify;
	uint64_t mStatusBits = 0;
	HANDLE mNotifyEvent;
	MW_RESULT mLastMwResult;
	int64_t mCaptureTime;
	int64_t mLastTempSnapAt{0};
	captured_frame mCapturedFrame{};

	// pro only
	HANDLE mCaptureEvent;

	VIDEO_SIGNAL mVideoSignal{};
	USB_CAPTURE_FORMATS mUsbCaptureFormats{};
	bool mHasHdrInfoFrame{false};
	// USB only
	std::unique_ptr<VideoCapture> mVideoCapture;
};

/**
 * An audio stream flowing from the capture device to an output pin.
 */
class MagewellAudioCapturePin final :
	public HdmiAudioCapturePin<MagewellCaptureFilter>
{
public:
	MagewellAudioCapturePin(HRESULT* phr, MagewellCaptureFilter* pParent, bool pPreview);
	~MagewellAudioCapturePin() override;

	void CopyToBitstreamBuffer(BYTE* buf);
	HRESULT ParseBitstreamBuffer(uint16_t bufSize, enum Codec** codec);
	HRESULT GetCodecFromIEC61937Preamble(enum IEC61937DataType dataType, uint16_t* burstSize, enum Codec* codec);

	//////////////////////////////////////////////////////////////////////////
	//  CBaseOutputPin
	//////////////////////////////////////////////////////////////////////////
	HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime,
	                          __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

	//////////////////////////////////////////////////////////////////////////
	//  CSourceStream
	//////////////////////////////////////////////////////////////////////////
	HRESULT OnThreadCreate(void) override;
	HRESULT FillBuffer(IMediaSample* pms) override;

protected:
	class AudioCapture
	{
	public:
		AudioCapture(MagewellAudioCapturePin* pin, HCHANNEL hChannel);
		~AudioCapture();

	private:
		log_data mLogData;
		MagewellAudioCapturePin* pin;
		HANDLE mEvent;
	};

	// Common - temp 
	HNOTIFY mNotify;
	ULONGLONG mStatusBits = 0;
	HANDLE mNotifyEvent;
	MW_RESULT mLastMwResult;
	// pro only
	HANDLE mCaptureEvent;

	double minus_10db{pow(10.0, -10.0 / 20.0)};
	AUDIO_SIGNAL mAudioSignal{};
	BYTE mFrameBuffer[maxFrameLengthInBytes];
	// IEC61937 processing
	uint32_t mBitstreamDetectionWindowLength{0};
	uint8_t mPaPbBytesRead{0};
	BYTE mPcPdBuffer[4];
	uint8_t mPcPdBytesRead{0};
	uint16_t mDataBurstFrameCount{0};
	uint16_t mDataBurstRead{0};
	uint16_t mDataBurstSize{0};
	uint16_t mDataBurstPayloadSize{0};
	uint32_t mBytesSincePaPb{0};
	uint64_t mSinceCodecChange{0};
	bool mPacketMayBeCorrupt{false};
	BYTE mCompressedBuffer[maxFrameLengthInBytes];
	std::vector<BYTE> mDataBurstBuffer; // variable size
	std::unique_ptr<AudioCapture> mAudioCapture;
	captured_frame mCapturedFrame{};

	#ifdef RECORD_RAW
    char mRawFileName[MAX_PATH];
    FILE* mRawFile;
	#endif
	#ifdef RECORD_ENCODED
    char mEncodedInFileName[MAX_PATH];
    FILE* mEncodedInFile;
    char mEncodedOutFileName[MAX_PATH];
    FILE* mEncodedOutFile;
	#endif
	// TODO remove after SDK bug is fixed
	Codec mDetectedCodec{PCM};
	bool mProbeOnTimer{false};

	static void CaptureFrame(const BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal) const;
	HRESULT LoadSignal(HCHANNEL* hChannel);
	HRESULT DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat);
	void StopCapture();
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
	void DoThreadDestroy() override;
};
