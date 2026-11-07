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
#include "gtest/gtest.h"

#include "metrics.h" // first, defines NOMINMAX before any windows.h

#include <streams.h>
#include "domain.h"

#include <chrono>
#include <optional>
#include <string>

#include <quill/Backend.h>
#include <quill/sinks/metrics/PrometheusSink.h>
#include <prometheus/text_serializer.h>

namespace
{
	class MetricsTest : public testing::Test
	{
	protected:
		void SetUp() override
		{
			quill::Backend::start();
		}

		void TearDown() override
		{
			quill::Backend::stop();
		}

		static std::string scrape(quill::PrometheusSink& sink)
		{
			return prometheus::TextSerializer{}.Serialize(sink.registry().Collect());
		}

		// the value of the series whose name + labels are exactly `series`
		static std::optional<double> value_of(const std::string& text, const std::string& series)
		{
			const auto needle = "\n" + series + " ";
			const auto pos = ("\n" + text).find(needle);
			if (pos == std::string::npos)
			{
				return std::nullopt;
			}
			return std::stod(text.substr(pos + needle.size() - 1));
		}

		static void sample(frame_metrics& fm, uint64_t capture, uint64_t conversion, uint64_t host)
		{
			fm.m1.sample(capture);
			fm.m2.sample(conversion);
			fm.m3.sample(host);
		}
	};
}

TEST_F(MetricsTest, PublishesPerFrameLatencyAndWindowStats)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_sink");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);

	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink), "");
	ASSERT_TRUE(metrics.active());

	frame_metrics fm;
	fm.refreshRate(1.4); // window of 2 frames
	ASSERT_EQ(fm.m1.capacity(), 2);
	fm.name3 = "Host";

	// values are in 100ns units
	sample(fm, 20000, 5000, 10000);
	metrics.on_frame(metric_stream::VIDEO, fm);
	sample(fm, 40000, 15000, 30000);
	fm.actualFrameRate = 59.94;
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.on_window(metric_stream::VIDEO, fm);
	metrics.flush();

	const auto text = scrape(prom);
	const std::string video = R"(filter="test",in_sig="unknown",out_sig="unknown",stage="capture",stream="video")";

	EXPECT_EQ(value_of(text, "ezcapture_frame_latency_seconds_count{" + video + "}"), 2.0) << text;
	EXPECT_NEAR(value_of(text, "ezcapture_frame_latency_seconds_sum{" + video + "}").value_or(-1), 0.006, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_seconds_sum{filter="test",in_sig="unknown",out_sig="unknown",stage="conversion",stream="video"})")
	            .value_or(-1), 0.002, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_seconds_sum{filter="test",in_sig="unknown",out_sig="unknown",stage="host",stream="video"})")
	            .value_or(-1), 0.004, 1e-9);

	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",in_sig="unknown",out_sig="unknown",stage="capture",stat="min",stream="video"})")
	            .value_or(-1), 0.002, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",in_sig="unknown",out_sig="unknown",stage="capture",stat="mean",stream="video"})")
	            .value_or(-1), 0.003, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",in_sig="unknown",out_sig="unknown",stage="capture",stat="max",stream="video"})")
	            .value_or(-1), 0.004, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_video_measured_fps{filter="test",in_sig="unknown",out_sig="unknown"})").value_or(-1), 59.94, 1e-9);

	metrics.stop();
	EXPECT_FALSE(metrics.active());
}

TEST_F(MetricsTest, SkipsStagesWithoutSamples)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_sink_unnamed");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);

	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink), "");

	frame_metrics fm;
	fm.refreshRate(1.4);
	// no name3 so the 3rd stage is not reported, a 0 sample is rejected
	sample(fm, 20000, 0, 10000);
	metrics.on_frame(metric_stream::AUDIO, fm);
	metrics.flush();

	const auto text = scrape(prom);
	EXPECT_EQ(value_of(text, R"(ezcapture_frame_latency_seconds_count{filter="test",in_sig="unknown",out_sig="unknown",stage="capture",stream="audio"})"), 1.0)
		<< text;
	EXPECT_EQ(text.find(R"(stage="conversion")"), std::string::npos) << text;
	EXPECT_EQ(text.find(R"(stage="host")"), std::string::npos) << text;
	metrics.stop();
}

TEST_F(MetricsTest, RejectsSinkThatIsNotPrometheus)
{
	capture_metrics metrics;
	EXPECT_NE(metrics.start("test", std::shared_ptr<quill::Sink>{}), "");
	EXPECT_FALSE(metrics.active());
}

