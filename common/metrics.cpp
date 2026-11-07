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
#include "metrics.h" // first, defines NOMINMAX before any windows.h

#include <streams.h>
#include "domain.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <format>

#include <quill/Backend.h>
#include <quill/sinks/metrics/PrometheusSink.h>

namespace
{
	// latencies are measured in 100ns units
	constexpr double to_seconds = 1.0 / 10000000.0;

	const char* to_label(metric_stream stream)
	{
		return stream == metric_stream::VIDEO ? "video" : "audio";
	}

	std::string to_stage_label(const std::string& name)
	{
		std::string lower{name};
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lower;
	}

	std::string metric_key(std::string_view name, const std::vector<quill::MetricLabel>& labels)
	{
		std::string key{name};
		for (const auto& label : labels)
		{
			key += std::format("|{}={}", label.key, label.value);
		}
		return key;
	}

	const quill::MetricMetadata* get_metric(std::string_view name, const std::vector<quill::MetricLabel>& labels)
	{
		// MetricManager owns the metadata for the life of the process so pointers stay valid even if
		// a sample is still queued when the filter goes away.
		return CustomFrontend::create_or_get_metric(metric_key(name, labels), std::string{name}, labels);
	}

	std::string to_resolution(int x, int y)
	{
		return x > 0 && y > 0 ? std::format("{}x{}", x, y) : "";
	}

	std::string to_fps(double fps)
	{
		return fps > 0.0 ? std::format("{:.3f}", fps) : "";
	}

	std::string to_count(uint64_t v)
	{
		return v > 0 ? std::to_string(v) : "";
	}

	const char* to_bool(bool b)
	{
		return b ? "true" : "false";
	}

	constexpr auto videoSignalInfoHelp = "Always 1, the labels describe the current video input and output";
	constexpr auto audioSignalInfoHelp = "Always 1, the labels describe the current audio input and output";

	std::array<std::pair<const metric*, const std::string*>, 3> stages_of(const frame_metrics& m)
	{
		return {{{&m.m1, &m.name1}, {&m.m2, &m.name2}, {&m.m3, &m.name3}}};
	}

	void register_families(quill::PrometheusSink& sink)
	{
		// 100us to ~205ms
		sink.register_histogram_family(std::string{frameLatencyMetricName},
		                               "Time spent in each stage of frame capture",
		                               quill::PrometheusSink::exponential_buckets(0.0001, 2.0, 12));
		sink.register_gauge_family(std::string{frameLatencyWindowMetricName},
		                           "Min/mean/max stage latency over the most recent ~1.5s window");
		sink.register_gauge_family(std::string{measuredFpsMetricName},
		                           "Video frame rate measured over the most recent ~1.5s window");
		sink.register_gauge_family(std::string{videoSignalInfoMetricName}, videoSignalInfoHelp);
		sink.register_gauge_family(std::string{audioSignalInfoMetricName}, audioSignalInfoHelp);
		sink.register_gauge_family(std::string{hdrMaxCllMetricName},
		                           "MaxCLL of the current HDR content, 0 when there is no HDR metadata");
		sink.register_gauge_family(std::string{hdrMaxFallMetricName},
		                           "MaxFALL of the current HDR content, 0 when there is no HDR metadata");
		sink.register_gauge_family(std::string{hdrMaxMasteringLuminanceMetricName},
		                           "Max mastering display luminance of the current HDR content, 0 when there is no HDR metadata");
		sink.register_gauge_family(std::string{hdrMinMasteringLuminanceMetricName},
		                           "Min mastering display luminance of the current HDR content, 0 when there is no HDR metadata");
	}
}

capture_metrics::~capture_metrics()
{
	stop();
}

std::string capture_metrics::start(const std::string& filter, const metrics_config& cfg)
{
	if (cfg.port == 0)
	{
		return "No port configured";
	}
	quill::PrometheusSink::ExposerConfiguration exposerCfg;
	exposerCfg.bind_address = std::format("{}:{}", cfg.bindAddress, cfg.port);
	std::shared_ptr<quill::Sink> sink;
	try
	{
		sink = CustomFrontend::create_or_get_sink<quill::PrometheusSink>("prometheus:" + exposerCfg.bind_address,
		                                                                  exposerCfg);
	}
	catch (const std::exception& e)
	{
		return std::format("Unable to listen on {} ({})", exposerCfg.bind_address, e.what());
	}
	auto result = start(filter, std::move(sink));
	if (result.empty())
	{
		auto prom = static_cast<quill::PrometheusSink*>(mSink.get());
		mEndpoint = prom->exposer_scrape_endpoint();
	}
	return result;
}

