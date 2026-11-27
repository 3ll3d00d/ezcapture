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
#include "row_workers.h"
#include <array>
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
	void OnBuffersReleasing() override;
	void StopCapture();
	// Pro cards only: buffers the card DMAs into are pinned once, on first use, rather than on every frame and are
	// unpinned before they can be freed
	void PinForCapture(uint8_t* data, DWORD size);
	void Unpin(const uint8_t* data);
	void UnpinAll();

	void LoadFormat(video_format* videoFormat, video_signal* videoSignal, const usb_capture_formats* captureFormats);
	// USB only
	static void CaptureFrame(BYTE* pbFrame, int cbFrame, UINT64 u64TimeStamp, void* pParam);

	void OnChangeMediaType() override;
	HRESULT LoadSignal(HCHANNEL* pChannel);
	void OnFrameWriterStrategyUpdated() override;

	// experimental Dolby Vision RGB tunnel decode
	bool IsDvTunnelCandidate() const;
	// reads the vendor specific InfoFrame, logging it when it changes, and keeps the Dolby one if present
	void LoadVsInfoFrame(HCHANNEL hChannel, DWORD validFlags);
	// a locked signal with a low latency Dolby VSIF and no HDR InfoFrame
	bool IsLldv() const;
	// the peak brightness the source mapped LLDV to, from the VSIF when present
	double LldvPeakNits() const;
	void LoadLldvHdrMeta(hdr_meta* meta) const;
	// USB only, whether the card can capture the signal as BGR24 at its own size, i.e. without scaling
	bool UsbCanCaptureDvTunnel() const;
	// frame is the first row in memory, bottomUp if that is the bottom row of the picture
	void ProbeDvTunnel(const uint8_t* frame, DWORD stride, bool bottomUp);
	// writes the captured tunnel frame, or black if length is too short to hold it
	void WriteDvTunnelFrame(const uint8_t* frame, DWORD stride, size_t length, bool bottomUp, IMediaSample* pms);

	// Magewell Pro captures that land in an intermediate buffer (the DV tunnel, CPU conversions) are pipelined: the
	// next frame is requested as soon as the current one has landed, so the card's copy (close to the PCIe limit at
	// 4K60) overlaps the CPU work on the frame instead of following it
	struct capture_target
	{
		uint8_t* buffer{nullptr};
		DWORD size{0};
		DWORD stride{0};
		DWORD fourcc{0};
		int cx{0};
		int cy{0};
		MWCAP_VIDEO_COLOR_FORMAT colourFormat{MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN};
		MWCAP_VIDEO_QUANTIZATION_RANGE quantisation{MWCAP_VIDEO_QUANTIZATION_UNKNOWN};
		MWCAP_VIDEO_SATURATION_RANGE saturation{MWCAP_VIDEO_SATURATION_UNKNOWN};
		bool dvTunnel{false};

		// the same capture apart from the buffer it lands in
		bool sameFormat(const capture_target& o) const
		{
			return size == o.size && stride == o.stride && fourcc == o.fourcc && cx == o.cx && cy == o.cy
				&& colourFormat == o.colourFormat && quantisation == o.quantisation && saturation == o.saturation
				&& dvTunnel == o.dvTunnel;
		}
	};

	struct pending_capture
	{
		bool active{false};
		int slot{0};
		// the frame index asked for, negative for a symbolic id (MWCAP_VIDEO_FRAME_ID_*)
		int frameId{0};
		int64_t requestedAt{0};
		capture_target target{};
	};

	bool IsPipelinedCapture() const;
	// what to capture for the current format into the given intermediate buffer, sized and pinned
	capture_target IntermediateTarget(int slot);
	HRESULT StartCapture(HCHANNEL hChannel, int frameId, const capture_target& target);
	// waits for the capture in flight to complete, false if it did not (mustExit is set if the stream should stop)
	bool WaitForCapture(HCHANNEL hChannel, bool& mustExit);
	bool StartPipelinedCapture(HCHANNEL hChannel, int frameId, const capture_target* mustMatch);
	// one pipelined frame into the sample, false if there is no frame to deliver (yet)
	bool GrabPipelined(HCHANNEL hChannel, IMediaSample* pms, bool& mustExit);
	// lets a capture still in flight complete so its buffer can be released or reused
	void DrainPendingCapture();
	void AppendDvSideData(IMediaSample* pms);
	void LogDvTunnelStats();
	void PublishDvStatus();
	void LogDvEdidSupport(HCHANNEL hChannel) const;
	void LogUsbProcessSettings(HCHANNEL hChannel) const;

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
	// LLDV, the last vendor specific InfoFrame as read so changes are logged once
	std::vector<uint8_t> mVsInfoFrame{};
	dv_tunnel::dolby_vsif mDolbyVsif{};
	uint8_t mVsMissedReads{0};
	static constexpr uint8_t vsMissedReadsBeforeClear{10};
	// dv tunnel
	dv_tunnel::decoder mDvDecoder{};
	bool mDvLocked{false};
	// intermediate capture buffers, two so one can be processed while the card copies into the other
	std::array<std::vector<uint8_t>, 2> mCaptureBuffers{};
	pending_capture mPendingCapture{};
	// splits the DV tunnel unpack across threads, at 4K60 one thread is too slow once it competes with the card's copy
	std::unique_ptr<row_workers> mUnpackWorkers{};
	static constexpr unsigned unpackWorkerCount{3};
	// whether the unpack uses streaming stores, chosen by timing both when the tunnel starts
	dv_tunnel::store_mode_tuner mUnpackStoreMode{};
	int mNextCaptureSlot{0};
	struct pinned_buffer
	{
		uint8_t* data;
		DWORD size;
		bool pinned; // false if the pin failed, the driver then locks the buffer on every capture instead
	};
	std::vector<pinned_buffer> mPinnedBuffers{};
	// the allocator the pinned samples belong to, a different allocator means different buffers
	IMemAllocator* mPinnedAllocator{nullptr};
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
