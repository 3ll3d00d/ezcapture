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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <streams.h>

#include "status_snapshot.h"
#include "web_api.h"
#include "web_json.h"
#include "web_notifier.h"
#include "web_server.h"

#include <map>
#include <thread>
#include <set>

#pragma comment(lib, "ws2_32.lib")
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
		R"("controls":{"refresh_rate_switch":true,"hdr_profile_switch":false,"hdr_profile":3,"sdr_profile":1,"high_thread_priority":true,"audio_capture":false,"metrics_enabled":true,"pending_reload":false})"))
		<< json;
	EXPECT_TRUE(has(json, R"("metrics":{"enabled":true,"bind_address":"0.0.0.0","port":9464,"status_text":"Serving"})"))
		<< json;
	const auto c = snap.Controls();
	EXPECT_EQ(c.hdrProfile, 3u);
	EXPECT_TRUE(c.metricsEnabled);
}

namespace
{
	struct fake_target : web_target
	{
		status_snapshot snap;
		std::map<std::string, control_value, std::less<>> values{
			{"refresh_rate_switch", true}, {"hdr_profile_switch", false}, {"hdr_profile", int64_t{3}},
			{"sdr_profile", int64_t{1}}, {"high_thread_priority", true}, {"audio_capture", true},
			{"metrics_enabled", false}, {"metrics_port", int64_t{9464}},
			{"metrics_bind_address", std::string{"127.0.0.1"}}
		};
		bool failSet{false};

		const status_snapshot& Status() const override { return snap; }
		control_value GetControl(const control_def& def) const override { return values.find(def.name)->second; }

		bool SetControl(const control_def& def, const control_value& value) override
		{
			if (failSet)
			{
				return false;
			}
			values.find(def.name)->second = value;
			return true;
		}

		bool ControlPending(const control_def& def) const override
		{
			return def.name == "audio_capture" && !std::get<bool>(values.find(def.name)->second);
		}
	};

	http_request get(std::string path, std::string host = "127.0.0.1:9474", std::string auth = {})
	{
		http_request r;
		r.method = "GET";
		r.path = std::move(path);
		r.host = std::move(host);
		r.authorization = std::move(auth);
		return r;
	}

	std::string header_of(const http_response& r, const std::string& name)
	{
		for (const auto& [k, v] : r.headers)
		{
			if (k == name)
			{
				return v;
			}
		}
		return {};
	}

	constexpr auto token = "0123456789abcdef0123456789abcdef";
}

TEST(WebApi, LoopbackHosts)
{
	EXPECT_TRUE(web_api::IsLoopbackHost("127.0.0.1:9474", 9474));
	EXPECT_TRUE(web_api::IsLoopbackHost("localhost:9474", 9474));
	EXPECT_TRUE(web_api::IsLoopbackHost("LOCALHOST", 9474));
	EXPECT_TRUE(web_api::IsLoopbackHost("[::1]:9474", 9474));
	EXPECT_FALSE(web_api::IsLoopbackHost("127.0.0.1:9475", 9474));
	EXPECT_FALSE(web_api::IsLoopbackHost("evil.example:9474", 9474));
	EXPECT_FALSE(web_api::IsLoopbackHost("localhost.evil.example:9474", 9474));
	EXPECT_FALSE(web_api::IsLoopbackHost("", 9474));
	EXPECT_FALSE(web_api::IsLoopbackHost("[::1", 9474));

	EXPECT_TRUE(web_api::IsLoopbackAddress("127.0.0.1"));
	EXPECT_TRUE(web_api::IsLoopbackAddress("::1"));
	EXPECT_TRUE(web_api::IsLoopbackAddress("[::1]"));
	EXPECT_FALSE(web_api::IsLoopbackAddress("0.0.0.0"));
	EXPECT_FALSE(web_api::IsLoopbackAddress("192.168.1.10"));
}

TEST(WebApi, BearerTokens)
{
	EXPECT_EQ(web_api::BearerToken("Bearer abc"), "abc");
	EXPECT_EQ(web_api::BearerToken("bearer  abc "), "abc");
	EXPECT_EQ(web_api::BearerToken("Basic abc"), "");
	EXPECT_EQ(web_api::BearerToken(""), "");
	EXPECT_TRUE(web_api::TokenMatches("abc", "abc"));
	EXPECT_FALSE(web_api::TokenMatches("abd", "abc"));
	EXPECT_FALSE(web_api::TokenMatches("ab", "abc"));
	EXPECT_FALSE(web_api::TokenMatches("abcd", "abc"));
	EXPECT_FALSE(web_api::TokenMatches("", ""));
}

