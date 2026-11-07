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
#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <utility>

#include <quill/Backend.h>
#include <quill/sinks/metrics/PrometheusSink.h>

namespace
{
	// latencies are measured in 100ns units
	constexpr double to_seconds = 1.0 / 10000000.0;

	constexpr auto latencyHelp = "Time spent in each stage of frame capture";
	constexpr auto latencyWindowHelp = "Min/mean/max stage latency over the most recent ~1.5s window";
	constexpr auto measuredFpsHelp = "Video frame rate measured over the most recent ~1.5s window";
	constexpr auto videoSignalInfoHelp =
		"1 for the current video signal, 0 for a recent signal that is retained, the labels describe the signal";
	constexpr auto audioSignalInfoHelp =
		"1 for the current audio signal, 0 for a recent signal that is retained, the labels describe the signal";

	constexpr std::array hdrHelp{
		"MaxCLL of the current HDR content, 0 when there is no HDR metadata",
		"MaxFALL of the current HDR content, 0 when there is no HDR metadata",
		"Max mastering display luminance of the current HDR content, 0 when there is no HDR metadata",
		"Min mastering display luminance of the current HDR content, 0 when there is no HDR metadata"
	};

	quill::PrometheusSink::HistogramBuckets latency_buckets()
	{
		// 100us to ~205ms
		return quill::PrometheusSink::exponential_buckets(0.0001, 2.0, 12);
	}

	const char* to_label(metric_stream stream)
	{
		return stream == metric_stream::VIDEO ? "video" : "audio";
	}

