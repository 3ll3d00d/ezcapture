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

#define NOMINMAX
#include <streams.h>

#include "status_snapshot.h"
#include "web_json.h"

#include <set>
#include <string>

namespace
{
	// every "key": in a document, in order, which is enough to compare the shape of two documents
	std::vector<std::string> keys_of(const std::string& json)
	{
		std::vector<std::string> keys;
		bool inString = false;
		std::string current;
		for (size_t i = 0; i < json.size(); ++i)
		{
			const char c = json[i];
			if (inString)
			{
				if (c == '\\')
				{
					current.push_back(json[++i]);
				}
				else if (c == '"')
				{
					inString = false;
					size_t j = i + 1;
					if (j < json.size() && json[j] == ':')
					{
						keys.push_back(current);
					}
				}
				else
				{
					current.push_back(c);
				}
			}
			else if (c == '"')
			{
				inString = true;
				current.clear();
			}
		}
		return keys;
	}

	bool has(const std::string& json, const std::string& fragment)
	{
		return json.find(fragment) != std::string::npos;
	}

	struct signal_fixture
	{
		video_input_status vin{};
		video_output_status vout{};
		hdr_status hdr{};
		dv_status dv{};
		audio_input_status ain{};
		audio_output_status aout{};

		signal_fixture()
		{
			vin.validSignal = true;
			vin.signalStatus = "Locked";
			vin.inX = 3840;
			vin.inY = 2160;
			vin.inFps = 24000.0 / 1001.0;
			vout.outTransferFunction = "REC.709";
			ain.audioInStatus = true;
			aout.audioOutCodec = "PCM";
			aout.audioOutChannelCount = 2;
		}

		status_summary summary() const
		{
			return status_snapshot::Summarise(vin, vout, hdr, dv, ain, aout);
		}
	};
}

TEST(WebJson, EscapesStrings)
{
	std::string out;
	web_json::append_quoted(out, "a\"b\\c\nd\x01");
	EXPECT_EQ(out, R"("a\"b\\c\nd\u0001")");
}

TEST(WebJson, WritesNestedDocumentsWithCommas)
{
	web_json::writer w;
	w.begin_object();
	w.key("a").value(1);
	w.key("b").begin_array().value(true).null().value("x").end_array();
	w.key("c").begin_object().end_object();
	w.key("d").value(std::optional<int>{});
	w.key("e").value(23.976);
	w.end_object();
	EXPECT_EQ(w.str(), R"({"a":1,"b":[true,null,"x"],"c":{},"d":null,"e":23.976})");
}

TEST(WebJson, NonFiniteDoublesAreNull)
{
	web_json::writer w;
	w.begin_array().value(std::numeric_limits<double>::infinity()).value(std::nan("")).end_array();
	EXPECT_EQ(w.str(), "[null,null]");
}

TEST(WebJson, Slugs)
{
	EXPECT_EQ(web_json::slug("YUV 4:2:2"), "yuv_422");
	EXPECT_EQ(web_json::slug("No Signal"), "no_signal");
	EXPECT_EQ(web_json::slug("REC.709"), "rec709");
	EXPECT_EQ(web_json::slug("SMPTE ST 2084 (PQ)"), "smpte_st_2084_pq");
	EXPECT_EQ(web_json::slug("?"), "");
	EXPECT_EQ(web_json::slug(""), "");
	EXPECT_EQ(web_json::slug("  Locked  "), "locked");
}

TEST(WebJson, Utf8)
{
	EXPECT_EQ(web_json::to_utf8(L"50 Hz \u00b0"), "50 Hz \xc2\xb0");
	EXPECT_EQ(web_json::to_utf8(L"\U0001F600"), "\xf0\x9f\x98\x80");
}

TEST(WebJson, ParsesScalars)
{
	auto t = web_json::parse_scalar(" true ");
	ASSERT_TRUE(t);
	EXPECT_EQ(t->type, web_json::scalar::BOOLEAN);
	EXPECT_TRUE(t->b);

	auto n = web_json::parse_scalar("-12.5e1");
	ASSERT_TRUE(n);
	EXPECT_EQ(n->type, web_json::scalar::NUMBER);
	EXPECT_DOUBLE_EQ(n->n, -125.0);

	auto s = web_json::parse_scalar(R"("a\u00e9\n")");
	ASSERT_TRUE(s);
	EXPECT_EQ(s->type, web_json::scalar::STRING);
	EXPECT_EQ(s->s, "a\xc3\xa9\n");

	EXPECT_TRUE(web_json::parse_scalar("null"));
	EXPECT_FALSE(web_json::parse_scalar("01"));
	EXPECT_FALSE(web_json::parse_scalar("1."));
	EXPECT_FALSE(web_json::parse_scalar("tru"));
	EXPECT_FALSE(web_json::parse_scalar("true false"));
	EXPECT_FALSE(web_json::parse_scalar(R"("\ud800")"));
	EXPECT_FALSE(web_json::parse_scalar(""));
}