TEST(WebApi, ServesThePageToAnyone)
{
	web_api api{false, 9474, "1.0"};
	auto r = api.Handle(get("/", "evil.example"));
	EXPECT_EQ(r.status, 200);
	EXPECT_EQ(header_of(r, "Content-Encoding"), "gzip");
	EXPECT_EQ(r.contentType, "text/html; charset=utf-8");
	ASSERT_GT(r.body.size(), 2u);
	EXPECT_EQ(static_cast<unsigned char>(r.body[0]), 0x1f);

	auto req = get("/index.html");
	req.ifNoneMatch = header_of(r, "ETag");
	EXPECT_EQ(api.Handle(req).status, 304);
}

TEST(WebApi, LoopbackReadsNeedNoTokenFromALocalName)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	EXPECT_EQ(api.Handle(get("/api/v1/filters/magewell-abc/state")).status, 200);
	EXPECT_EQ(api.Handle(get("/api/v1/info", "localhost:9474")).status, 200);
	// a DNS rebinding page gets the attacker's host name
	EXPECT_EQ(api.Handle(get("/api/v1/filters/magewell-abc/state", "evil.example:9474")).status, 401);
	// a token works from any host name
	EXPECT_EQ(api.Handle(get("/api/v1/filters/magewell-abc/state", "evil.example:9474",
	                         std::string{"Bearer "} + token)).status, 200);
	const auto bad = api.Handle(get("/api/v1/filters/magewell-abc/state", "127.0.0.1:9474", "Bearer nope"));
	EXPECT_EQ(bad.status, 401);
	EXPECT_EQ(header_of(bad, "WWW-Authenticate"), "Bearer");
	EXPECT_TRUE(has(bad.body, "bad_token"));
}

TEST(WebApi, NetworkReadsNeedTheToken)
{
	web_api api{false, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	EXPECT_EQ(api.Handle(get("/api/v1/filters/magewell-abc/state")).status, 401);
	EXPECT_EQ(api.Handle(get("/api/v1/info")).status, 401);
	const auto auth = std::string{"Bearer "} + token;
	EXPECT_EQ(api.Handle(get("/api/v1/filters/default/state", "capture-pc:9474", auth)).status, 200);
	const auto info = api.Handle(get("/api/v1/info", "192.168.1.5:9474", auth));
	EXPECT_EQ(info.status, 200);
	EXPECT_TRUE(has(info.body, R"("filters":[{"id":"magewell-abc","name":"Magewell"}])")) << info.body;
	EXPECT_TRUE(has(info.body, R"("api_version":1)")) << info.body;
}

TEST(WebApi, StateUsesETags)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	auto first = api.Handle(get("/api/v1/filters/default/state"));
	ASSERT_EQ(first.status, 200);
	EXPECT_EQ(header_of(first, "Cache-Control"), "no-store");
	EXPECT_TRUE(has(first.body, R"("id":"magewell-abc")"));
	const auto etag = header_of(first, "ETag");
	ASSERT_FALSE(etag.empty());

	auto again = get("/api/v1/filters/default/state");
	again.ifNoneMatch = etag;
	auto cached = api.Handle(again);
	EXPECT_EQ(cached.status, 304);
	EXPECT_TRUE(cached.body.empty());

	double fps = 50.0;
	target.snap.ReloadVfps(&fps);
	auto changed = api.Handle(again);
	EXPECT_EQ(changed.status, 200);
	EXPECT_NE(header_of(changed, "ETag"), etag);
}

TEST(WebApi, ETagsDifferBetweenFilterInstances)
{
	web_api api{true, 9474, "1.0"};
	fake_target a;
	auto reg = api.Register(&a, "magewell-abc", "Magewell", token);
	const auto etag = header_of(api.Handle(get("/api/v1/filters/default/state")), "ETag");
	api.Unregister(reg);

	fake_target b;
	api.Register(&b, "magewell-abc", "Magewell", token);
	auto req = get("/api/v1/filters/default/state");
	req.ifNoneMatch = etag;
	EXPECT_EQ(api.Handle(req).status, 200);
}

