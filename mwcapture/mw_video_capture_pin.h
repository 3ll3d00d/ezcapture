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
#ifndef MW_VIDEO_CAPTURE_PIN_HEADER
#define MW_VIDEO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "mw_capture_filter.h"
#include "video_capture_pin.h"
#include "dv_tunnel_lav.h"
#include <memory>
#include <vector>

/**
 * A video stream flowing from the capture device to an output pin.
 */
class magewell_video_capture_pin final :
	public hdmi_video_capture_pin<magewell_capture_filter, video_sample_buffer>
{
public:
	magewell_video_capture_pin(HRESULT* phr, magewell_capture_filter* pParent, bool pPreview);
	~magewell_video_capture_pin() override;

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

	//////////////////////////////////////////////////////////////////////////
	//  IAmTimeAware
	//////////////////////////////////////////////////////////////////////////
	void SetStopTime(LONGLONG streamStopTime) override
	{
		mStreamStartTime = -1LL;
		mStreamStopTime = -1LL;

		#ifndef NO_LOGGING
		LOG_WARNING(mLogData.logger, "[{}] MagewellVideoCapturePin::SetStopTime at {}", mLogData.prefix,
		            streamStopTime);
		#endif
	}

protected:
	void DoThreadDestroy() override;
	void StopCapture();

	void LoadFormat(video_format* videoFormat, video_signal* videoSignal, const usb_capture_formats* captureFormats);
	// USB only
	static void CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void OnChangeMediaType() override;
	HRESULT LoadSignal(HCHANNEL* pChannel);
	void OnFrameWriterStrategyUpdated() override;

	// experimental Dolby Vision RGB tunnel decode, pro only
	bool IsDvTunnelCandidate() const;
	void ProbeDvTunnel(const uint8_t* frame, DWORD stride);
	void WriteDvTunnelFrame(IMediaSample* pms);
	void AppendDvSideData(IMediaSample* pms);
	void LogDvTunnelStats();
	void PublishDvStatus();
	void LogDvEdidSupport(HCHANNEL hChannel) const;

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
	class video_frame_grabber
	{
	public:
		video_frame_grabber(magewell_video_capture_pin* pin, HCHANNEL hChannel, device_type deviceType,
		                    IMediaSample* pms);
		~video_frame_grabber();

		video_frame_grabber(video_frame_grabber const&) = delete;
		video_frame_grabber& operator =(video_frame_grabber const&) = delete;
		video_frame_grabber(video_frame_grabber&&) = delete;
		video_frame_grabber& operator=(video_frame_grabber&&) = delete;

		HRESULT grab() const;

	private:
		log_data mLogData;
		HCHANNEL hChannel;
		device_type deviceType;
		magewell_video_capture_pin* pin;
		IMediaSample* pms;
		BYTE* pmsData;
	};

	// USB only
	class video_capture
	{
	public:
		video_capture(magewell_video_capture_pin* pin, HCHANNEL hChannel);
		~video_capture();

	private:
		magewell_video_capture_pin* pin;
		log_data mLogData;
		HANDLE mEvent;
	};

	pixel_format_by_bit_depth_subsampling mPixelFormatMatrix;
	// Common - temp 
	HNOTIFY mNotify;
	uint64_t mStatusBits = 0;
	HANDLE mNotifyEvent;
	int64_t mLastTempSnapAt{0};
	captured_frame mCapturedFrame{};

	// pro only
	HANDLE mCaptureEvent;

	video_signal mVideoSignal{};
	usb_capture_formats mUsbCaptureFormats{};
	bool mHasHdrInfoFrame{false};
	// dv tunnel
	dv_tunnel::decoder mDvDecoder{};
	bool mDvLocked{false};
	std::vector<uint8_t> mDvCapture{};
	DWORD mDvCaptureStride{0};
	int mDvOutputWidth{0};
	bool mDvFullRange{true};
	bool mDvRejected{false};
	dv_state mDvPublishedState{DV_INACTIVE};
	uint64_t mDvStatusPublishedAt{0};
	uint64_t mDvStatsLoggedAt{0};
	// the last bandwidth based format decision that was logged, so it is logged once per change
	std::string mBandwidthDecision{};
	std::unique_ptr<MediaSideDataDOVIMetadata> mDvSideData{std::make_unique<MediaSideDataDOVIMetadata>()};
	// USB only
	uint16_t mCaptureSessionId{0};
	std::unique_ptr<video_capture> mVideoCapture;
};

#endif