TEST(WebJson, ParsesMembers)
{
	auto v = web_json::parse_member(R"( {"other": "x", "value" : 3} )", "value");
	ASSERT_TRUE(v);
	EXPECT_EQ(v->type, web_json::scalar::NUMBER);
	EXPECT_DOUBLE_EQ(v->n, 3.0);

	EXPECT_FALSE(web_json::parse_member(R"({"other": 1})", "value"));
	EXPECT_FALSE(web_json::parse_member(R"({"value": {"x": 1}})", "value"));
	EXPECT_FALSE(web_json::parse_member(R"({"value": 1,})", "value"));
	EXPECT_FALSE(web_json::parse_member(R"({"value": 1} x)", "value"));
	EXPECT_FALSE(web_json::parse_member("true", "value"));
	EXPECT_FALSE(web_json::parse_member("{}", "value"));
}

TEST(StatusSummary, NoSignal)
{
	const auto s = status_snapshot::Summarise({}, {}, {}, {}, {}, {});
	EXPECT_FALSE(s.signal);
	EXPECT_EQ(s.videoMode, "");
	EXPECT_EQ(s.dynamicRange, "");
	EXPECT_EQ(s.audio, "none");
	EXPECT_FALSE(s.audioChannels.has_value());
}

TEST(StatusSummary, Sdr)
{
	signal_fixture f;
	const auto s = f.summary();
	EXPECT_TRUE(s.signal);
	EXPECT_EQ(s.videoMode, "3840x2160p23.976");
	EXPECT_EQ(s.dynamicRange, "sdr");
	EXPECT_EQ(s.audio, "pcm");
	EXPECT_EQ(s.audioChannels, 2);
}

TEST(StatusSummary, WholeFrameRatesHaveNoDecimals)
{
	signal_fixture f;
	f.vin.inFps = 50.0;
	EXPECT_EQ(f.summary().videoMode, "3840x2160p50");
	f.vin.inFps = 60000.0 / 1001.0;
	EXPECT_EQ(f.summary().videoMode, "3840x2160p59.94");
}

TEST(StatusSummary, Hdr10FromInfoFrameOrPq)
{
	signal_fixture f;
	f.hdr.hdrOn = true;
	EXPECT_EQ(f.summary().dynamicRange, "hdr10");
	f.hdr.hdrOn = false;
	f.vout.outTransferFunction = "SMPTE ST 2084 (PQ)";
	EXPECT_EQ(f.summary().dynamicRange, "hdr10");
}

TEST(StatusSummary, Hlg)
{
	signal_fixture f;
	f.hdr.hdrOn = true;
	f.vout.outTransferFunction = "HLG";
	EXPECT_EQ(f.summary().dynamicRange, "hlg");
}

TEST(StatusSummary, DolbyVision)
{
	signal_fixture f;
	f.dv.state = DV_LOCKED;
	EXPECT_EQ(f.summary().dynamicRange, "dv");
	f.dv.state = DV_NO_METADATA;
	EXPECT_EQ(f.summary().dynamicRange, "dv");
	f.dv.state = DV_LLDV;
	f.hdr.hdrOn = true;
	EXPECT_EQ(f.summary().dynamicRange, "lldv");
	f.dv.state = DV_SEARCHING;
	f.hdr.hdrOn = false;
	EXPECT_EQ(f.summary().dynamicRange, "sdr");
}

TEST(StatusSummary, AudioCodecs)
{
	EXPECT_EQ(status_snapshot::AudioCodecId("PCM"), "pcm");
	EXPECT_EQ(status_snapshot::AudioCodecId("AC3"), "ac3");
	EXPECT_EQ(status_snapshot::AudioCodecId("DTS"), "dts");
	EXPECT_EQ(status_snapshot::AudioCodecId("DTSHD"), "dts_hd");
	EXPECT_EQ(status_snapshot::AudioCodecId("EAC3"), "eac3");
	EXPECT_EQ(status_snapshot::AudioCodecId("TrueHD"), "truehd");
	EXPECT_EQ(status_snapshot::AudioCodecId("Unidentified"), "unknown");
	EXPECT_EQ(status_snapshot::AudioCodecId("PAUSE_OR_NULL"), "none");
	EXPECT_EQ(status_snapshot::AudioCodecId(""), "none");
	for (const auto& name : codecNames)
	{
		EXPECT_FALSE(status_snapshot::AudioCodecId(name).empty()) << name;
	}
}