TEST(WebApi, RoutesAndErrors)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	EXPECT_EQ(api.Handle(get("/api/v1/filters/nope/state")).status, 404);
	EXPECT_EQ(api.Handle(get("/api/v1/filters/magewell-abc/nope")).status, 404);
	EXPECT_EQ(api.Handle(get("/api/v2/info")).status, 404);
	EXPECT_EQ(api.Handle(get("/favicon.ico")).status, 404);
	auto post = get("/api/v1/filters/magewell-abc/state");
	post.method = "POST";
	EXPECT_EQ(api.Handle(post).status, 405);
	auto options = get("/api/v1/info");
	options.method = "OPTIONS";
	const auto preflight = api.Handle(options);
	EXPECT_EQ(preflight.status, 405);
	EXPECT_EQ(header_of(preflight, "Access-Control-Allow-Origin"), "");
}

TEST(WebApi, IdsAreUniqueAndDefaultIsTheFirst)
{
	web_api api{true, 9474, "1.0"};
	fake_target a, b, c;
	auto ra = api.Register(&a, "decklink-1", "A", token);
	auto rb = api.Register(&b, "decklink-1", "B", token);
	auto rc = api.Register(&c, "default", "C", token);
	EXPECT_EQ(ra->id, "decklink-1");
	EXPECT_EQ(rb->id, "decklink-1-2");
	EXPECT_EQ(rc->id, "default-2");
	EXPECT_TRUE(has(api.Handle(get("/api/v1/filters/default/state")).body, R"("name":"A")"));
	api.Unregister(ra);
	EXPECT_TRUE(has(api.Handle(get("/api/v1/filters/default/state")).body, R"("name":"B")"));
	EXPECT_EQ(api.Handle(get("/api/v1/filters/decklink-1/state")).status, 404);
	EXPECT_EQ(api.Count(), 2u);
}

namespace
{
	// a raw HTTP/1.1 exchange with a server on 127.0.0.1, returns everything it sent back
	std::string http_exchange(uint16_t port, const std::string& request)
	{
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		std::string response;
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
		{
			send(s, request.data(), static_cast<int>(request.size()), 0);
			char buf[4096];
			int n;
			while ((n = recv(s, buf, sizeof(buf), 0)) > 0)
			{
				response.append(buf, static_cast<size_t>(n));
			}
		}
		closesocket(s);
		WSACleanup();
		return response;
	}

	constexpr uint16_t testPort{19474};
}

TEST(WebServer, AnswersOverHttpAndIsShared)
{
	std::string error;
	auto server = web_server::Acquire("127.0.0.1", testPort, "1.2.3", error);
	ASSERT_TRUE(server) << error;
	EXPECT_EQ(server->Url(), "http://127.0.0.1:19474/");
	auto same = web_server::Acquire("127.0.0.1", testPort, "1.2.3", error);
	EXPECT_EQ(server.get(), same.get());
	same.reset();

	fake_target target;
	auto reg = server->Api().Register(&target, "magewell-abc", "Magewell", token);

	const auto state = http_exchange(testPort,
		"GET /api/v1/filters/default/state HTTP/1.1\r\nHost: 127.0.0.1:19474\r\nConnection: close\r\n\r\n");
	EXPECT_TRUE(state.starts_with("HTTP/1.1 200")) << state;
	EXPECT_TRUE(has(state, "Content-Type: application/json; charset=utf-8")) << state;
	EXPECT_TRUE(has(state, R"("id":"magewell-abc")")) << state;

	const auto rebound = http_exchange(testPort,
		"GET /api/v1/info HTTP/1.1\r\nHost: evil.example:19474\r\nConnection: close\r\n\r\n");
	EXPECT_TRUE(rebound.starts_with("HTTP/1.1 401")) << rebound;

	const auto big = http_exchange(testPort,
		"POST /api/v1/info HTTP/1.1\r\nHost: 127.0.0.1:19474\r\nContent-Length: 5000\r\nConnection: close\r\n\r\n" +
		std::string(5000, 'x'));
	EXPECT_TRUE(big.starts_with("HTTP/1.1 413")) << big;

	server->Api().Unregister(reg);
	server.reset();

	// the port is released with the last user
	auto again = web_server::Acquire("127.0.0.1", testPort, "1.2.3", error);
	EXPECT_TRUE(again) << error;
}