	std::string to_lower(std::string_view s)
	{
		std::string lower{s};
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

	// ---- signal ids, every field comes from a fixed list of known values ----

	constexpr auto otherId = "other";
	constexpr auto noneId = "none";

	std::string resolution_id(int x, int y)
	{
		constexpr std::array<std::pair<int, int>, 7> known{
			{{720, 480}, {720, 576}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {4096, 2160}}
		};
		const auto match = std::ranges::find(known, std::pair{x, y});
		return match == known.end() ? otherId : std::format("{}x{}", x, y);
	}

	std::string fps_id(double fps)
	{
		struct rate
		{
			double fps;
			const char* id;
		};
		constexpr std::array<rate, 13> known{
			{
				{24000.0 / 1001.0, "23.976"}, {24.0, "24"}, {25.0, "25"}, {30000.0 / 1001.0, "29.97"}, {30.0, "30"},
				{48000.0 / 1001.0, "47.952"}, {48.0, "48"}, {50.0, "50"}, {60000.0 / 1001.0, "59.94"}, {60.0, "60"},
				{100.0, "100"}, {120000.0 / 1001.0, "119.88"}, {120.0, "120"}
			}
		};
		// the NTSC rates are 0.1% away from the integer rates so the tolerance has to be well inside that
		for (const auto& r : known)
		{
			if (std::abs(fps - r.fps) < r.fps * 0.0003)
			{
				return r.id;
			}
		}
		return otherId;
	}

	std::optional<std::string> sampling_of(const std::string& desc)
	{
		if (desc.find("RGB") != std::string::npos)
		{
			return "rgb";
		}
		if (desc.find("4:2:0") != std::string::npos)
		{
			return "yuv420";
		}
		if (desc.find("4:2:2") != std::string::npos)
		{
			return "yuv422";
		}
		if (desc.find("4:4:4") != std::string::npos)
		{
			return "yuv444";
		}
		return std::nullopt;
	}

	std::string bit_depth_id(int depth)
	{
		return depth == 8 || depth == 10 || depth == 12 || depth == 16 ? std::to_string(depth) : otherId;
	}

	std::string pixel_format_id(const std::string& name)
	{
		static const std::array known{
			&NV12, &NV16, &P010, &P210, &AYUV, &BGR24, &BGR10, &YUY2, &UYVY, &Y210, &YUV2, &V210, &AY10, &ARGB,
			&BGRA, &RGBA, &R210, &R12B, &R12L, &R10L, &R10B, &YV16, &RGB48
		};
		const auto match = std::ranges::find_if(known, [&name](const pixel_format* pf) { return pf->name == name; });
		if (match == known.end())
		{
			return otherId;
		}
		auto id = to_lower((*match)->name);
		std::erase(id, ' ');
		return id;
	}

	std::string sample_rate_id(unsigned long fs)
	{
		switch (fs)
		{
		case 32000: return "32k";
		case 44100: return "44.1k";
		case 48000: return "48k";
		case 88200: return "88.2k";
		case 96000: return "96k";
		case 176400: return "176.4k";
		case 192000: return "192k";
		default: return otherId;
		}
	}

	std::string audio_bit_depth_id(unsigned int depth)
	{
		return depth == 16 || depth == 20 || depth == 24 || depth == 32 ? std::to_string(depth) : otherId;
	}

	std::string codec_id(const std::string& codec)
	{
		// codecNames
		if (codec == "PCM") return "pcm";
		if (codec == "AC3") return "ac3";
		if (codec == "DTS") return "dts";
		if (codec == "DTSHD") return "dtshd";
		if (codec == "EAC3") return "eac3";
		if (codec == "TrueHD") return "truehd";
		if (codec == "Unidentified") return "bitstream";
		if (codec == "PAUSE_OR_NULL") return "null";
		return otherId;
	}

	std::array<std::pair<const metric*, const std::string*>, 3> stages_of(const frame_metrics& m)
	{
		return {{{&m.m1, &m.name1}, {&m.m2, &m.name2}, {&m.m3, &m.name3}}};
	}

	void register_families(quill::PrometheusSink& sink)
	{
		sink.register_histogram_family(std::string{frameLatencyMetricName}, latencyHelp, latency_buckets());
		sink.register_gauge_family(std::string{frameLatencyWindowMetricName}, latencyWindowHelp);
		sink.register_gauge_family(std::string{measuredFpsMetricName}, measuredFpsHelp);
		sink.register_gauge_family(std::string{videoSignalInfoMetricName}, videoSignalInfoHelp);
		sink.register_gauge_family(std::string{audioSignalInfoMetricName}, audioSignalInfoHelp);
		sink.register_gauge_family(std::string{hdrMaxCllMetricName}, hdrHelp[0]);
		sink.register_gauge_family(std::string{hdrMaxFallMetricName}, hdrHelp[1]);
		sink.register_gauge_family(std::string{hdrMaxMasteringLuminanceMetricName}, hdrHelp[2]);
		sink.register_gauge_family(std::string{hdrMinMasteringLuminanceMetricName}, hdrHelp[3]);
	}

	// a series removed earlier ignores samples until it is explicitly registered again so every series
	// is registered explicitly before use
	bool ensure_gauge(quill::PrometheusSink& sink, const quill::MetricMetadata* meta, const char* help)
	{
		try
		{
			if (!sink.has_metric(meta->metric_key()))
			{
				sink.register_gauge(meta, help);
			}
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	bool ensure_histogram(quill::PrometheusSink& sink, const quill::MetricMetadata* meta)
	{
		try
		{
			if (!sink.has_metric(meta->metric_key()))
			{
				sink.register_histogram(meta, latencyHelp, latency_buckets());
			}
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
}

std::string video_input_signal_id(const video_input_status& in)
{
	if (!in.validSignal)
	{
		return noneId;
	}
	// magewell reports subsampling in the pixel layout, blackmagic in the colour format
	auto sampling = sampling_of(in.inPixelLayout);
	if (!sampling)
	{
		sampling = sampling_of(in.inColourFormat);
	}
	return std::format("{}_{}_{}_{}", resolution_id(in.inX, in.inY), fps_id(in.inFps), sampling.value_or(otherId),
	                   bit_depth_id(in.inBitDepth));
}

std::string video_output_signal_id(const video_output_status& out)
{
	if (out.outX <= 0 || out.outY <= 0)
	{
		return noneId;
	}
	return std::format("{}_{}_{}", resolution_id(out.outX, out.outY), fps_id(out.outFps),
	                   pixel_format_id(out.outPixelStructure));
}

std::string audio_input_signal_id(const audio_input_status& in)
{
	if (!in.audioInStatus)
	{
		return noneId;
	}
	return std::format("{}_{}_{}_{:#04x}", in.audioInIsPcm ? "pcm" : "bitstream", sample_rate_id(in.audioInFs),
	                   audio_bit_depth_id(in.audioInBitDepth), in.audioInChannelMap);
}

std::string audio_output_signal_id(const audio_output_status& out)
{
	if (out.audioOutCodec.empty())
	{
		return noneId;
	}
	const auto channels = out.audioOutChannelCount > 0 && out.audioOutChannelCount <= 8
		                      ? std::format("{}ch", out.audioOutChannelCount)
		                      : std::string{otherId};
	return std::format("{}_{}_{}_{}", codec_id(out.audioOutCodec), sample_rate_id(out.audioOutFs),
	                   audio_bit_depth_id(out.audioOutBitDepth), channels);
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
	auto result = start(filter, std::move(sink), cfg.signalRetention);
	if (result.empty())
	{
		std::lock_guard lock{mLock};
		mEndpoint = static_cast<quill::PrometheusSink*>(mSink.get())->exposer_scrape_endpoint();
	}
	return result;
}

std::string capture_metrics::start(const std::string& filter, std::shared_ptr<quill::Sink> sink,
                                   std::chrono::seconds signalRetention, bool housekeeping)
{
	stop();
	auto prom = dynamic_cast<quill::PrometheusSink*>(sink.get());
	if (prom == nullptr)
	{
		return "Not a PrometheusSink";
	}
	CustomLogger* logger{nullptr};
	std::array<const quill::MetricMetadata*, 4> hdr{};
	try
	{
		register_families(*prom);
		const std::vector<quill::MetricLabel> filterLabel{{"filter", filter}};
		hdr = {
			get_metric(hdrMaxCllMetricName, filterLabel),
			get_metric(hdrMaxFallMetricName, filterLabel),
			get_metric(hdrMaxMasteringLuminanceMetricName, filterLabel),
			get_metric(hdrMinMasteringLuminanceMetricName, filterLabel)
		};
		for (size_t i = 0; i < hdr.size(); ++i)
		{
			if (!prom->has_metric(hdr[i]->metric_key()))
			{
				prom->register_gauge(hdr[i], hdrHelp[i]);
			}
		}
		logger = CustomFrontend::create_or_get_logger("metrics:" + filter, sink, quill::PatternFormatterOptions{},
		                                              quill::ClockSourceType::System);
	}
	catch (const std::exception& e)
	{
		return std::format("Unable to initialise metrics ({})", e.what());
	}
	{
		std::lock_guard lock{mLock};
		mFilter = filter;
		mSink = std::move(sink);
		mRetention = signalRetention;
		mStreams = {};
		mHdr = hdr;
		mLogger = logger;
	}
	if (housekeeping)
	{
		mHousekeeper = std::jthread{
			[this](const std::stop_token& stop)
			{
				std::unique_lock lock{mWakeLock};
				while (!mWake.wait_for(lock, stop, std::chrono::seconds{1}, [] { return false; }) && !stop.stop_requested())
				{
					expire(std::chrono::steady_clock::now());
				}
			}
		};
	}
	return {};
}

void capture_metrics::stop()
{
	if (mHousekeeper.joinable())
	{
		mHousekeeper.request_stop();
		mHousekeeper.join();
	}
	CustomLogger* logger{nullptr};
	{
		std::lock_guard lock{mLock};
		logger = mLogger;
		if (logger != nullptr)
		{
			// the sink may be shared with another filter so remove everything we published rather than
			// leave it stale, a sample still in the queue for a removed series is ignored
			for (auto& stream : mStreams)
			{
				for (const auto& signal : stream.signals)
				{
					retire(*signal);
				}
			}
			mStreams = {};
			for (const auto* hdr : mHdr)
			{
				unregister(hdr);
			}
		}
		mLogger = nullptr;
		mEndpoint.clear();
	}
	if (logger != nullptr)
	{
		// blocking removal guarantees the backend no longer references the sink so the port is released
		// before the filter is recreated, it is only possible while the backend is alive though
		if (quill::Backend::is_running())
		{
			CustomFrontend::remove_logger_blocking(logger);
		}
		else
		{
			CustomFrontend::remove_logger(logger);
		}
	}
	std::lock_guard lock{mLock};
	mSink.reset();
}

void capture_metrics::flush()
{
	CustomLogger* logger{nullptr};
	{
		std::lock_guard lock{mLock};
		logger = mLogger;
	}
	if (logger != nullptr && quill::Backend::is_running())
	{
		logger->flush_log();
	}
}

void capture_metrics::unregister(const quill::MetricMetadata* meta)
{
	if (meta != nullptr)
	{
		static_cast<quill::PrometheusSink*>(mSink.get())->unregister_metric(meta);
	}
}

void capture_metrics::retire(signal_series& signal)
{
	unregister(signal.info);
	unregister(signal.fps);
	for (const auto& stage : signal.stages)
	{
		unregister(stage.latency);
		unregister(stage.min);
		unregister(stage.mean);
		unregister(stage.max);
	}
}

std::vector<quill::MetricLabel> capture_metrics::series_labels(metric_stream stream,
                                                               const signal_series& signal) const
{
	return {{"filter", mFilter}, {"stream", to_label(stream)}, {"in_sig", signal.inSig}, {"out_sig", signal.outSig}};
}

void capture_metrics::set_signal(metric_stream stream, std::string inSig, std::string outSig,
                                 std::vector<quill::MetricLabel> details)
{
	auto& state = mStreams[static_cast<size_t>(stream)];
	auto* prom = static_cast<quill::PrometheusSink*>(mSink.get());
	const auto now = std::chrono::steady_clock::now();
	const auto infoName = stream == metric_stream::VIDEO ? videoSignalInfoMetricName : audioSignalInfoMetricName;
	const auto infoHelp = stream == metric_stream::VIDEO ? videoSignalInfoHelp : audioSignalInfoHelp;

	auto* previous = state.current;
	if (previous == nullptr || previous->inSig != inSig || previous->outSig != outSig)
	{
		if (previous != nullptr)
		{
			previous->lastCurrent = now;
			if (previous->hasFrames)
			{
				// keep it around in case the same signal comes back shortly
				if (previous->info != nullptr)
				{
					QUILL_METRIC(mLogger, previous->info, 0.0);
				}
			}
			else
			{
				// never carried any frames (e.g. a transient state while the input and output are both
				// changing) so there is nothing worth keeping
				retire(*previous);
				std::erase_if(state.signals, [previous](const auto& s) { return s.get() == previous; });
			}
		}
		const auto match = std::ranges::find_if(state.signals, [&inSig, &outSig](const auto& s)
		{
			return s->inSig == inSig && s->outSig == outSig;
		});
		if (match != state.signals.end())
		{
			state.current = match->get();
		}
		else
		{
			auto created = std::make_unique<signal_series>();
			created->inSig = std::move(inSig);
			created->outSig = std::move(outSig);
			state.current = created.get();
			state.signals.push_back(std::move(created));
		}
	}

	auto& current = *state.current;
	std::vector<quill::MetricLabel> labels{
		{"filter", mFilter}, {"in_sig", current.inSig}, {"out_sig", current.outSig}
	};
	labels.insert(labels.end(), std::make_move_iterator(details.begin()), std::make_move_iterator(details.end()));
	const auto* info = get_metric(infoName, labels);
	if (info == current.info && current.info != nullptr && state.current == previous)
	{
		// unchanged
		return;
	}
	if (!ensure_gauge(*prom, info, infoHelp))
	{
		return;
	}
	QUILL_METRIC(mLogger, info, 1.0);
	if (current.info != info)
	{
		// the detail behind the ids changed, there is only ever one info series per signal id
		unregister(current.info);
		current.info = info;
	}
}

capture_metrics::signal_series& capture_metrics::current_signal(metric_stream stream)
{
	auto& state = mStreams[static_cast<size_t>(stream)];
	if (state.current == nullptr)
	{
		// frames before the signal was described
		auto created = std::make_unique<signal_series>();
		created->inSig = "unknown";
		created->outSig = "unknown";
		state.current = created.get();
		state.signals.push_back(std::move(created));
	}
	return *state.current;
}

void capture_metrics::expire(std::chrono::steady_clock::time_point now)
{
	std::lock_guard lock{mLock};
	if (mLogger == nullptr)
	{
		return;
	}
	for (auto& state : mStreams)
	{
		std::erase_if(state.signals, [this, &state, now](const auto& signal)
		{
			if (signal.get() == state.current || now - signal->lastCurrent < mRetention)
			{
				return false;
			}
			retire(*signal);
			return true;
		});
	}
}

void capture_metrics::on_video_signal(const video_input_status& in, const video_output_status& out,
                                      const hdr_status& hdr)
{
	std::lock_guard lock{mLock};
	if (mLogger == nullptr)
	{
		return;
	}
	set_signal(metric_stream::VIDEO, video_input_signal_id(in), video_output_signal_id(out), {
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
	std::lock_guard lock{mLock};
	if (mLogger == nullptr)
	{
		return;
	}
	set_signal(metric_stream::AUDIO, audio_input_signal_id(in), audio_output_signal_id(out), {
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

capture_metrics::stage_series* capture_metrics::resolve(signal_series& signal, metric_stream stream, size_t slot,
                                                        const std::string& stage)
{
	if (stage.empty())
	{
		return nullptr;
	}
	auto& series = signal.stages[slot];
	if (series.latency == nullptr || series.stage != stage)
	{
		unregister(series.latency);
		unregister(series.min);
		unregister(series.mean);
		unregister(series.max);
		series = {};

		auto& prom = *static_cast<quill::PrometheusSink*>(mSink.get());
		auto labels = series_labels(stream, signal);
		labels.emplace_back("stage", to_lower(stage));
		const auto* latency = get_metric(frameLatencyMetricName, labels);
		labels.emplace_back("stat", "min");
		const auto* min = get_metric(frameLatencyWindowMetricName, labels);
		labels.back().value = "mean";
		const auto* mean = get_metric(frameLatencyWindowMetricName, labels);
		labels.back().value = "max";
		const auto* max = get_metric(frameLatencyWindowMetricName, labels);
		if (!ensure_histogram(prom, latency) || !ensure_gauge(prom, min, latencyWindowHelp)
			|| !ensure_gauge(prom, mean, latencyWindowHelp) || !ensure_gauge(prom, max, latencyWindowHelp))
		{
			return nullptr;
		}
		series = {stage, latency, min, mean, max};
	}
	return &series;
}

void capture_metrics::on_frame(metric_stream stream, const frame_metrics& metrics)
{
	std::lock_guard lock{mLock};
	if (mLogger == nullptr)
	{
		return;
	}
	auto& signal = current_signal(stream);
	signal.hasFrames = true;
	const auto stages = stages_of(metrics);
	for (size_t i = 0; i < stages.size(); ++i)
	{
		const auto sample = stages[i].first->last();
		if (sample == 0)
		{
			continue;
		}
		if (auto series = resolve(signal, stream, i, *stages[i].second))
		{
			QUILL_METRIC(mLogger, series->latency, static_cast<double>(sample) * to_seconds);
		}
	}
}

void capture_metrics::on_window(metric_stream stream, const frame_metrics& metrics)
{
	std::lock_guard lock{mLock};
	if (mLogger == nullptr)
	{
		return;
	}
	auto& signal = current_signal(stream);
	signal.hasFrames = true;
	const auto stages = stages_of(metrics);
	for (size_t i = 0; i < stages.size(); ++i)
	{
		const auto* m = stages[i].first;
		if (m->max() == 0)
		{
			continue;
		}
		if (auto series = resolve(signal, stream, i, *stages[i].second))
		{
			QUILL_METRIC(mLogger, series->min, static_cast<double>(m->min()) * to_seconds);
			QUILL_METRIC(mLogger, series->mean, m->mean() * to_seconds);
			QUILL_METRIC(mLogger, series->max, static_cast<double>(m->max()) * to_seconds);
		}
	}
	if (stream == metric_stream::VIDEO && metrics.actualFrameRate > 0.0)
	{
		if (signal.fps == nullptr)
		{
			const auto* fps = get_metric(measuredFpsMetricName, {
				                             {"filter", mFilter}, {"in_sig", signal.inSig}, {"out_sig", signal.outSig}
			                             });
			if (!ensure_gauge(*static_cast<quill::PrometheusSink*>(mSink.get()), fps, measuredFpsHelp))
			{
				return;
			}
			signal.fps = fps;
		}
		QUILL_METRIC(mLogger, signal.fps, metrics.actualFrameRate);
	}
}