std::string capture_metrics::start(const std::string& filter, std::shared_ptr<quill::Sink> sink)
{
	stop();
	auto prom = dynamic_cast<quill::PrometheusSink*>(sink.get());
	if (prom == nullptr)
	{
		return "Not a PrometheusSink";
	}
	try
	{
		register_families(*prom);
		const std::vector<quill::MetricLabel> filterLabel{{"filter", filter}};
		mFps = get_metric(measuredFpsMetricName, filterLabel);
		mHdr = {
			get_metric(hdrMaxCllMetricName, filterLabel),
			get_metric(hdrMaxFallMetricName, filterLabel),
			get_metric(hdrMaxMasteringLuminanceMetricName, filterLabel),
			get_metric(hdrMinMasteringLuminanceMetricName, filterLabel)
		};
		mLogger = CustomFrontend::create_or_get_logger("metrics:" + filter, sink, quill::PatternFormatterOptions{},
		                                               quill::ClockSourceType::System);
	}
	catch (const std::exception& e)
	{
		mLogger = nullptr;
		return std::format("Unable to initialise metrics ({})", e.what());
	}
	mFilter = filter;
	mSink = std::move(sink);
	mStreams = {};
	mVideoInfo = {};
	mAudioInfo = {};
	return {};
}

void capture_metrics::stop()
{
	if (mLogger != nullptr)
	{
		// the sink may be shared with another filter so remove our current state rather than leave it stale
		{
			std::lock_guard lock{mInfoLock};
			clear_info(mVideoInfo);
			clear_info(mAudioInfo);
		}
		// blocking removal guarantees the backend no longer references the sink so the port is released
		// before the filter is recreated, it is only possible while the backend is alive though
		if (quill::Backend::is_running())
		{
			CustomFrontend::remove_logger_blocking(mLogger);
		}
		else
		{
			CustomFrontend::remove_logger(mLogger);
		}
		mLogger = nullptr;
	}
	mSink.reset();
	mEndpoint.clear();
}

void capture_metrics::flush()
{
	if (mLogger != nullptr && quill::Backend::is_running())
	{
		mLogger->flush_log();
	}
}

void capture_metrics::set_info(info_series& info, std::string_view name, const std::vector<quill::MetricLabel>& labels)
{
	auto key = metric_key(name, labels);
	if (key == info.key)
	{
		return;
	}
	auto* prom = static_cast<quill::PrometheusSink*>(mSink.get());
	auto* meta = get_metric(name, labels);
	try
	{
		// a series that was removed earlier ignores samples until it is explicitly registered again
		if (!prom->has_metric(meta->metric_key()))
		{
			prom->register_gauge(meta, name == videoSignalInfoMetricName ? videoSignalInfoHelp : audioSignalInfoHelp);
		}
	}
	catch (const std::exception&)
	{
		return;
	}
	QUILL_METRIC(mLogger, meta, 1.0);
	clear_info(info);
	info.key = std::move(key);
	info.meta = meta;
}

void capture_metrics::clear_info(info_series& info)
{
	if (info.meta != nullptr)
	{
		// drain the queue first, a sample for the old series processed after it is removed would recreate it
		if (quill::Backend::is_running())
		{
			quill::Backend::notify();
			mLogger->flush_log();
		}
		static_cast<quill::PrometheusSink*>(mSink.get())->unregister_metric(info.meta);
	}
	info = {};
}