TEST(WebServer, ReportsAPortInUse)
{
	std::string error;
	auto first = web_server::Acquire("127.0.0.1", testPort + 1, "1", error);
	ASSERT_TRUE(first) << error;
	// a different address string for the same port is a different server, so the bind fails
	auto second = web_server::Acquire("0.0.0.0", testPort + 1, "1", error);
	EXPECT_FALSE(second);
	EXPECT_TRUE(has(error, "Unable to listen on 0.0.0.0:19475")) << error;
}

TEST(WebServer, TokensAreRandomHex)
{
	const auto a = web_server::NewToken();
	const auto b = web_server::NewToken();
	EXPECT_EQ(a.size(), 32u);
	EXPECT_NE(a, b);
	EXPECT_EQ(a.find_first_not_of("0123456789abcdef"), std::string::npos);
}

namespace
{
	http_request put(std::string path, std::string body, std::string auth = std::string{"Bearer "} + token,
	                 std::string host = "127.0.0.1:9474")
	{
		auto r = get(std::move(path), std::move(host), std::move(auth));
		r.method = "PUT";
		r.body = std::move(body);
		return r;
	}

	const control_value rejected{std::string{"<rejected>"}};

	control_value convert(std::string_view name, std::string_view json, std::string* errorOut = nullptr)
	{
		std::string error;
		auto v = to_control_value(*find_control(name), *web_json::parse_scalar(json), error);
		if (errorOut) *errorOut = error;
		return v ? *v : rejected;
	}
}

TEST(WebControls, LenientBooleans)
{
	for (const auto* on : {"true", "1", R"("on")", R"("ON")", R"("true")", R"("yes")", R"("1")"})
	{
		EXPECT_EQ(convert("refresh_rate_switch", on), control_value{true}) << on;
	}
	for (const auto* off : {"false", "0", R"("off")", R"("False")", R"("no")", R"("0")"})
	{
		EXPECT_EQ(convert("refresh_rate_switch", off), control_value{false}) << off;
	}
	std::string error;
	EXPECT_EQ(convert("refresh_rate_switch", "2", &error), rejected);
	EXPECT_EQ(error, "refresh_rate_switch must be true or false");
	EXPECT_EQ(convert("refresh_rate_switch", "null"), rejected);
}

TEST(WebControls, IntegersAreWholeAndInRange)
{
	EXPECT_EQ(convert("hdr_profile", "7"), control_value{int64_t{7}});
	EXPECT_EQ(convert("hdr_profile", R"("7")"), control_value{int64_t{7}});
	EXPECT_EQ(convert("hdr_profile", "7.0"), control_value{int64_t{7}});
	std::string error;
	EXPECT_EQ(convert("hdr_profile", "7.5", &error), rejected);
	EXPECT_EQ(error, "hdr_profile must be a whole number");
	EXPECT_EQ(convert("hdr_profile", "1001", &error), rejected);
	EXPECT_EQ(error, "hdr_profile must be between 0 and 1000");
	EXPECT_EQ(convert("metrics_port", "0"), rejected);
	EXPECT_EQ(convert("metrics_port", "1e30"), rejected);
	EXPECT_EQ(convert("hdr_profile", "true"), rejected);
}

TEST(WebControls, Addresses)
{
	EXPECT_EQ(convert("metrics_bind_address", R"("0.0.0.0")"), control_value{std::string{"0.0.0.0"}});
	EXPECT_EQ(convert("metrics_bind_address", R"("[::1]")"), control_value{std::string{"[::1]"}});
	EXPECT_EQ(convert("metrics_bind_address", R"("")"), rejected);
	EXPECT_EQ(convert("metrics_bind_address", R"("1.2.3.4; rm")"), rejected);
	EXPECT_EQ(convert("metrics_bind_address", "1"), rejected);
}

TEST(WebControls, ListsEveryControl)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	target.values["audio_capture"] = false;
	api.Register(&target, "magewell-abc", "Magewell", token);
	const auto r = api.Handle(get("/api/v1/filters/default/controls"));
	ASSERT_EQ(r.status, 200);
	for (const auto& def : web_controls())
	{
		EXPECT_TRUE(has(r.body, std::format(R"("name":"{}")", def.name))) << def.name;
	}
	EXPECT_TRUE(has(r.body,
		R"({"name":"hdr_profile","type":"integer","value":3,"min":0,"max":1000,"applies":"now","pending_reload":false})"))
		<< r.body;
	EXPECT_TRUE(has(r.body,
		R"({"name":"audio_capture","type":"boolean","value":false,"applies":"next_load","pending_reload":true})"))
		<< r.body;
	EXPECT_TRUE(r.body.ends_with(R"("pending_reload":true})")) << r.body;
}