TEST_F(MetricsTest, ServesEndpointAndReleasesPortOnStop)
{
	metrics_config cfg{true, 19464, "127.0.0.1"};
	{
		capture_metrics first;
		ASSERT_EQ(first.start("first", cfg), "");
		EXPECT_EQ(first.endpoint(), "http://127.0.0.1:19464/metrics");

		// a second filter in the same module shares the endpoint
		capture_metrics second;
		ASSERT_EQ(second.start("second", cfg), "");
		EXPECT_EQ(second.endpoint(), first.endpoint());
		second.stop();
		first.stop();
	}
	// the port is free again once every user has stopped
	capture_metrics again;
	EXPECT_EQ(again.start("again", cfg), "");
	again.stop();
}

TEST_F(MetricsTest, ReportsPortConflict)
{
	// something else (here a sink that capture_metrics does not know about) already owns the port
	auto blocker = quill::Frontend::create_or_get_sink<quill::PrometheusSink>(
		"blocker", quill::PrometheusSink::ExposerConfiguration{"127.0.0.1:19465"});
	ASSERT_TRUE(blocker);

	// must report the failure rather than throw
	capture_metrics metrics;
	const auto err = metrics.start("test", metrics_config{true, 19465, "127.0.0.1"});
	EXPECT_NE(err, "");
	EXPECT_FALSE(metrics.active());
}

namespace
{
	video_input_status uhd_input()
	{
		video_input_status in{};
		in.validSignal = true;
		in.inX = 3840;
		in.inY = 2160;
		in.inFps = 23.976;
		in.inColourFormat = "BT2020";
		in.inQuantisation = "Limited";
		in.inSaturation = "Limited";
		in.inBitDepth = 12;
		in.inPixelLayout = "YUV 4:2:2";
		return in;
	}

	video_output_status uhd_output()
	{
		video_output_status out{};
		out.outX = 3840;
		out.outY = 2160;
		out.outFps = 23.976;
		out.outColourFormat = "BT2020";
		out.outBitDepth = 10;
		out.outSubsampling = "YUV 4:2:2";
		out.outPixelStructure = "P210";
		out.outTransferFunction = "SMPTE ST 2084 (PQ)";
		return out;
	}

