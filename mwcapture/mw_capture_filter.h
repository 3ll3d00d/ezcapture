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
#ifndef MW_CAPTURE_FILTER_HEADER
#define MW_CAPTURE_FILTER_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "capture_filter.h"
#include "modeswitcher.h"
#include "mw_domain.h"
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
				std::chrono::high_resolution_clock::now().time_since_epoch()).count() * 10;
		}
		return t;
	}
};

/**
 * Directshow filter which can uses the MWCapture SDK to receive video and audio from a HDMI capture card.
 * Can inject HDR/WCG data if found on the incoming HDMI stream.
 */
class magewell_capture_filter final :
	public hdmi_capture_filter<DEVICE_INFO, VIDEO_SIGNAL, AUDIO_SIGNAL>
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
	magewell_capture_filter(LPUNKNOWN punk, HRESULT* phr);
	~magewell_capture_filter() override;

	BOOL mInited;
};

#endif