TEST(WebControls, GetAndSetOneControl)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	auto r = api.Handle(get("/api/v1/filters/default/controls/refresh_rate_switch"));
	ASSERT_EQ(r.status, 200);
	EXPECT_TRUE(has(r.body, R"("value":true)")) << r.body;

	r = api.Handle(put("/api/v1/filters/default/controls/refresh_rate_switch", R"({"value": "off"})"));
	ASSERT_EQ(r.status, 200) << r.body;
	EXPECT_TRUE(has(r.body, R"("value":false)")) << r.body;
	EXPECT_EQ(target.values["refresh_rate_switch"], control_value{false});

	auto post = put("/api/v1/filters/magewell-abc/controls/hdr_profile", R"({"value": 12})");
	post.method = "POST";
	r = api.Handle(post);
	ASSERT_EQ(r.status, 200) << r.body;
	EXPECT_EQ(target.values["hdr_profile"], control_value{int64_t{12}});
}

TEST(WebControls, ChangesAlwaysNeedTheToken)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	// a loopback host is enough to read, never to write
	EXPECT_EQ(api.Handle(put("/api/v1/filters/default/controls/refresh_rate_switch", R"({"value":false})", "")).status,
	          401);
	EXPECT_EQ(api.Handle(put("/api/v1/filters/default/controls/refresh_rate_switch", R"({"value":false})",
	                         "Bearer wrong")).status, 401);
	EXPECT_EQ(target.values["refresh_rate_switch"], control_value{true});
}

TEST(WebControls, BadRequests)
{
	web_api api{true, 9474, "1.0"};
	fake_target target;
	api.Register(&target, "magewell-abc", "Magewell", token);

	auto r = api.Handle(put("/api/v1/filters/default/controls/refresh_rate_switch", "true"));
	EXPECT_EQ(r.status, 400);
	EXPECT_TRUE(has(r.body, "bad_request"));
	r = api.Handle(put("/api/v1/filters/default/controls/hdr_profile", R"({"value": -1})"));
	EXPECT_EQ(r.status, 400);
	EXPECT_TRUE(has(r.body, "hdr_profile must be between 0 and 1000")) << r.body;
	r = api.Handle(put("/api/v1/filters/default/controls/web_enabled", R"({"value": true})"));
	EXPECT_EQ(r.status, 404);
	EXPECT_TRUE(has(r.body, "unknown_control"));
	auto del = put("/api/v1/filters/default/controls/hdr_profile", "");
	del.method = "DELETE";
	EXPECT_EQ(api.Handle(del).status, 405);
	EXPECT_EQ(api.Handle(put("/api/v1/filters/default/controls", R"({"value": true})")).status, 405);

	target.failSet = true;
	r = api.Handle(put("/api/v1/filters/default/controls/hdr_profile", R"({"value": 2})"));
	EXPECT_EQ(r.status, 500);
}

TEST(WebControls, WebSettingsAreNotControls)
{
	for (const auto& def : web_controls())
	{
		EXPECT_FALSE(def.name.starts_with("web")) << def.name;
	}
}