	size_t count_of(const std::string& text, const std::string& needle)
	{
		size_t n = 0;
		for (auto pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
		{
			++n;
		}
		return n;
	}
}

TEST(SignalIds, AreBuiltFromKnownValues)
{
	auto in = uhd_input();
	EXPECT_EQ(video_input_signal_id(in), "3840x2160_23.976_yuv422_12");
	in.inFps = 23.97602; // as measured from the frame duration
	EXPECT_EQ(video_input_signal_id(in), "3840x2160_23.976_yuv422_12");
	in.inFps = 24.0;
	in.inX = 1920;
	in.inY = 1080;
	in.inBitDepth = 8;
	in.inPixelLayout = "RGB 4:4:4";
	EXPECT_EQ(video_input_signal_id(in), "1920x1080_24_rgb_8");
	// blackmagic describes the subsampling in the colour format and the frame layout in the pixel layout
	in.inColourFormat = "YUV 4:2:2";
	in.inPixelLayout = "V210";
	in.inBitDepth = 10;
	EXPECT_EQ(video_input_signal_id(in), "1920x1080_24_yuv422_10");
	// anything unknown is "other" so the number of ids is bounded
	in.inX = 1366;
	in.inY = 768;
	in.inFps = 23.5;
	in.inBitDepth = 9;
	in.inColourFormat = "?";
	in.inPixelLayout = "?";
	EXPECT_EQ(video_input_signal_id(in), "other_other_other_other");
	in.validSignal = false;
	EXPECT_EQ(video_input_signal_id(in), "none");

	auto out = uhd_output();
	EXPECT_EQ(video_output_signal_id(out), "3840x2160_23.976_p210");
	out.outPixelStructure = "BGR ";
	out.outFps = 60000.0 / 1001.0;
	EXPECT_EQ(video_output_signal_id(out), "3840x2160_59.94_bgr");
	out.outPixelStructure = "XYZW";
	EXPECT_EQ(video_output_signal_id(out), "3840x2160_59.94_other");
	EXPECT_EQ(video_output_signal_id(video_output_status{}), "none");

	audio_input_status ain{};
	ain.audioInStatus = true;
	ain.audioInIsPcm = true;
	ain.audioInBitDepth = 24;
	ain.audioInFs = 48000;
	ain.audioInChannelMap = 0x13;
	EXPECT_EQ(audio_input_signal_id(ain), "pcm_48k_24_0x13");
	ain.audioInIsPcm = false;
	ain.audioInFs = 192000;
	EXPECT_EQ(audio_input_signal_id(ain), "bitstream_192k_24_0x13");
	ain.audioInStatus = false;
	EXPECT_EQ(audio_input_signal_id(ain), "none");

	audio_output_status aout{};
	EXPECT_EQ(audio_output_signal_id(aout), "none");
	aout.audioOutCodec = "PCM";
	aout.audioOutFs = 44100;
	aout.audioOutBitDepth = 16;
	aout.audioOutChannelCount = 2;
	EXPECT_EQ(audio_output_signal_id(aout), "pcm_44.1k_16_2ch");
	aout.audioOutCodec = "TrueHD";
	aout.audioOutFs = 192000;
	aout.audioOutBitDepth = 24;
	aout.audioOutChannelCount = 8;
	EXPECT_EQ(audio_output_signal_id(aout), "truehd_192k_24_8ch");
}

TEST_F(MetricsTest, LatencySeriesCarryTheSignalIds)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_signal_ids");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);
	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink, std::chrono::seconds{60}, false), "");

	metrics.on_video_signal(uhd_input(), uhd_output(), hdr_status{});
	frame_metrics fm;
	fm.refreshRate(1.4);
	sample(fm, 20000, 5000, 0);
	fm.actualFrameRate = 23.976;
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.on_window(metric_stream::VIDEO, fm);
	metrics.flush();

	const auto text = scrape(prom);
	const std::string ids = R"(in_sig="3840x2160_23.976_yuv422_12",out_sig="3840x2160_23.976_p210")";
	EXPECT_EQ(value_of(text, R"(ezcapture_frame_latency_seconds_count{filter="test",)" + ids + R"(,stage="capture",stream="video"})"), 1.0) << text;
	EXPECT_NEAR(value_of(text, R"(ezcapture_video_measured_fps{filter="test",)" + ids + "}").value_or(-1), 23.976, 1e-9) << text;
	// the info series carries the same ids so it can be joined on them
	EXPECT_NE(text.find(R"(ezcapture_video_signal_info{filter="test",hdr="false",in_bit_depth="12")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"(in_saturation="Limited",in_sig="3840x2160_23.976_yuv422_12",out_bit_depth="10")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"x(out_resolution="3840x2160",out_sig="3840x2160_23.976_p210",out_subsampling="YUV 4:2:2",out_transfer_function="SMPTE ST 2084 (PQ)"} 1)x"), std::string::npos) << text;
	metrics.stop();
}

namespace
{
	// the value of the video info series that includes `label`
	std::optional<double> info_value(const std::string& text, const std::string& label)
	{
		const std::string name = "ezcapture_video_signal_info{";
		for (auto pos = text.find(name); pos != std::string::npos; pos = text.find(name, pos + 1))
		{
			const auto line = text.substr(pos, text.find('\n', pos) - pos);
			if (line.find(label) != std::string::npos)
			{
				return std::stod(line.substr(line.rfind(' ') + 1));
			}
		}
		return std::nullopt;
	}
}

