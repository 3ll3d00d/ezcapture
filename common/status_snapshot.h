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
#ifndef STATUS_SNAPSHOT_HEADER
#define STATUS_SNAPSHOT_HEADER

#include "signalinfo.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

// what the filter graph is doing with the filter
enum class filter_run_state : uint8_t
{
	STOPPED,
	PAUSED,
	RUNNING
};

const char* to_string(filter_run_state state);

// the values most automations want, worked out from the status, see docs/homeassistant.md
struct status_summary
{
	bool signal{false};
	// <w>x<h>p<fps>, empty when there is no signal
	std::string videoMode;
	// sdr, hdr10, hlg, dv, lldv, empty when there is no signal
	std::string dynamicRange;
	// none, pcm, ac3, eac3, truehd, dts, dts_hd, unknown
	std::string audio{"none"};
	std::optional<int> audioChannels;

	bool operator==(const status_summary&) const = default;
};

// the settings shown on the property page, as last sent by the filter
struct status_controls
{
	bool refreshRateSwitch{false};
	bool hdrProfileSwitch{false};
	DWORD hdrProfile{0};
	DWORD sdrProfile{0};
	bool highThreadPriority{false};
	bool audioCapture{false};
	bool metricsEnabled{false};
	std::string metricsBindAddress;
	DWORD metricsPort{0};
	std::string metricsStatus;
	bool pendingReload{false};
};

/*
 * The latest status of one filter, kept for the web API. The filter sends it everything it sends the property page,
 * each callback copies the payload under the lock (the same copy the page makes) so the web server never reads the
 * filter's own members. JSON is only built when asked for.
 */
class status_snapshot : public ISignalInfoCB
{
public:
	status_snapshot() = default;
	status_snapshot(const status_snapshot&) = delete;
	status_snapshot& operator=(const status_snapshot&) = delete;
	virtual ~status_snapshot() = default;

	// ISignalInfoCB
	HRESULT Reload(audio_input_status* payload) override;
	HRESULT Reload(audio_output_status* payload) override;
	HRESULT Reload(video_input_status* payload) override;
	HRESULT Reload(video_output_status* payload) override;
	HRESULT Reload(hdr_status* payload) override;
	HRESULT Reload(device_status* payload) override;
	HRESULT Reload(display_status* payload) override;
	HRESULT ReloadV1(latency_stats* payload) override;
	HRESULT ReloadV2(latency_stats* payload) override;
	HRESULT ReloadV3(latency_stats* payload) override;
	HRESULT ReloadVfps(double* payload) override;
	HRESULT ReloadVHeadroom(video_headroom* payload) override;
	HRESULT ReloadA1(latency_stats* payload) override;
	HRESULT ReloadA2(latency_stats* payload) override;
	HRESULT ReloadControls(const bool& rateEnabled, const bool& profileEnabled, const DWORD& hdr, const DWORD& sdr,
	                       const bool& highThreadPrio, const bool& audioCapture) override;
	HRESULT ReloadMetricsControls(const bool& enabled, const std::string& bindAddress, const DWORD& port,
	                              const std::string& status) override;
	HRESULT Reload(dv_status* payload) override;
	// the web settings are not part of the state document, the token must never be served
	HRESULT ReloadWebControls(const bool&, const std::string&, const DWORD&, const std::string&, const std::string&,
	                          const std::string&) override
	{
		return S_OK;
	}
	HRESULT OnFilterStopped() override;

	void SetRunState(filter_run_state state);

	// true when a setting has changed that only applies when the filter is next loaded
	void SetPendingReload(bool pending);

	// changes every time anything does, for use as an ETag
	uint64_t Version() const;

	status_summary Summary() const;
	status_controls Controls() const;

	/**
	 * The state document, see docs/homeassistant.md.
	 * @param id the filter id used in the API paths.
	 * @param name a description of the filter.
	 * @param version if not null, set to the version the document was built from.
	 */
	std::string ToJson(const std::string& id, const std::string& name, uint64_t* version = nullptr) const;

	// worked out from the status alone so it can be tested
	static status_summary Summarise(const video_input_status& vin, const video_output_status& vout,
	                                const hdr_status& hdr, const dv_status& dv, const audio_input_status& ain,
	                                const audio_output_status& aout);

	// the codec name the filter reports (codecNames) as a summary audio value
	static std::string AudioCodecId(const std::string& codecName);

protected:
	// called with the lock held after every change, so a subclass can see the summary change
	virtual void OnChanged()
	{
	}

	mutable std::mutex mLock;

	// everything below is guarded by mLock
	uint64_t mVersion{0};
	std::chrono::system_clock::time_point mUpdated{};
	filter_run_state mRunState{filter_run_state::STOPPED};
	audio_input_status mAudioInput{};
	audio_output_status mAudioOutput{};
	video_input_status mVideoInput{};
	video_output_status mVideoOutput{};
	hdr_status mHdr{};
	dv_status mDv{};
	device_status mDevice{};
	display_status mDisplay{};
	latency_stats mVideoLatency[3]{};
	latency_stats mAudioLatency[2]{};
	double mMeasuredFps{0.0};
	video_headroom mHeadroom{};
	status_controls mControls{};

private:
	void Changed();
};

#endif
