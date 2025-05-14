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
#include "util.h"
#include "LibMWCapture/MWCapture.h"

// HDMI Audio Bitstream Codec Identification metadata

// IEC 61937-1 Chapter 6.1.7 Field Pa
constexpr auto IEC61937_SYNCWORD_1 = 0xF872;
// IEC 61937-1 Chapter 6.1.7 Field Pb
constexpr auto IEC61937_SYNCWORD_2 = 0x4E1F;
// IEC 61937-2 Table 2
enum IEC61937DataType : uint8_t
{
    IEC61937_NULL               = 0x0,           ///< NULL
    IEC61937_AC3                = 0x01,          ///< AC-3 data
    IEC61937_PAUSE              = 0x03,          ///< Pause
    IEC61937_MPEG1_LAYER1       = 0x04,          ///< MPEG-1 layer 1
    IEC61937_MPEG1_LAYER23      = 0x05,          ///< MPEG-1 layer 2 or 3 data or MPEG-2 without extension
    IEC61937_MPEG2_EXT          = 0x06,          ///< MPEG-2 data with extension
    IEC61937_MPEG2_AAC          = 0x07,          ///< MPEG-2 AAC ADTS
    IEC61937_MPEG2_LAYER1_LSF   = 0x08,          ///< MPEG-2, layer-1 low sampling frequency
    IEC61937_MPEG2_LAYER2_LSF   = 0x09,          ///< MPEG-2, layer-2 low sampling frequency
    IEC61937_MPEG2_LAYER3_LSF   = 0x0A,          ///< MPEG-2, layer-3 low sampling frequency
    IEC61937_DTS1               = 0x0B,          ///< DTS type I   (512 samples)
    IEC61937_DTS2               = 0x0C,          ///< DTS type II  (1024 samples)
    IEC61937_DTS3               = 0x0D,          ///< DTS type III (2048 samples)
    IEC61937_ATRAC              = 0x0E,          ///< ATRAC data
    IEC61937_ATRAC3             = 0x0F,          ///< ATRAC3 data
    IEC61937_ATRACX             = 0x10,          ///< ATRAC3+ data
    IEC61937_DTSHD              = 0x11,          ///< DTS HD data
    IEC61937_WMAPRO             = 0x12,          ///< WMA 9 Professional data
    IEC61937_MPEG2_AAC_LSF_2048 = 0x13,          ///< MPEG-2 AAC ADTS half-rate low sampling frequency
    IEC61937_MPEG2_AAC_LSF_4096 = 0x13 | 0x20,   ///< MPEG-2 AAC ADTS quarter-rate low sampling frequency
    IEC61937_EAC3               = 0x15,          ///< E-AC-3 data
    IEC61937_TRUEHD             = 0x16,          ///< TrueHD/MAT data
};

constexpr int maxBitDepthInBytes = sizeof(DWORD);
constexpr int maxFrameLengthInBytes = MWCAP_AUDIO_SAMPLES_PER_FRAME * MWCAP_AUDIO_MAX_NUM_CHANNELS * maxBitDepthInBytes;

EXTERN_C const GUID CLSID_MWCAPTURE_FILTER;

struct USB_CAPTURE_FORMATS
{
    bool usb{ false };
    MWCAP_VIDEO_OUTPUT_FOURCC fourccs;
    MWCAP_VIDEO_OUTPUT_FRAME_INTERVAL frameIntervals;
    MWCAP_VIDEO_OUTPUT_FRAME_SIZE frameSizes;
};

struct VIDEO_SIGNAL
{
    MWCAP_INPUT_SPECIFIC_STATUS inputStatus;
    MWCAP_VIDEO_SIGNAL_STATUS signalStatus;
    MWCAP_VIDEO_BUFFER_INFO bufferInfo;
    MWCAP_VIDEO_FRAME_INFO frameInfo;
    MWCAP_VIDEO_CAPTURE_STATUS captureStatus;
    HDMI_HDR_INFOFRAME_PAYLOAD hdrInfo;
    HDMI_AVI_INFOFRAME_PAYLOAD aviInfo;
};

struct AUDIO_SIGNAL
{
    MWCAP_AUDIO_SIGNAL_STATUS signalStatus;
    MWCAP_AUDIO_CAPTURE_FRAME frameInfo;
    HDMI_AUDIO_INFOFRAME_PAYLOAD audioInfo;
};

