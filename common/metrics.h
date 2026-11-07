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
#ifndef METRICS_HEADER
#define METRICS_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logging.h"

struct frame_metrics;
struct video_input_status;
struct video_output_status;
struct hdr_status;
struct audio_input_status;
struct audio_output_status;

// Prometheus export of the capture latency metrics. prometheus-cpp is only included by metrics.cpp,
// everything else just publishes samples through a quill logger.

inline constexpr auto metricsEnabledRegKey = L"prometheusEnabled";
inline constexpr auto metricsPortRegKey = L"prometheusPort";
inline constexpr auto metricsBindAddressRegKey = L"prometheusBindAddress";
inline constexpr auto metricsSignalRetentionRegKey = L"prometheusSignalRetention";

inline constexpr std::string_view frameLatencyMetricName{"ezcapture_frame_latency_seconds"};
inline constexpr std::string_view frameLatencyWindowMetricName{"ezcapture_frame_latency_window_seconds"};
inline constexpr std::string_view measuredFpsMetricName{"ezcapture_video_measured_fps"};
inline constexpr std::string_view videoSignalInfoMetricName{"ezcapture_video_signal_info"};
inline constexpr std::string_view audioSignalInfoMetricName{"ezcapture_audio_signal_info"};
inline constexpr std::string_view hdrMaxCllMetricName{"ezcapture_hdr_max_cll_nits"};
inline constexpr std::string_view hdrMaxFallMetricName{"ezcapture_hdr_max_fall_nits"};
inline constexpr std::string_view hdrMaxMasteringLuminanceMetricName{"ezcapture_hdr_max_mastering_luminance_nits"};
inline constexpr std::string_view hdrMinMasteringLuminanceMetricName{"ezcapture_hdr_min_mastering_luminance_nits"};

struct metrics_config
{
	bool enabled{false};
	uint16_t port{0};
	std::string bindAddress{"127.0.0.1"};
	// how long the series of a signal that is no longer present are kept so a signal that drops out
	// and comes back does not churn the exported series
	std::chrono::seconds signalRetention{60};
};

/*
 * Signal ids, a compact and stable description of a signal built only from a fixed set of known values
 * (anything else is reported as "other") so the number of distinct ids stays small. Each is a "_"
 * separated list of fields so they can be split into separate labels on ingest if required.
 *
 * video input:  <width>x<height>_<fps>_<sampling>_<bit depth>, e.g. 3840x2160_23.976_yuv422_12
 * video output: <width>x<height>_<fps>_<pixel format>,        e.g. 3840x2160_23.976_p210
 * audio input:  <pcm|bitstream>_<sample rate>_<bit depth>_<channel allocation>, e.g. pcm_48k_24_0x13
 * audio output: <codec>_<sample rate>_<bit depth>_<channels>, e.g. pcm_48k_24_8ch
 *
 * "none" means there is no signal (input) or no format has been agreed yet (output).
 */
std::string video_input_signal_id(const video_input_status& in);
std::string video_output_signal_id(const video_output_status& out);
std::string audio_input_signal_id(const audio_input_status& in);
std::string audio_output_signal_id(const audio_output_status& out);

enum class metric_stream : uint8_t
{
	VIDEO,
	AUDIO
};

class capture_metrics
{
public:
	capture_metrics() = default;
	capture_metrics(const capture_metrics&) = delete;
	capture_metrics& operator=(const capture_metrics&) = delete;
	~capture_metrics();

	/**
	 * Starts a Prometheus scrape endpoint on cfg.bindAddress:cfg.port. Filters in the same module
	 * that use the same address share the endpoint. Never throws.
	 * @return an empty string on success, otherwise a description of the failure.
	 */
	std::string start(const std::string& filter, const metrics_config& cfg);

	/**
	 * Publishes to an existing quill::PrometheusSink, e.g. one without an exposer in tests.
	 * @param signalRetention how long the series of a signal that is no longer current are kept.
	 * @param housekeeping false to not start the thread that removes expired signals, i.e. the caller
	 *                     calls expire() itself.
	 * @return an empty string on success, otherwise a description of the failure.
	 */
	std::string start(const std::string& filter, std::shared_ptr<quill::Sink> sink,
	                  std::chrono::seconds signalRetention = std::chrono::seconds{60}, bool housekeeping = true);

	// Removes the metrics logger, must be called while the quill backend is still running.
	void stop();

	bool active() const
	{
		std::lock_guard lock{mLock};
		return mLogger != nullptr;
	}

	const std::string& endpoint() const
	{
		return mEndpoint;
	}

	// blocks until every published sample has reached the sink
	void flush();

	// called for every frame, publishes the stage latencies of that frame
	void on_frame(metric_stream stream, const frame_metrics& metrics);

	// called each time the metrics window completes, publishes the window stats
	void on_window(metric_stream stream, const frame_metrics& metrics);

	// called whenever the video input, video output or HDR metadata may have changed
	void on_video_signal(const video_input_status& in, const video_output_status& out, const hdr_status& hdr);

	// called whenever the audio input or output may have changed
	void on_audio_signal(const audio_input_status& in, const audio_output_status& out);

	// removes the series of any signal that has not been current for at least the retention period
	void expire(std::chrono::steady_clock::time_point now);

private:
	struct stage_series
	{
		std::string stage{};
		const quill::MetricMetadata* latency{nullptr};
		const quill::MetricMetadata* min{nullptr};
		const quill::MetricMetadata* mean{nullptr};
		const quill::MetricMetadata* max{nullptr};
	};

	// every series published while a particular in/out signal pair was current
	struct signal_series
	{
		std::string inSig{};
		std::string outSig{};
		// the info series, 1 while current else 0, its labels carry the detail behind the ids
		const quill::MetricMetadata* info{nullptr};
		std::array<stage_series, 3> stages{};
		const quill::MetricMetadata* fps{nullptr};
		bool hasFrames{false};
		std::chrono::steady_clock::time_point lastCurrent{};
	};

	struct stream_state
	{
		std::vector<std::unique_ptr<signal_series>> signals{};
		signal_series* current{nullptr};
	};

	void set_signal(metric_stream stream, std::string inSig, std::string outSig,
	                std::vector<quill::MetricLabel> details);
	signal_series& current_signal(metric_stream stream);
	stage_series* resolve(signal_series& signal, metric_stream stream, size_t slot, const std::string& stage);
	void retire(signal_series& signal);
	void unregister(const quill::MetricMetadata* meta);
	std::vector<quill::MetricLabel> series_labels(metric_stream stream, const signal_series& signal) const;

	std::string mFilter{};
	std::string mEndpoint{};
	CustomLogger* mLogger{nullptr};
	std::shared_ptr<quill::Sink> mSink{};
	std::chrono::seconds mRetention{60};
	mutable std::mutex mLock{};
	std::array<stream_state, 2> mStreams{};
	std::array<const quill::MetricMetadata*, 4> mHdr{};
	std::mutex mWakeLock{};
	std::condition_variable_any mWake{};
	std::jthread mHousekeeper{};
};

#endif