TEST(StatusSummary, NoAudioInputMeansNone)
{
	signal_fixture f;
	f.ain.audioInStatus = false;
	EXPECT_EQ(f.summary().audio, "none");
	EXPECT_FALSE(f.summary().audioChannels.has_value());
}

TEST(StatusSnapshot, VersionChangesOnEveryUpdate)
{
	status_snapshot snap;
	EXPECT_EQ(snap.Version(), 0u);
	video_input_status vin{};
	snap.Reload(&vin);
	EXPECT_EQ(snap.Version(), 1u);
	double fps = 23.976;
	snap.ReloadVfps(&fps);
	EXPECT_EQ(snap.Version(), 2u);
	snap.SetRunState(filter_run_state::RUNNING);
	EXPECT_EQ(snap.Version(), 3u);
	snap.SetRunState(filter_run_state::RUNNING);
	EXPECT_EQ(snap.Version(), 3u);
	snap.OnFilterStopped();
	EXPECT_EQ(snap.Version(), 4u);
}

TEST(StatusSnapshot, EmptyDocumentHasNullsNotSentinels)
{
	status_snapshot snap;
	uint64_t version = 99;
	const auto json = snap.ToJson("magewell-abc", "Magewell", &version);
	EXPECT_EQ(version, 0u);
	EXPECT_TRUE(has(json, R"("filter":{"id":"magewell-abc","name":"Magewell","state":"stopped"})")) << json;
	EXPECT_TRUE(has(json, R"("summary":{"signal":false,"video_mode":null,"dynamic_range":null,"audio":"none","audio_channels":null})")) << json;
	EXPECT_TRUE(has(json, R"("width":null)")) << json;
	EXPECT_TRUE(has(json, R"("fan_rpm":null)")) << json;
	EXPECT_TRUE(has(json, R"("primaries":null)")) << json;
	EXPECT_TRUE(has(json, R"("updated":null)")) << json;
	EXPECT_FALSE(has(json, "-1")) << json;
}

TEST(StatusSnapshot, DocumentShapeDoesNotDependOnTheSignal)
{
	status_snapshot empty;
	const auto emptyKeys = keys_of(empty.ToJson("id", "name"));

	status_snapshot full;
	signal_fixture f;
	f.hdr.hdrOn = true;
	// l1 and lldv are null unless present, like primaries, but share child names with other objects so leave them out
	f.dv.state = DV_LOCKED;
	full.Reload(&f.vin);
	full.Reload(&f.vout);
	full.Reload(&f.hdr);
	full.Reload(&f.dv);
	full.Reload(&f.ain);
	full.Reload(&f.aout);
	latency_stats lat{"Capture", 10000, 12500.0, 20000};
	full.ReloadV1(&lat);
	full.ReloadA1(&lat);
	const auto json = full.ToJson("id", "name");
	auto fullKeys = keys_of(json);

	// the children of objects that are null when absent and the latency entries
	const std::set<std::string> optionalChildren{
		"rx", "ry", "gx", "gy", "bx", "by", "wpx", "wpy",
		"stage", "min_ms", "mean_ms", "max_ms"
	};
	std::vector<std::string> fullRequired;
	for (const auto& k : fullKeys)
	{
		if (!optionalChildren.contains(k))
		{
			fullRequired.push_back(k);
		}
	}
	EXPECT_EQ(emptyKeys, fullRequired) << json;

	EXPECT_TRUE(has(json, R"("video_mode":"3840x2160p23.976")")) << json;
	EXPECT_TRUE(has(json, R"("dynamic_range":"dv")")) << json;
	EXPECT_TRUE(has(json, R"({"stage":"capture","min_ms":1,"mean_ms":1.25,"max_ms":2})")) << json;
	EXPECT_TRUE(has(json, R"("status":"locked","status_text":"Locked")")) << json;
}

TEST(StatusSnapshot, Controls)
{
	status_snapshot snap;
	snap.ReloadControls(true, false, 3, 1, true, false);
	snap.ReloadMetricsControls(true, "0.0.0.0", 9464, "Serving");
	const auto json = snap.ToJson("id", "name");
	EXPECT_TRUE(has(json,
		R"("controls":{"refresh_rate_switch":true,"hdr_profile_switch":false,"hdr_profile":3,"sdr_profile":1,"high_thread_priority":true,"audio_capture":false,"metrics_enabled":true})"))
		<< json;
	EXPECT_TRUE(has(json, R"("metrics":{"enabled":true,"bind_address":"0.0.0.0","port":9464,"status_text":"Serving"})"))
		<< json;
	const auto c = snap.Controls();
	EXPECT_EQ(c.hdrProfile, 3u);
	EXPECT_TRUE(c.metricsEnabled);
}