enum DeviceType : uint8_t
{
	USB,
    PRO
};

inline const char* devicetype_to_name(DeviceType e)
{
    switch (e)
    {
    case USB: return "USB";
    case PRO: return "PRO";
    default: return "unknown";
    }
}

struct DEVICE_INFO
{
    DeviceType deviceType;
    std::string serialNo{};
    WCHAR devicePath[128];
    HCHANNEL hChannel;
};

struct CAPTURED_FRAME
{
    BYTE* data;
    int length;
    UINT64 ts;
};

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
            t = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
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

    // Callbacks to update the prop page data
    void OnVideoSignalLoaded(VIDEO_SIGNAL* vs) override;
    void OnAudioSignalLoaded(AUDIO_SIGNAL* as) override;
    void OnDeviceSelected() override;

    HRESULT Reload() override;

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
	public HdmiVideoCapturePin<MagewellCaptureFilter>
{
public:
    MagewellVideoCapturePin(HRESULT* phr, MagewellCaptureFilter* pParent, bool pPreview);
    ~MagewellVideoCapturePin() override;

    //////////////////////////////////////////////////////////////////////////
    //  CBaseOutputPin
    //////////////////////////////////////////////////////////////////////////
    HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime, __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

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

	void LoadFormat(VIDEO_FORMAT* videoFormat, VIDEO_SIGNAL* videoSignal, USB_CAPTURE_FORMATS* captureFormats);
    // USB only
    static void CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void LogHdrMetaIfPresent(VIDEO_FORMAT* newVideoFormat);
    void OnChangeMediaType() override;
    HRESULT LoadSignal(HCHANNEL* pChannel);

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
    ULONGLONG mStatusBits = 0;
    HANDLE mNotifyEvent;
    MW_RESULT mLastMwResult;
    int64_t mCaptureTime;
    // pro only
    HANDLE mCaptureEvent;

    VIDEO_SIGNAL mVideoSignal{};
    USB_CAPTURE_FORMATS mUsbCaptureFormats{};
    bool mHasHdrInfoFrame{ false };
    // USB only
    VideoCapture* mVideoCapture{nullptr};
    CAPTURED_FRAME mCapturedFrame{};
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
    HRESULT GetDeliveryBuffer(__deref_out IMediaSample** ppSample, __in_opt REFERENCE_TIME* pStartTime, __in_opt REFERENCE_TIME* pEndTime, DWORD dwFlags) override;

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

    double minus_10db{ pow(10.0, -10.0 / 20.0) };
    AUDIO_SIGNAL mAudioSignal{};
    BYTE mFrameBuffer[maxFrameLengthInBytes];
    // IEC61937 processing
    uint32_t mBitstreamDetectionWindowLength{ 0 };
    uint8_t mPaPbBytesRead{ 0 };
    BYTE mPcPdBuffer[4]; 
    uint8_t mPcPdBytesRead{ 0 };
    uint16_t mDataBurstFrameCount{ 0 };
    uint16_t mDataBurstRead{ 0 };
    uint16_t mDataBurstSize{ 0 };
    uint16_t mDataBurstPayloadSize{ 0 };
    uint32_t mBytesSincePaPb{ 0 };
    uint64_t mSinceCodecChange{ 0 };
    bool mPacketMayBeCorrupt{ false };
    BYTE mCompressedBuffer[maxFrameLengthInBytes];
    std::vector<BYTE> mDataBurstBuffer; // variable size
    AudioCapture* mAudioCapture{ nullptr };
    CAPTURED_FRAME mCapturedFrame{};

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
    Codec mDetectedCodec{ PCM };
    bool mProbeOnTimer{ false };

    static void CaptureFrame(const BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void LoadFormat(AUDIO_FORMAT* audioFormat, const AUDIO_SIGNAL* audioSignal) const;
    HRESULT LoadSignal(HCHANNEL* hChannel);
    HRESULT DoChangeMediaType(const CMediaType* pmt, const AUDIO_FORMAT* newAudioFormat);
    void StopCapture();
    bool ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties) override;
    void DoThreadDestroy() override;
};
