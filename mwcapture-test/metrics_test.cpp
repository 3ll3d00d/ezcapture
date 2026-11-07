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
	const std::string video = R"(filter="test",stage="capture",stream="video")";

	EXPECT_EQ(value_of(text, "ezcapture_frame_latency_seconds_count{" + video + "}"), 2.0) << text;
	EXPECT_NEAR(value_of(text, "ezcapture_frame_latency_seconds_sum{" + video + "}").value_or(-1), 0.006, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_seconds_sum{filter="test",stage="conversion",stream="video"})")
	            .value_or(-1), 0.002, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_seconds_sum{filter="test",stage="host",stream="video"})")
	            .value_or(-1), 0.004, 1e-9);

	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",stage="capture",stat="min",stream="video"})")
	            .value_or(-1), 0.002, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",stage="capture",stat="mean",stream="video"})")
	            .value_or(-1), 0.003, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_frame_latency_window_seconds{filter="test",stage="capture",stat="max",stream="video"})")
	            .value_or(-1), 0.004, 1e-9);
	EXPECT_NEAR(value_of(text, R"(ezcapture_video_measured_fps{filter="test"})").value_or(-1), 59.94, 1e-9);

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
	EXPECT_EQ(value_of(text, R"(ezcapture_frame_latency_seconds_count{filter="test",stage="capture",stream="audio"})"), 1.0)
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

TEST_F(MetricsTest, VideoSignalInfoTracksTheCurrentSignalOnly)
{
	auto sink = quill::Frontend::create_or_get_sink<quill::PrometheusSink>("metrics_test_video_info");
	auto& prom = static_cast<quill::PrometheusSink&>(*sink);
	capture_metrics metrics;
	ASSERT_EQ(metrics.start("test", sink), "");

	auto in = uhd_input();
	auto out = uhd_output();
	hdr_status hdr{};
	hdr.hdrOn = true;
	hdr.hdrMaxCLL = 1000;
	hdr.hdrMaxFALL = 400;
	hdr.hdrMaxDML = 4000;
	hdr.hdrMinDML = 0.005;

	metrics.on_video_signal(in, out, hdr);
	metrics.flush();
	auto text = scrape(prom);
	const std::string uhd = R"(in_resolution="3840x2160")";
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;
	EXPECT_NE(text.find(uhd), std::string::npos) << text;
	EXPECT_NE(text.find(R"(in_fps="23.976")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"(out_pixel_format="P210")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"(hdr="true")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"(signal="locked")"), std::string::npos) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_cll_nits{filter="test"})"), 1000.0) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_fall_nits{filter="test"})"), 400.0) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_mastering_luminance_nits{filter="test"})"), 4000.0) << text;
	EXPECT_NEAR(value_of(text, R"(ezcapture_hdr_min_mastering_luminance_nits{filter="test"})").value_or(-1), 0.005, 1e-9);

	// switch to 1080p SDR, the UHD series must disappear
	auto hdIn = in;
	hdIn.inX = 1920;
	hdIn.inY = 1080;
	hdr.hdrOn = false;
	metrics.on_video_signal(hdIn, out, hdr);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;
	EXPECT_EQ(text.find(uhd), std::string::npos) << text;
	EXPECT_NE(text.find(R"(in_resolution="1920x1080")"), std::string::npos) << text;
	EXPECT_NE(text.find(R"(hdr="false")"), std::string::npos) << text;
	EXPECT_EQ(value_of(text, R"(ezcapture_hdr_max_cll_nits{filter="test"})"), 0.0) << text;

	// an unchanged signal publishes nothing new
	metrics.on_video_signal(hdIn, out, hdr);
	metrics.flush();
	EXPECT_EQ(count_of(scrape(prom), "ezcapture_video_signal_info{"), 1u);

	// and back again, a previously removed series must come back
	hdr.hdrOn = true;
	metrics.on_video_signal(in, out, hdr);
	metrics.flush();
	text = scrape(prom);
	EXPECT_EQ(count_of(text, "ezcapture_video_signal_info{"), 1u) << text;
	EXPECT_NE(text.find(uhd), std::string::npos) << text;

	// stopping removes the info so a shared endpoint does not report a stale signal
	metrics.stop();
	EXPECT_EQ(count_of(scrape(prom), "ezcapture_video_signal_info{"), 0u);
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
	EXPECT_EQ(value_of(text, R"(ezcapture_audio_signal_info{filter="test",in_bit_depth="24",in_channel_map="0x13",in_pcm="true",in_sample_rate="48000",out_bit_depth="24",out_channel_layout="FL FR FC LFE BL BR SL SR",out_channels="8",out_codec="PCM",out_sample_rate="48000",signal="locked"})"), 1.0) << text;
	metrics.stop();
}
