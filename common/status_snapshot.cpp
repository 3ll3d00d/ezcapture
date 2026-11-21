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
#include "status_snapshot.h"
#include "web_json.h"

#include <cmath>
#include <format>

namespace
{
	// fps with at most 3 decimals and no trailing zeros, e.g. 23.976, 24, 59.94
	std::string format_fps(double fps)
	{
		auto s = std::format("{:.3f}", fps);
		while (!s.empty() && s.back() == '0')
		{
			s.pop_back();
		}
		if (!s.empty() && s.back() == '.')
		{
			s.pop_back();
		}
		return s;
	}

	const char* dv_state_id(dv_state state)
	{
		switch (state)
		{
		case DV_INACTIVE: return "inactive";
		case DV_SEARCHING: return "searching";
		case DV_LOCKED: return "locked";
		case DV_NO_METADATA: return "no_metadata";
		case DV_REJECTED: return "rejected";
		case DV_LLDV: return "lldv";
		}
		return "inactive";
	}

	const char* pixel_encoding_id(pixel_encoding enc)
	{
		switch (enc)
		{
		case RGB_444: return "rgb444";
		case YUV_422: return "yuv422";
		case YUV_444: return "yuv444";
		case YUV_420: return "yuv420";
		}
		return "yuv422";
	}

	// the transfer function names set in capture_filter::OnVideoFormatLoaded
	std::string transfer_function_id(const std::string& tf)
	{
		if (tf == "SMPTE ST 2084 (PQ)") return "pq";
		if (tf == "HLG") return "hlg";
		if (tf == "REC.709") return "bt709";
		return {};
	}

	// the subsampling names set in capture_filter::OnVideoFormatLoaded and the pixel layouts the filters set
	std::string sampling_id(const std::string& s)
	{
		if (s == "YUV 4:2:0") return "yuv420";
		if (s == "YUV 4:2:2") return "yuv422";
		if (s == "YUV 4:4:4") return "yuv444";
		if (s == "RGB 4:4:4") return "rgb444";
		return web_json::slug(s);
	}

	template <typename T>
	std::optional<T> positive(T v)
	{
		return v > 0 ? std::optional<T>{v} : std::nullopt;
	}

	template <typename T>
	std::optional<T> non_negative(T v)
	{
		return v >= 0 ? std::optional<T>{v} : std::nullopt;
	}

	void write_latency(web_json::writer& w, const latency_stats& lat)
	{
		w.begin_object();
		w.key("stage").value(web_json::slug(lat.name));
		w.key("min_ms").value(static_cast<double>(lat.min) / 10000.0);
		w.key("mean_ms").value(lat.mean / 10000.0);
		w.key("max_ms").value(static_cast<double>(lat.max) / 10000.0);
		w.end_object();
	}
}

const char* to_string(filter_run_state state)
{
	switch (state)
	{
	case filter_run_state::STOPPED: return "stopped";
	case filter_run_state::PAUSED: return "paused";
	case filter_run_state::RUNNING: return "running";
	}
	return "stopped";
}

void status_snapshot::Changed()
{
	++mVersion;
	mUpdated = std::chrono::system_clock::now();
	if (mListener)
	{
		auto summary = Summarise(mVideoInput, mVideoOutput, mHdr, mDv, mAudioInput, mAudioOutput);
		if (summary != mLastSummary || mRunState != mLastState)
		{
			mLastSummary = std::move(summary);
			mLastState = mRunState;
			mListener(mLastSummary, mLastState);
		}
	}
}

void status_snapshot::SetSummaryListener(summary_listener listener)
{
	std::lock_guard lock{mLock};
	mListener = std::move(listener);
	if (mListener)
	{
		mLastSummary = Summarise(mVideoInput, mVideoOutput, mHdr, mDv, mAudioInput, mAudioOutput);
		mLastState = mRunState;
		mListener(mLastSummary, mLastState);
	}
}

void status_snapshot::WriteSummary(web_json::writer& w, const status_summary& summary)
{
	w.begin_object();
	w.key("signal").value(summary.signal);
	w.key("video_mode").value_or_null(summary.videoMode);
	w.key("dynamic_range").value_or_null(summary.dynamicRange);
	w.key("audio").value(summary.audio);
	w.key("audio_channels").value(summary.audioChannels);
	w.end_object();
}