TEST_F(MetricsTest, SignalSeriesAreRetainedWhenTheSignalDropsOut)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_retention");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);
	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink, std::chrono::seconds{60}, false), "");

	auto in = uhd_input();
	auto out = uhd_output();
	hdr_status hdr{};
	hdr.hdrOn = true;
	hdr.hdrMaxCLL = 1000;
	hdr.hdrMaxFALL = 400;
	hdr.hdrMaxDML = 4000;
	hdr.hdrMinDML = 0.005;

	frame_metrics fm;
	fm.refreshRate(1.4);
	sample(fm, 20000, 5000, 0);

	const std::string uhd = R"(in_sig="3840x2160_23.976_yuv422_12")";
	const std::string uhdLatency = R"(ezcapture_frame_latency_seconds_count{filter="test",in_sig="3840x2160_23.976_yuv422_12",out_sig="3840x2160_23.976_p210",stage="capture",stream="video"})";

	metrics.on_video_signal(in, out, hdr);
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.flush();
	auto text = scrape(prom);
	EXPECT_EQ(info_value(text, uhd), 1.0) << text;
	EXPECT_EQ(value_of(text, uhdLatency), 1.0) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_cll_nits{filter="test"})"), 1000.0) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_fall_nits{filter="test"})"), 400.0) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_mastering_luminance_nits{filter="test"})"), 4000.0) << text;
	EXPECT_NEAR(value_of(text, R"(ezcapture_hdr_min_mastering_luminance_nits{filter="test"})").value_or(-1), 0.005, 1e-9);

	// a change in detail that is not part of the ids replaces the info series, still exactly one per id
	hdr.hdrOn = false;
	metrics.on_video_signal(in, out, hdr);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;
	EXPECT_NE(text.find(R"(hdr="false")"), std::string::npos) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_cll_nits{filter="test"})"), 0.0) << text;
	hdr.hdrOn = true;
	metrics.on_video_signal(in, out, hdr);

	// the signal drops out, the previous signal is kept but is no longer current
	auto noSignal = in;
	noSignal.validSignal = false;
	metrics.on_video_signal(noSignal, out, hdr);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(info_value(text, uhd), 0.0) << text;
	EXPECT_EQ(info_value(text, R"(in_sig="none")"), 1.0) << text;
	EXPECT_EQ(value_of(text, uhdLatency), 1.0) << text;

	// nothing is removed within the retention period
	metrics.expire(std::chrono::steady_clock::now() + std::chrono::seconds{30});

	// so when it comes back the same series continue
	metrics.on_video_signal(in, out, hdr);
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(info_value(text, uhd), 1.0) << text;
	EXPECT_EQ(value_of(text, uhdLatency), 2.0) << text;
	// "none" never carried a frame so there is nothing worth keeping
	EXPECT_EQ(info_value(text, R"(in_sig="none")"), std::nullopt) << text;
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;

	// switch to 1080p, the UHD series stay until the retention period expires
	auto hdIn = in;
	hdIn.inX = 1920;
	hdIn.inY = 1080;
	metrics.on_video_signal(hdIn, out, hdr);
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.expire(std::chrono::steady_clock::now() + std::chrono::seconds{30});
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 2u) << text;
	EXPECT_EQ(info_value(text, uhd), 0.0) << text;
	EXPECT_EQ(info_value(text, R"(in_sig="1920x1080_23.976_yuv422_12")"), 1.0) << text;
	EXPECT_EQ(value_of(text, uhdLatency), 2.0) << text;

	metrics.expire(std::chrono::steady_clock::now() + std::chrono::seconds{61});
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;
	EXPECT_EQ(text.find(uhd), std::string::npos) << text;

	// a signal that comes back after it expired starts again from scratch
	metrics.on_video_signal(in, out, hdr);
	metrics.on_frame(metric_stream::VIDEO, fm);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(info_value(text, uhd), 1.0) << text;
	EXPECT_EQ(value_of(text, uhdLatency), 1.0) << text;

	// stopping removes everything so a shared endpoint does not report a stale signal
	metrics.stop();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 0u) << text;
	EXPECT_EQ(text.find("ezcapture_frame_latency_seconds_count{"), std::string::npos) << text;
	EXPECT_EQ(text.find("ezcapture_hdr_max_cll_nits{"), std::string::npos) << text;
}

TEST_F(MetricsTest, AudioSignalInfoDescribesInputAndOutput)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_audio_info");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);
	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink), "");

	audio_input_status in{};
	in.audioInStatus = true;
	in.audioInIsPcm = true;
	in.audioInBitDepth = 24;
	in.audioInFs = 48000;
	in.audioInChannelMap = 0x13;
	audio_output_status out{};
	out.audioOutCodec = "PCM";
	out.audioOutFs = 48000;
	out.audioOutBitDepth = 24;
	out.audioOutChannelCount = 8;
	out.audioOutChannelLayout = "FL FR FC LFE BL BR SL SR";

	metrics.on_audio_signal(in, out);
	metrics.flush();
	const auto text = scrape(prom);
	EXPECT_EQ(value_of(text, R"(ezcapture_audio_signal_info{filter="test",in_bit_depth="24",in_channel_map="0x13",in_pcm="true",in_sample_rate="48000",in_sig="pcm_48k_24_0x13",out_bit_depth="24",out_channel_layout="FL FR FC LFE BL BR SL SR",out_channels="8",out_codec="PCM",out_sample_rate="48000",out_sig="pcm_48k_24_8ch"})"), 1.0) << text;
	metrics.stop();
}