void capture_metrics::on_video_signal(const video_input_status& in, const video_output_status& out,
                                      const hdr_status& hdr)
{
	if (mLogger == nullptr)
	{
		return;
	}
	std::lock_guard lock{mInfoLock};
	set_info(mVideoInfo, videoSignalInfoMetricName, {
		         {"filter", mFilter},
		         {"signal", in.validSignal ? "locked" : "none"},
		         {"in_resolution", to_resolution(in.inX, in.inY)},
		         {"in_fps", to_fps(in.inFps)},
		         {"in_colour_format", in.inColourFormat},
		         {"in_quantisation", in.inQuantisation},
		         {"in_saturation", in.inSaturation},
		         {"in_bit_depth", to_count(in.inBitDepth > 0 ? in.inBitDepth : 0)},
		         {"in_pixel_layout", in.inPixelLayout},
		         {"out_resolution", to_resolution(out.outX, out.outY)},
		         {"out_fps", to_fps(out.outFps)},
		         {"out_colour_format", out.outColourFormat},
		         {"out_bit_depth", to_count(out.outBitDepth > 0 ? out.outBitDepth : 0)},
		         {"out_subsampling", out.outSubsampling},
		         {"out_pixel_format", out.outPixelStructure},
		         {"out_transfer_function", out.outTransferFunction},
		         {"hdr", to_bool(hdr.hdrOn)}
	         });
	QUILL_METRIC(mLogger, mHdr[0], hdr.hdrOn ? hdr.hdrMaxCLL : 0.0);
	QUILL_METRIC(mLogger, mHdr[1], hdr.hdrOn ? hdr.hdrMaxFALL : 0.0);
	QUILL_METRIC(mLogger, mHdr[2], hdr.hdrOn ? hdr.hdrMaxDML : 0.0);
	QUILL_METRIC(mLogger, mHdr[3], hdr.hdrOn ? hdr.hdrMinDML : 0.0);
}

void capture_metrics::on_audio_signal(const audio_input_status& in, const audio_output_status& out)
{
	if (mLogger == nullptr)
	{
		return;
	}
	std::lock_guard lock{mInfoLock};
	set_info(mAudioInfo, audioSignalInfoMetricName, {
		         {"filter", mFilter},
		         {"signal", in.audioInStatus ? "locked" : "none"},
		         {"in_pcm", to_bool(in.audioInIsPcm)},
		         {"in_sample_rate", to_count(in.audioInFs)},
		         {"in_bit_depth", to_count(in.audioInBitDepth)},
		         {"in_channel_map", std::format("{:#04x}", in.audioInChannelMap)},
		         {"out_codec", out.audioOutCodec},
		         {"out_sample_rate", to_count(out.audioOutFs)},
		         {"out_bit_depth", to_count(out.audioOutBitDepth)},
		         {"out_channels", to_count(out.audioOutChannelCount)},
		         {"out_channel_layout", out.audioOutChannelLayout}
	         });
}

capture_metrics::stage_series* capture_metrics::resolve(metric_stream stream, size_t slot, const std::string& stage)
{
	if (stage.empty())
	{
		return nullptr;
	}
	auto& series = mStreams[static_cast<size_t>(stream)][slot];
	if (series.latency == nullptr || series.stage != stage)
	{
		const auto stageLabel = to_stage_label(stage);
		const auto streamLabel = to_label(stream);
		std::vector<quill::MetricLabel> labels{{"filter", mFilter}, {"stream", streamLabel}, {"stage", stageLabel}};
		series.latency = get_metric(frameLatencyMetricName, labels);
		labels.emplace_back("stat", "min");
		series.min = get_metric(frameLatencyWindowMetricName, labels);
		labels.back().value = "mean";
		series.mean = get_metric(frameLatencyWindowMetricName, labels);
		labels.back().value = "max";
		series.max = get_metric(frameLatencyWindowMetricName, labels);
		series.stage = stage;
	}
	return &series;
}

void capture_metrics::on_frame(metric_stream stream, const frame_metrics& metrics)
{
	if (mLogger == nullptr)
	{
		return;
	}
	const auto stages = stages_of(metrics);
	for (size_t i = 0; i < stages.size(); ++i)
	{
		const auto sample = stages[i].first->last();
		if (sample == 0)
		{
			continue;
		}
		if (auto series = resolve(stream, i, *stages[i].second))
		{
			QUILL_METRIC(mLogger, series->latency, static_cast<double>(sample) * to_seconds);
		}
	}
}

void capture_metrics::on_window(metric_stream stream, const frame_metrics& metrics)
{
	if (mLogger == nullptr)
	{
		return;
	}
	const auto stages = stages_of(metrics);
	for (size_t i = 0; i < stages.size(); ++i)
	{
		const auto* m = stages[i].first;
		if (m->max() == 0)
		{
			continue;
		}
		if (auto series = resolve(stream, i, *stages[i].second))
		{
			QUILL_METRIC(mLogger, series->min, static_cast<double>(m->min()) * to_seconds);
			QUILL_METRIC(mLogger, series->mean, m->mean() * to_seconds);
			QUILL_METRIC(mLogger, series->max, static_cast<double>(m->max()) * to_seconds);
		}
	}
	if (stream == metric_stream::VIDEO && metrics.actualFrameRate > 0.0)
	{
		QUILL_METRIC(mLogger, mFps, metrics.actualFrameRate);
	}
}