// Serves a filter with made up status for working on the page, e.g.
//   mwcapture-test --gtest_also_run_disabled_tests --gtest_filter=*ServeForPageDevelopment
// then open http://127.0.0.1:19480/#t=0123456789abcdef0123456789abcdef. EZ_SERVE_SECONDS sets how long (default 60).
TEST(WebServer, DISABLED_ServeForPageDevelopment)
{
	std::string error;
	auto server = web_server::Acquire("127.0.0.1", 19480, "dev", error);
	ASSERT_TRUE(server) << error;
	fake_target target;
	signal_fixture f;
	f.hdr.hdrOn = true;
	f.hdr.hdrMaxCLL = 1000;
	f.hdr.hdrMaxFALL = 400;
	f.vout.outTransferFunction = "SMPTE ST 2084 (PQ)";
	f.vout.outPixelStructure = "P210";
	target.snap.Reload(&f.vin);
	target.snap.Reload(&f.vout);
	target.snap.Reload(&f.hdr);
	target.snap.Reload(&f.ain);
	target.snap.Reload(&f.aout);
	target.snap.ReloadControls(true, false, 3, 1, true, true);
	target.snap.ReloadMetricsControls(false, "127.0.0.1", 9464, "Disabled");
	target.snap.SetRunState(filter_run_state::RUNNING);
	auto reg = server->Api().Register(&target, "magewell-dev", "Magewell Pro Capture", token);

	int seconds = 60;
	char buf[16]{};
	if (GetEnvironmentVariableA("EZ_SERVE_SECONDS", buf, sizeof(buf)) > 0)
	{
		seconds = std::atoi(buf);
	}
	for (int i = 0; i < seconds * 2; ++i)
	{
		latency_stats lat{"Capture", 10000 + i * 10, 12500.0 + i, 20000};
		target.snap.ReloadV1(&lat);
		double fps = 23.976 + (i % 3) * 0.001;
		target.snap.ReloadVfps(&fps);
		Sleep(500);
	}
	server->Api().Unregister(reg);
}

namespace
{
	// accepts connections on 127.0.0.1:port and records each request, answering with status (or not at all)
	struct webhook_receiver
	{
		explicit webhook_receiver(uint16_t port, int status = 200, bool answer = true) : answerWith(status),
			respond(answer)
		{
			WSADATA wsa;
			WSAStartup(MAKEWORD(2, 2), &wsa);
			listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
			bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
			listen(listener, 4);
			worker = std::thread([this] { Run(); });
		}

		~webhook_receiver()
		{
			closesocket(listener);
			worker.join();
			WSACleanup();
		}

		void Run()
		{
			for (;;)
			{
				SOCKET c = accept(listener, nullptr, nullptr);
				if (c == INVALID_SOCKET)
				{
					return;
				}
				std::string req;
				char buf[2048];
				int n;
				// read the headers then the body by Content-Length
				size_t want = std::string::npos;
				while ((n = recv(c, buf, sizeof(buf), 0)) > 0)
				{
					req.append(buf, static_cast<size_t>(n));
					const auto end = req.find("\r\n\r\n");
					if (end != std::string::npos && want == std::string::npos)
					{
						const auto cl = req.find("Content-Length: ");
						want = end + 4 + (cl == std::string::npos ? 0 : std::stoul(req.substr(cl + 16)));
					}
					if (want != std::string::npos && req.size() >= want)
					{
						break;
					}
				}
				{
					std::lock_guard lock{mutex};
					requests.push_back(req);
				}
				if (respond)
				{
					const auto resp = std::format("HTTP/1.1 {} X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
					                              answerWith);
					send(c, resp.data(), static_cast<int>(resp.size()), 0);
				}
				else
				{
					// hold the connection open so the sender waits
					Sleep(3000);
				}
				closesocket(c);
			}
		}

		std::vector<std::string> Requests()
		{
			std::lock_guard lock{mutex};
			return requests;
		}

		int answerWith;
		bool respond;
		SOCKET listener;
		std::thread worker;
		std::mutex mutex;
		std::vector<std::string> requests;
	};

	notify_event event_for(const std::string& mode, const std::string& range = "sdr")
	{
		notify_event e;
		e.summary.signal = true;
		e.summary.videoMode = mode;
		e.summary.dynamicRange = range;
		e.summary.audio = "pcm";
		e.summary.audioChannels = 2;
		e.state = filter_run_state::RUNNING;
		return e;
	}

	// polls for up to timeoutMs
	template <typename F>
	bool eventually(F&& f, int timeoutMs = 1000)
	{
		for (int i = 0; i < timeoutMs / 10; ++i)
		{
			if (f())
			{
				return true;
			}
			Sleep(10);
		}
		return false;
	}
}

TEST(WebNotifier, Payload)
{
	EXPECT_EQ(web_notifier::Payload("magewell-abc", "Magewell", event_for("3840x2160p23.976", "hdr10")),
	          R"({"filter":"magewell-abc","name":"Magewell","state":"running","summary":{"signal":true,"video_mode":"3840x2160p23.976","dynamic_range":"hdr10","audio":"pcm","audio_channels":2}})");
}

TEST(WebNotifier, RejectsBadUrls)
{
	for (const auto* url : {"", "ftp://x/y", "not a url", "http://"})
	{
		web_notifier n{"id", "name"};
		EXPECT_FALSE(n.Start(url).empty()) << url;
	}
	web_notifier ok{"id", "name"};
	EXPECT_EQ(ok.Start("https://ha.local:8123/api/webhook/abc?x=1"), "");
}

