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
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

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
};

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
	 * @return an empty string on success, otherwise a description of the failure.
	 */
	std::string start(const std::string& filter, std::shared_ptr<quill::Sink> sink);

	// Removes the metrics logger, must be called while the quill backend is still running.
	void stop();

	bool active() const
	{
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

private:
	// an "info" style series, always 1, whose labels describe the current state; the previous series
	// is removed when the labels change so only the current state is ever exported
	struct info_series
	{
		std::string key{};
		const quill::MetricMetadata* meta{nullptr};
	};

	void set_info(info_series& info, std::string_view name, const std::vector<quill::MetricLabel>& labels);
	void clear_info(info_series& info);

	struct stage_series
	{
		std::string stage{};
		const quill::MetricMetadata* latency{nullptr};
		const quill::MetricMetadata* min{nullptr};
		const quill::MetricMetadata* mean{nullptr};
		const quill::MetricMetadata* max{nullptr};
	};

	using stream_series = std::array<stage_series, 3>;

	stage_series* resolve(metric_stream stream, size_t slot, const std::string& stage);

	std::string mFilter{};
	std::string mEndpoint{};
	CustomLogger* mLogger{nullptr};
	std::shared_ptr<quill::Sink> mSink{};
	std::array<stream_series, 2> mStreams{};
	const quill::MetricMetadata* mFps{nullptr};
	std::mutex mInfoLock{};
	info_series mVideoInfo{};
	info_series mAudioInfo{};
	std::array<const quill::MetricMetadata*, 4> mHdr{};
};

#endif
