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
#ifndef BM_VIDEO_CAPTURE_PIN_HEADER
#define BM_VIDEO_CAPTURE_PIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "bm_capture_filter.h"
#include "video_capture_pin.h"
#include "VideoFrameWriter.h"
#include "any_rgb.h"
#include "straight_through.h"
#include <memory>

class blackmagic_video_capture_pin final :
	public hdmi_video_capture_pin<blackmagic_capture_filter, video_frame>
{
public:
	blackmagic_video_capture_pin(HRESULT* phr, blackmagic_capture_filter* pParent, bool pPreview, VIDEO_FORMAT pVideoFormat);
	~blackmagic_video_capture_pin() override;

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
	void OnChangeMediaType() override;

	std::shared_ptr<video_frame> mCurrentFrame;

private:
	void OnFrameWriterStrategyUpdated() override
	{
		switch (mFrameWriterStrategy)
		{
		case ANY_RGB:
			mFrameWriter = std::make_unique<any_rgb>(mLogData, mVideoFormat.cx, mVideoFormat.cy);
			break;
		case STRAIGHT_THROUGH:
			mFrameWriter = std::make_unique<straight_through>(mLogData, mVideoFormat.cx, mVideoFormat.cy,
				&mVideoFormat.pixelFormat);
			break;
		case YUY2_YV16:
		case Y210_P210:
		case UYVY_YV16:
		case BGR10_BGR48:
			#ifndef NO_QUILL
			LOG_ERROR(mLogData.logger, "[{}] Conversion strategy {} is not supported by bmcapture",
				mLogData.prefix, to_string(mFrameWriterStrategy));
			#endif
			break;
		default:
			hdmi_video_capture_pin::OnFrameWriterStrategyUpdated();
		}
	}
};

#endif