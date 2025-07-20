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
#ifndef MW_AUDIO_CAPTURE_PIN_HEADER
#define MW_AUDIO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "mw_capture_filter.h"
#include "audio_capture_pin.h"

/**
 * An audio stream flowing from the capture device to an output pin.
 */
class magewell_audio_capture_pin final :
	public hdmi_audio_capture_pin<magewell_capture_filter>
{
public:
	magewell_audio_capture_pin(HRESULT* phr, magewell_capture_filter* pParent, bool pPreview);
	~magewell_audio_capture_pin() override;

	void CopyToBitstreamBuffer(BYTE* buf);
	HRESULT ParseBitstreamBuffer(uint16_t bufSize, enum codec** codec);
	HRESULT GetCodecFromIEC61937Preamble(enum IEC61937DataType dataType, uint16_t* burstSize, enum codec* codec);

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

	//////////////////////////////////////////////////////////////////////////
	//  IAmTimeAware
	//////////////////////////////////////////////////////////////////////////
	void SetStopTime(LONGLONG streamStopTime) override
	{
		mStreamStartTime = -1LL;
		mStreamStopTime = -1LL;

		#ifndef NO_QUILL
		LOG_WARNING(mLogData.logger, "[{}] MagewellAudioCapturePin::SetStopTime at {}", mLogData.prefix,
		            streamStopTime);
		#endif
	}

protected:
	class audio_capture
	{
	public:
		audio_capture(magewell_audio_capture_pin* pin, HCHANNEL hChannel);
		~audio_capture();

	private:
		log_data mLogData;
		magewell_audio_capture_pin* pin;
		HANDLE mEvent;
	};

	int GetSamplesPerFrame() const
	{
		if (mFilter->GetDeviceType() == MW_PRO)
		{
			return MWCAP_AUDIO_SAMPLES_PER_FRAME;
		}
		return usbAudioSamplesPerFrame;
	}

	// Common - temp 
	HNOTIFY mNotify;
	ULONGLONG mStatusBits = 0;
	HANDLE mNotifyEvent;

	// pro only
	HANDLE mCaptureEvent;

	double minus_10db{pow(10.0, -10.0 / 20.0)};
	audio_signal mAudioSignal{};
	BYTE mFrameBuffer[maxFrameLengthInBytes];
	// usb only
	uint64_t mFrameBufferLen{0}; // reportedly always be 480 samples (so 1920 bytes)
	// IEC61937 processing
	uint32_t mBitstreamDetectionWindowLength{std::numeric_limits<uint32_t>::max()};
	uint8_t mPaPbBytesRead{0};
	BYTE mPcPdBuffer[4];
	uint8_t mPcPdBytesRead{0};
	uint16_t mDataBurstFrameCount{0};
	uint16_t mDataBurstRead{0};
	uint16_t mDataBurstSize{0};
	uint16_t mDataBurstPayloadSize{0};
	uint32_t mBytesSincePaPb{0};
	double mCompressedAudioRefreshRate{0.0};
	uint64_t mSinceCodecChange{0};
	bool mPacketMayBeCorrupt{false};
	BYTE mCompressedBuffer[maxFrameLengthInBytes];
	std::vector<BYTE> mDataBurstBuffer; // variable size
	std::unique_ptr<audio_capture> mAudioCapture;
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
	codec mDetectedCodec{PCM};
	bool mProbeOnTimer{false};

	static void CaptureFrame(const BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void LoadFormat(audio_format* audioFormat, const audio_signal* audioSignal) const;
	HRESULT LoadSignal(HCHANNEL* hChannel);
	HRESULT DoChangeMediaType(const CMediaType* pmt, const audio_format* newAudioFormat);
	void StopCapture();
	bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
	void DoThreadDestroy() override;
};
#endif