HRESULT status_snapshot::Reload(audio_input_status* payload)
{
	std::lock_guard lock{mLock};
	mAudioInput = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(audio_output_status* payload)
{
	std::lock_guard lock{mLock};
	mAudioOutput = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(video_input_status* payload)
{
	std::lock_guard lock{mLock};
	mVideoInput = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(video_output_status* payload)
{
	std::lock_guard lock{mLock};
	mVideoOutput = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(hdr_status* payload)
{
	std::lock_guard lock{mLock};
	mHdr = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(device_status* payload)
{
	std::lock_guard lock{mLock};
	mDevice = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(display_status* payload)
{
	std::lock_guard lock{mLock};
	mDisplay = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadV1(latency_stats* payload)
{
	std::lock_guard lock{mLock};
	mVideoLatency[0] = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadV2(latency_stats* payload)
{
	std::lock_guard lock{mLock};
	mVideoLatency[1] = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadV3(latency_stats* payload)
{
	std::lock_guard lock{mLock};
	mVideoLatency[2] = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadVfps(double* payload)
{
	std::lock_guard lock{mLock};
	mMeasuredFps = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadVHeadroom(video_headroom* payload)
{
	std::lock_guard lock{mLock};
	mHeadroom = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadA1(latency_stats* payload)
{
	std::lock_guard lock{mLock};
	mAudioLatency[0] = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadA2(latency_stats* payload)
{
	std::lock_guard lock{mLock};
	mAudioLatency[1] = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadControls(const bool& rateEnabled, const bool& profileEnabled, const DWORD& hdr,
                                        const DWORD& sdr, const bool& highThreadPrio, const bool& audioCapture)
{
	std::lock_guard lock{mLock};
	mControls.refreshRateSwitch = rateEnabled;
	mControls.hdrProfileSwitch = profileEnabled;
	mControls.hdrProfile = hdr;
	mControls.sdrProfile = sdr;
	mControls.highThreadPriority = highThreadPrio;
	mControls.audioCapture = audioCapture;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::ReloadMetricsControls(const bool& enabled, const std::string& bindAddress, const DWORD& port,
                                               const std::string& status)
{
	std::lock_guard lock{mLock};
	mControls.metricsEnabled = enabled;
	mControls.metricsBindAddress = bindAddress;
	mControls.metricsPort = port;
	mControls.metricsStatus = status;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::Reload(dv_status* payload)
{
	std::lock_guard lock{mLock};
	mDv = *payload;
	Changed();
	return S_OK;
}

HRESULT status_snapshot::OnFilterStopped()
{
	SetRunState(filter_run_state::STOPPED);
	return S_OK;
}

void status_snapshot::SetRunState(filter_run_state state)
{
	std::lock_guard lock{mLock};
	if (mRunState != state)
	{
		mRunState = state;
		Changed();
	}
}

void status_snapshot::SetPendingReload(bool pending)
{
	std::lock_guard lock{mLock};
	if (mControls.pendingReload != pending)
	{
		mControls.pendingReload = pending;
		Changed();
	}
}

uint64_t status_snapshot::Version() const
{
	std::lock_guard lock{mLock};
	return mVersion;
}

status_controls status_snapshot::Controls() const
{
	std::lock_guard lock{mLock};
	return mControls;
}

std::string status_snapshot::AudioCodecId(const std::string& codecName)
{
	// codecNames in domain.h
	if (codecName == "PCM") return "pcm";
	if (codecName == "AC3") return "ac3";
	if (codecName == "DTS") return "dts";
	if (codecName == "DTSHD") return "dts_hd";
	if (codecName == "EAC3") return "eac3";
	if (codecName == "TrueHD") return "truehd";
	if (codecName == "Unidentified") return "unknown";
	return "none";
}

status_summary status_snapshot::Summarise(const video_input_status& vin, const video_output_status& vout,
                                          const hdr_status& hdr, const dv_status& dv, const audio_input_status& ain,
                                          const audio_output_status& aout)
{
	status_summary s;
	s.signal = vin.validSignal;
	if (s.signal)
	{
		if (vin.inX > 0 && vin.inY > 0)
		{
			s.videoMode = std::format("{}x{}p{}", vin.inX, vin.inY, format_fps(vin.inFps));
		}
		if (dv.state == DV_LOCKED || dv.state == DV_NO_METADATA)
		{
			s.dynamicRange = "dv";
		}
		else if (dv.state == DV_LLDV)
		{
			s.dynamicRange = "lldv";
		}
		else if (const auto tf = transfer_function_id(vout.outTransferFunction); tf == "hlg")
		{
			s.dynamicRange = "hlg";
		}
		else if (hdr.hdrOn || tf == "pq")
		{
			s.dynamicRange = "hdr10";
		}
		else
		{
			s.dynamicRange = "sdr";
		}
	}
	s.audio = ain.audioInStatus ? AudioCodecId(aout.audioOutCodec) : "none";
	if (s.audio != "none" && aout.audioOutChannelCount > 0)
	{
		s.audioChannels = aout.audioOutChannelCount;
	}
	return s;
}

status_summary status_snapshot::Summary() const
{
	std::lock_guard lock{mLock};
	return Summarise(mVideoInput, mVideoOutput, mHdr, mDv, mAudioInput, mAudioOutput);
}

std::string status_snapshot::ToJson(const std::string& id, const std::string& name, uint64_t* version) const
{
	// copy under the lock, format without it
	std::unique_lock lock{mLock};
	const auto ver = mVersion;
	const auto updated = mUpdated;
	const auto runState = mRunState;
	const auto ain = mAudioInput;
	const auto aout = mAudioOutput;
	const auto vin = mVideoInput;
	const auto vout = mVideoOutput;
	const auto hdr = mHdr;
	const auto dv = mDv;
	const auto dev = mDevice;
	const auto disp = mDisplay;
	const latency_stats vlat[3] = {mVideoLatency[0], mVideoLatency[1], mVideoLatency[2]};
	const latency_stats alat[2] = {mAudioLatency[0], mAudioLatency[1]};
	const auto fps = mMeasuredFps;
	const auto headroom = mHeadroom;
	const auto controls = mControls;
	lock.unlock();

	if (version != nullptr)
	{
		*version = ver;
	}
	const auto summary = Summarise(vin, vout, hdr, dv, ain, aout);

	web_json::writer w;
	w.begin_object();

	w.key("filter").begin_object();
	w.key("id").value(id);
	w.key("name").value(name);
	w.key("state").value(to_string(runState));
	w.end_object();

	w.key("summary");
	WriteSummary(w, summary);

	w.key("device").begin_object();
	w.key("description").value_or_null(dev.deviceDesc);
	w.key("protocol").value(dev.protocol == USB ? "usb" : "pcie");
	w.key("temperature_c").value(dev.temperature != 0.0 ? std::optional{dev.temperature} : std::nullopt);
	w.key("fan_rpm").value(non_negative(static_cast<int>(dev.fanSpeed)));
	w.key("link_speed").value(positive(dev.linkSpeed));
	w.key("link_width").value(non_negative(dev.linkWidth));
	w.key("max_payload_size").value(non_negative(static_cast<int>(dev.maxPayloadSize)));
	w.key("max_read_request_size").value(non_negative(static_cast<int>(dev.maxReadRequestSize)));
	w.key("bandwidth_bytes_per_sec").value(positive(dev.bandwidth));
	w.key("bandwidth_used_bytes_per_sec").value(positive(dev.bandwidthUsed));
	w.end_object();

	w.key("display").begin_object();
	w.key("refresh_hz").value(positive(disp.freq));
	w.key("status_text").value_or_null(web_json::to_utf8(disp.status));
	w.end_object();

	w.key("video").begin_object();
	w.key("input").begin_object();
	w.key("valid").value(vin.validSignal);
	w.key("status").value_or_null(web_json::slug(vin.signalStatus));
	w.key("status_text").value_or_null(vin.signalStatus);
	w.key("width").value(positive(vin.inX));
	w.key("height").value(positive(vin.inY));
	w.key("aspect_x").value(positive(vin.inAspectX));
	w.key("aspect_y").value(positive(vin.inAspectY));
	w.key("fps").value(positive(vin.inFps));
	w.key("frame_duration").value(positive(vin.inFrameDuration));
	w.key("bit_depth").value(positive(vin.inBitDepth));
	w.key("colour_format").value_or_null(web_json::slug(vin.inColourFormat));
	w.key("pixel_layout").value_or_null(sampling_id(vin.inPixelLayout));
	w.key("quantisation").value_or_null(web_json::slug(vin.inQuantisation));
	w.key("saturation").value_or_null(web_json::slug(vin.inSaturation));
	w.key("pixel_clock_mhz").value(positive(vin.inPixelClockMhz));
	w.key("total_width").value(positive(vin.inTotalX));
	w.key("total_height").value(positive(vin.inTotalY));
	w.end_object();
	w.key("output").begin_object();
	w.key("width").value(positive(vout.outX));
	w.key("height").value(positive(vout.outY));
	w.key("aspect_x").value(positive(vout.outAspectX));
	w.key("aspect_y").value(positive(vout.outAspectY));
	w.key("fps").value(positive(vout.outFps));
	w.key("bit_depth").value(positive(vout.outBitDepth));
	w.key("pixel_format").value_or_null(web_json::slug(vout.outPixelStructure));
	w.key("colour_format").value_or_null(web_json::slug(vout.outColourFormat));
	w.key("subsampling").value_or_null(sampling_id(vout.outSubsampling));
	w.key("transfer_function").value_or_null(transfer_function_id(vout.outTransferFunction));
	w.key("quantisation").value_or_null(web_json::slug(vout.outQuantisation));
	w.key("saturation").value_or_null(web_json::slug(vout.outSaturation));
	w.end_object();
	w.key("measured_fps").value(positive(fps));
	w.key("headroom").begin_object();
	w.key("interval_ms").value(positive(headroom.intervalMs));
	w.key("busy_mean_ms").value(headroom.intervalMs > 0.0 ? std::optional{headroom.busyMeanMs} : std::nullopt);
	w.key("busy_max_ms").value(headroom.intervalMs > 0.0 ? std::optional{headroom.busyMaxMs} : std::nullopt);
	w.end_object();
	w.end_object();

	w.key("hdr").begin_object();
	w.key("active").value(hdr.hdrOn);
	if (hdr.hdrOn)
	{
		w.key("max_cll_nits").value(hdr.hdrMaxCLL);
		w.key("max_fall_nits").value(hdr.hdrMaxFALL);
		w.key("max_dml_nits").value(hdr.hdrMaxDML);
		w.key("min_dml_nits").value(hdr.hdrMinDML);
		w.key("primaries").begin_object();
		w.key("rx").value(hdr.hdrPrimaryRX);
		w.key("ry").value(hdr.hdrPrimaryRY);
		w.key("gx").value(hdr.hdrPrimaryGX);
		w.key("gy").value(hdr.hdrPrimaryGY);
		w.key("bx").value(hdr.hdrPrimaryBX);
		w.key("by").value(hdr.hdrPrimaryBY);
		w.key("wpx").value(hdr.hdrWpX);
		w.key("wpy").value(hdr.hdrWpY);
		w.end_object();
	}
	else
	{
		w.key("max_cll_nits").null();
		w.key("max_fall_nits").null();
		w.key("max_dml_nits").null();
		w.key("min_dml_nits").null();
		w.key("primaries").null();
	}
	w.end_object();

	w.key("dv").begin_object();
	w.key("state").value(dv_state_id(dv.state));
	w.key("metadata_valid").value(dv.metadataValid);
	w.key("full_range").value(dv.fullRange);
	w.key("colour_space").value(static_cast<int>(dv.colourSpace));
	w.key("source_min_pq").value(static_cast<int>(dv.sourceMinPq));
	w.key("source_max_pq").value(static_cast<int>(dv.sourceMaxPq));
	if (dv.hasL1)
	{
		w.key("l1").begin_object();
		w.key("min_pq").value(static_cast<int>(dv.l1MinPq));
		w.key("avg_pq").value(static_cast<int>(dv.l1AvgPq));
		w.key("max_pq").value(static_cast<int>(dv.l1MaxPq));
		w.end_object();
	}
	else
	{
		w.key("l1").null();
	}
	w.key("metadata_bytes").value(static_cast<uint64_t>(dv.metadataBytes));
	w.key("packets").value(static_cast<uint64_t>(dv.packets));
	w.key("frames").value(dv.frames);
	w.key("rejected_frames").value(dv.rejectedFrames);
	w.key("crc_failed_copies").value(dv.crcFailedCopies);
	w.key("recovered_packets").value(dv.recoveredPackets);
	if (dv.state == DV_LLDV)
	{
		w.key("lldv").begin_object();
		w.key("bit_depth").value(static_cast<int>(dv.lldvBitDepth));
		w.key("pixel_encoding").value(pixel_encoding_id(dv.lldvPixelEncoding));
		w.key("backlight_metadata").value(dv.lldvBacklightMetadata);
		w.key("effective_max_pq").value(static_cast<int>(dv.lldvEffectiveMaxPq));
		w.key("peak_nits").value(dv.lldvPeakNits);
		w.key("aux_run_mode").value(static_cast<int>(dv.lldvAuxRunMode));
		w.key("aux_version").value(static_cast<int>(dv.lldvAuxVersion));
		w.key("aux_debug").value(static_cast<int>(dv.lldvAuxDebug));
		w.end_object();
	}
	else
	{
		w.key("lldv").null();
	}
	w.end_object();

	w.key("audio").begin_object();
	w.key("input").begin_object();
	w.key("present").value(ain.audioInStatus);
	w.key("pcm").value(ain.audioInStatus ? std::optional{ain.audioInIsPcm} : std::nullopt);
	w.key("bit_depth").value(positive(static_cast<int>(ain.audioInBitDepth)));
	w.key("sample_rate_hz").value(positive(static_cast<uint64_t>(ain.audioInFs)));
	w.key("channel_pairs").value(static_cast<int>(ain.audioInChannelPairs));
	w.key("channel_allocation").value(static_cast<int>(ain.audioInChannelMap));
	w.key("lfe_level").value(static_cast<int>(ain.audioInLfeLevel));
	w.end_object();
	w.key("output").begin_object();
	w.key("codec").value(AudioCodecId(aout.audioOutCodec));
	w.key("sample_rate_hz").value(positive(static_cast<uint64_t>(aout.audioOutFs)));
	w.key("bit_depth").value(positive(static_cast<int>(aout.audioOutBitDepth)));
	w.key("channels").value(positive(static_cast<int>(aout.audioOutChannelCount)));
	w.key("channel_layout").value_or_null(aout.audioOutChannelLayout);
	w.key("lfe_offset_db").value(static_cast<int>(aout.audioOutLfeOffset));
	w.key("lfe_channel").value(non_negative(aout.audioOutLfeChannelIndex));
	w.key("burst_size").value(positive(static_cast<int>(aout.audioOutDataBurstSize)));
	w.end_object();
	w.end_object();

	w.key("latency").begin_object();
	w.key("video").begin_array();
	for (const auto& lat : vlat)
	{
		if (!lat.name.empty())
		{
			write_latency(w, lat);
		}
	}
	w.end_array();
	w.key("audio").begin_array();
	for (const auto& lat : alat)
	{
		if (!lat.name.empty())
		{
			write_latency(w, lat);
		}
	}
	w.end_array();
	w.end_object();

	w.key("controls").begin_object();
	w.key("refresh_rate_switch").value(controls.refreshRateSwitch);
	w.key("hdr_profile_switch").value(controls.hdrProfileSwitch);
	w.key("hdr_profile").value(static_cast<uint64_t>(controls.hdrProfile));
	w.key("sdr_profile").value(static_cast<uint64_t>(controls.sdrProfile));
	w.key("high_thread_priority").value(controls.highThreadPriority);
	w.key("audio_capture").value(controls.audioCapture);
	w.key("metrics_enabled").value(controls.metricsEnabled);
	w.key("pending_reload").value(controls.pendingReload);
	w.end_object();

	w.key("metrics").begin_object();
	w.key("enabled").value(controls.metricsEnabled);
	w.key("bind_address").value_or_null(controls.metricsBindAddress);
	w.key("port").value(positive(static_cast<uint64_t>(controls.metricsPort)));
	w.key("status_text").value_or_null(controls.metricsStatus);
	w.end_object();

	if (ver == 0)
	{
		w.key("updated").null();
	}
	else
	{
		const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(updated);
		w.key("updated").value(std::format("{:%FT%T}Z", ms));
	}

	w.end_object();
	return w.take();
}