TEST(WebNotifier, SendsTheLatestOnceAfterTheDebounce)
{
	webhook_receiver receiver{19490};
	web_notifier n{"magewell-abc", "Magewell", std::chrono::milliseconds{100}};
	ASSERT_EQ(n.Start("http://127.0.0.1:19490/api/webhook/ezcapture"), "");
	n.Notify(event_for("1920x1080p50"));
	n.Notify(event_for("3840x2160p24"));
	n.Notify(event_for("3840x2160p23.976", "hdr10"));
	ASSERT_TRUE(eventually([&] { return receiver.Requests().size() == 1; }));
	Sleep(300);
	const auto requests = receiver.Requests();
	ASSERT_EQ(requests.size(), 1u);
	EXPECT_TRUE(requests[0].starts_with("POST /api/webhook/ezcapture HTTP/1.1")) << requests[0];
	EXPECT_TRUE(has(requests[0], "Content-Type: application/json")) << requests[0];
	EXPECT_TRUE(has(requests[0], R"("video_mode":"3840x2160p23.976","dynamic_range":"hdr10")")) << requests[0];
	EXPECT_TRUE(eventually([&] { return n.Status() == "Last sent, answered 200"; })) << n.Status();

	// the same event again is not sent
	n.Notify(event_for("3840x2160p23.976", "hdr10"));
	Sleep(300);
	EXPECT_EQ(receiver.Requests().size(), 1u);

	n.Notify(event_for("3840x2160p23.976", "dv"));
	EXPECT_TRUE(eventually([&] { return receiver.Requests().size() == 2; }));
}

TEST(WebNotifier, ReportsErrors)
{
	webhook_receiver receiver{19491, 404};
	web_notifier n{"id", "name", std::chrono::milliseconds{10}};
	ASSERT_EQ(n.Start("http://127.0.0.1:19491/x"), "");
	n.Notify(event_for("1920x1080p50"));
	EXPECT_TRUE(eventually([&] { return n.Status() == "The webhook answered 404"; })) << n.Status();

	web_notifier nobody{"id", "name", std::chrono::milliseconds{10}};
	ASSERT_EQ(nobody.Start("http://127.0.0.1:19492/x"), "");
	nobody.Notify(event_for("1920x1080p50"));
	// Windows retries a refused connection for about 2s
	EXPECT_TRUE(eventually([&] { return nobody.Status().starts_with("Unable to send"); }, 5000)) << nobody.Status();
}

TEST(WebNotifier, DestructionAbortsASendInProgress)
{
	webhook_receiver receiver{19493, 200, false};
	auto n = std::make_unique<web_notifier>("id", "name", std::chrono::milliseconds{10},
	                                        std::chrono::milliseconds{2000});
	ASSERT_EQ(n->Start("http://127.0.0.1:19493/x"), "");
	n->Notify(event_for("1920x1080p50"));
	ASSERT_TRUE(eventually([&] { return receiver.Requests().size() == 1; }));
	const auto t0 = std::chrono::steady_clock::now();
	n.reset();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
	EXPECT_LT(ms.count(), 500);
}

TEST(StatusSnapshot, SummaryListenerSeesOnlyChanges)
{
	status_snapshot snap;
	std::vector<notify_event> seen;
	snap.SetSummaryListener([&](const status_summary& s, filter_run_state st) { seen.push_back({s, st}); });
	ASSERT_EQ(seen.size(), 1u);
	EXPECT_FALSE(seen[0].summary.signal);

	double fps = 23.976;
	snap.ReloadVfps(&fps);
	EXPECT_EQ(seen.size(), 1u);

	signal_fixture f;
	snap.Reload(&f.vin);
	ASSERT_EQ(seen.size(), 2u);
	EXPECT_EQ(seen[1].summary.videoMode, "3840x2160p23.976");

	snap.SetRunState(filter_run_state::RUNNING);
	ASSERT_EQ(seen.size(), 3u);
	EXPECT_EQ(seen[2].state, filter_run_state::RUNNING);

	snap.SetSummaryListener(nullptr);
	snap.SetRunState(filter_run_state::PAUSED);
	EXPECT_EQ(seen.size(), 3u);
}
