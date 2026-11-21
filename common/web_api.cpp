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
#define NOMINMAX
#include <streams.h>

#include "web_api.h"
#include "status_snapshot.h"
#include "web_json.h"
#include "web_page.h"

#include <chrono>
#include <format>

namespace
{
	constexpr std::string_view apiPrefix{"/api/v1/"};
	constexpr std::string_view filtersPrefix{"/api/v1/filters/"};

	bool iequals(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			auto la = a[i] >= 'A' && a[i] <= 'Z' ? a[i] - 'A' + 'a' : a[i];
			auto lb = b[i] >= 'A' && b[i] <= 'Z' ? b[i] - 'A' + 'a' : b[i];
			if (la != lb)
			{
				return false;
			}
		}
		return true;
	}

	std::string_view trim(std::string_view s)
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
		{
			s.remove_prefix(1);
		}
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
		{
			s.remove_suffix(1);
		}
		return s;
	}

	http_response no_store(http_response r)
	{
		r.headers.emplace_back("Cache-Control", "no-store");
		return r;
	}

	http_response unauthorised(bool badToken)
	{
		auto r = badToken
			         ? json_error(401, "bad_token", "The token is wrong")
			         : json_error(401, "token_required", "Send the token as an Authorization: Bearer header");
		r.headers.emplace_back("WWW-Authenticate", "Bearer");
		return r;
	}

	// true if the If-None-Match header contains etag (or is *)
	bool etag_matches(std::string_view header, std::string_view etag)
	{
		while (!header.empty())
		{
			const auto comma = header.find(',');
			auto item = trim(header.substr(0, comma));
			if (item.starts_with("W/"))
			{
				item.remove_prefix(2);
			}
			if (item == etag || item == "*")
			{
				return true;
			}
			if (comma == std::string_view::npos)
			{
				break;
			}
			header.remove_prefix(comma + 1);
		}
		return false;
	}

	http_response page(const http_request& request)
	{
		const std::string etag = std::format("\"{}\"", web_page::sha256.substr(0, 16));
		http_response r;
		r.headers.emplace_back("ETag", etag);
		r.headers.emplace_back("Cache-Control", "no-cache");
		if (etag_matches(request.ifNoneMatch, etag))
		{
			r.status = 304;
			r.contentType.clear();
			return r;
		}
		r.contentType = "text/html; charset=utf-8";
		r.headers.emplace_back("Content-Encoding", "gzip");
		r.body.assign(reinterpret_cast<const char*>(web_page::gzipped), sizeof(web_page::gzipped));
		return r;
	}
}

namespace
{
	constexpr control_def controls[] = {
		{"refresh_rate_switch", control_type::BOOLEAN, control_applies::NOW},
		{"hdr_profile_switch", control_type::BOOLEAN, control_applies::NOW},
		{"hdr_profile", control_type::INTEGER, control_applies::NOW, 0, 1000},
		{"sdr_profile", control_type::INTEGER, control_applies::NOW, 0, 1000},
		{"high_thread_priority", control_type::BOOLEAN, control_applies::NEXT_START},
		{"audio_capture", control_type::BOOLEAN, control_applies::NEXT_LOAD},
		{"metrics_enabled", control_type::BOOLEAN, control_applies::NEXT_LOAD},
		{"metrics_port", control_type::INTEGER, control_applies::NEXT_LOAD, 1, 65535},
		{"metrics_bind_address", control_type::STRING, control_applies::NEXT_LOAD, 1, 63},
	};

	void write_value(web_json::writer& w, const control_value& v)
	{
		std::visit([&w](const auto& x) { w.value(x); }, v);
	}

	void write_control(web_json::writer& w, const control_def& def, const web_target& target)
	{
		w.key("name").value(def.name);
		w.key("type").value(def.type == control_type::BOOLEAN
			                    ? "boolean"
			                    : def.type == control_type::INTEGER
			                    ? "integer"
			                    : "string");
		w.key("value");
		write_value(w, target.GetControl(def));
		if (def.type == control_type::INTEGER)
		{
			w.key("min").value(def.min);
			w.key("max").value(def.max);
		}
		w.key("applies").value(to_string(def.applies));
		w.key("pending_reload").value(target.ControlPending(def));
	}
}

std::span<const control_def> web_controls()
{
	return controls;
}

const control_def* find_control(std::string_view name)
{
	for (const auto& c : controls)
	{
		if (c.name == name)
		{
			return &c;
		}
	}
	return nullptr;
}

const char* to_string(control_applies applies)
{
	switch (applies)
	{
	case control_applies::NOW: return "now";
	case control_applies::NEXT_START: return "next_start";
	case control_applies::NEXT_LOAD: return "next_load";
	}
	return "now";
}

std::optional<control_value> to_control_value(const control_def& def, const web_json::scalar& value,
                                              std::string& error)
{
	using web_json::scalar;
	switch (def.type)
	{
	case control_type::BOOLEAN:
		if (value.type == scalar::BOOLEAN)
		{
			return value.b;
		}
		if (value.type == scalar::NUMBER && (value.n == 0.0 || value.n == 1.0))
		{
			return value.n == 1.0;
		}
		if (value.type == scalar::STRING)
		{
			for (const auto* t : {"on", "true", "yes", "1"})
			{
				if (iequals(value.s, t))
				{
					return true;
				}
			}
			for (const auto* f : {"off", "false", "no", "0"})
			{
				if (iequals(value.s, f))
				{
					return false;
				}
			}
		}
		error = std::format("{} must be true or false", def.name);
		return std::nullopt;
	case control_type::INTEGER:
		{
			double n;
			if (value.type == scalar::NUMBER)
			{
				n = value.n;
			}
			else if (value.type == scalar::STRING)
			{
				auto parsed = web_json::parse_scalar(value.s);
				if (!parsed || parsed->type != scalar::NUMBER)
				{
					error = std::format("{} must be a whole number", def.name);
					return std::nullopt;
				}
				n = parsed->n;
			}
			else
			{
				error = std::format("{} must be a whole number", def.name);
				return std::nullopt;
			}
			if (n < -9.0e15 || n > 9.0e15 || n != static_cast<double>(static_cast<int64_t>(n)))
			{
				error = std::format("{} must be a whole number", def.name);
				return std::nullopt;
			}
			const auto i = static_cast<int64_t>(n);
			if (i < def.min || i > def.max)
			{
				error = std::format("{} must be between {} and {}", def.name, def.min, def.max);
				return std::nullopt;
			}
			return i;
		}
	case control_type::STRING:
		{
			if (value.type != scalar::STRING)
			{
				error = std::format("{} must be a string", def.name);
				return std::nullopt;
			}
			const auto len = static_cast<int64_t>(value.s.size());
			if (len < def.min || len > def.max)
			{
				error = std::format("{} must be {} to {} characters", def.name, def.min, def.max);
				return std::nullopt;
			}
			// the only string control is an address: digits, letters, '.', ':', '-', '[' and ']'
			for (const char c : value.s)
			{
				if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
					c == ':' || c == '-' || c == '[' || c == ']'))
				{
					error = std::format("{} is not a valid address", def.name);
					return std::nullopt;
				}
			}
			return value.s;
		}
	}
	error = "unknown control";
	return std::nullopt;
}

http_response json_error(int status, std::string_view code, std::string_view message)
{
	web_json::writer w;
	w.begin_object();
	w.key("error").value(code);
	w.key("message").value(message);
	w.end_object();
	http_response r;
	r.status = status;
	r.body = w.take();
	r.headers.emplace_back("Cache-Control", "no-store");
	return r;
}

web_api::web_api(bool loopbackOnly, uint16_t port, std::string version) :
	mLoopbackOnly(loopbackOnly),
	mPort(port),
	mVersion(std::move(version))
{
}

std::shared_ptr<web_registration> web_api::Register(web_target* target, const std::string& id,
                                                    const std::string& name, const std::string& token)
{
	auto reg = std::make_shared<web_registration>();
	reg->name = name;
	reg->token = token;
	reg->target = target;

	std::lock_guard lock{mLock};
	auto unique = id;
	for (int n = 2; ; ++n)
	{
		bool clash = unique == "default";
		for (const auto& r : mRegistrations)
		{
			clash |= r->id == unique;
		}
		if (!clash)
		{
			break;
		}
		unique = std::format("{}-{}", id, n);
	}
	reg->id = std::move(unique);
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	reg->instance = std::format("{:x}{:x}", static_cast<uint64_t>(now) & 0xFFFFFFFF, mNextInstance++);
	mRegistrations.push_back(reg);
	return reg;
}

void web_api::Unregister(const std::shared_ptr<web_registration>& registration)
{
	if (!registration)
	{
		return;
	}
	{
		std::lock_guard lock{mLock};
		std::erase(mRegistrations, registration);
	}
	// waits for a request that already found the registration
	std::lock_guard lock{registration->lock};
	registration->target = nullptr;
}

size_t web_api::Count() const
{
	std::lock_guard lock{mLock};
	return mRegistrations.size();
}

std::shared_ptr<web_registration> web_api::Find(std::string_view id) const
{
	std::lock_guard lock{mLock};
	if (id == "default")
	{
		return mRegistrations.empty() ? nullptr : mRegistrations.front();
	}
	for (const auto& r : mRegistrations)
	{
		if (r->id == id)
		{
			return r;
		}
	}
	return nullptr;
}

bool web_api::IsLoopbackAddress(std::string_view address)
{
	if (address.starts_with('[') && address.ends_with(']'))
	{
		address = address.substr(1, address.size() - 2);
	}
	return address == "::1" || iequals(address, "localhost") || address.starts_with("127.");
}

bool web_api::IsLoopbackHost(std::string_view host, uint16_t port)
{
	std::string_view name = host;
	std::string_view portPart;
	if (host.starts_with('['))
	{
		const auto close = host.find(']');
		if (close == std::string_view::npos)
		{
			return false;
		}
		name = host.substr(0, close + 1);
		const auto rest = host.substr(close + 1);
		if (!rest.empty())
		{
			if (rest.front() != ':')
			{
				return false;
			}
			portPart = rest.substr(1);
		}
	}
	else if (const auto colon = host.rfind(':'); colon != std::string_view::npos)
	{
		name = host.substr(0, colon);
		portPart = host.substr(colon + 1);
	}
	if (!portPart.empty() && portPart != std::to_string(port))
	{
		return false;
	}
	return name == "127.0.0.1" || name == "[::1]" || iequals(name, "localhost");
}

std::string_view web_api::BearerToken(std::string_view authorization)
{
	authorization = trim(authorization);
	if (authorization.size() < 7 || !iequals(authorization.substr(0, 7), "bearer "))
	{
		return {};
	}
	return trim(authorization.substr(7));
}

bool web_api::TokenMatches(std::string_view presented, std::string_view expected)
{
	if (expected.empty())
	{
		return false;
	}
	// look at every byte whatever the input so the time taken does not show how much of the token matched
	unsigned diff = presented.size() == expected.size() ? 0u : 1u;
	for (size_t i = 0; i < expected.size(); ++i)
	{
		const auto p = i < presented.size() ? static_cast<unsigned char>(presented[i]) : 0u;
		diff |= p ^ static_cast<unsigned char>(expected[i]);
	}
	return diff == 0;
}

web_api::access web_api::ReadAccess(const http_request& request, const std::string& token) const
{
	if (const auto presented = BearerToken(request.authorization); !presented.empty())
	{
		// a valid token works from any host name, as Home Assistant may use any name or address for this PC
		return TokenMatches(presented, token) ? access::ALLOWED : access::BAD_TOKEN;
	}
	// without a token only a local client may read, and only by a local name so a DNS rebinding page cannot
	if (mLoopbackOnly && IsLoopbackHost(request.host, mPort))
	{
		return access::ALLOWED;
	}
	return access::NO_TOKEN;
}

http_response web_api::Info() const
{
	web_json::writer w;
	w.begin_object();
	w.key("api_version").value(1);
	w.key("product").value("ezcapture");
	w.key("version").value(mVersion);
	w.key("filters").begin_array();
	{
		std::lock_guard lock{mLock};
		for (const auto& r : mRegistrations)
		{
			w.begin_object();
			w.key("id").value(r->id);
			w.key("name").value(r->name);
			w.end_object();
		}
	}
	w.end_array();
	w.end_object();
	http_response r;
	r.body = w.take();
	return no_store(std::move(r));
}

http_response web_api::State(const http_request& request, web_registration& reg)
{
	const auto& status = reg.target->Status();
	const auto current = std::format("\"{}-{}\"", reg.instance, status.Version());
	http_response r;
	if (etag_matches(request.ifNoneMatch, current))
	{
		r.status = 304;
		r.contentType.clear();
		r.headers.emplace_back("ETag", current);
		return no_store(std::move(r));
	}
	uint64_t version{0};
	r.body = status.ToJson(reg.id, reg.name, &version);
	r.headers.emplace_back("ETag", std::format("\"{}-{}\"", reg.instance, version));
	return no_store(std::move(r));
}

http_response web_api::Handle(const http_request& request) const
{
	const std::string_view path = request.path;
	const bool get = request.method == "GET" || request.method == "HEAD";

	if (path == "/" || path == "/index.html")
	{
		if (!get)
		{
			return json_error(405, "method_not_allowed", "Use GET");
		}
		return page(request);
	}
	if (!path.starts_with(apiPrefix))
	{
		return json_error(404, "not_found", "No such path");
	}

	if (path == "/api/v1/info" || path == "/api/v1/filters")
	{
		if (!get)
		{
			return json_error(405, "method_not_allowed", "Use GET");
		}
		// any filter's token will do, filters in one DLL share their settings so they have the same token
		std::vector<std::string> tokens;
		{
			std::lock_guard lock{mLock};
			for (const auto& r : mRegistrations)
			{
				tokens.push_back(r->token);
			}
		}
		auto result = access::NO_TOKEN;
		for (const auto& t : tokens)
		{
			result = ReadAccess(request, t);
			if (result == access::ALLOWED)
			{
				break;
			}
		}
		if (tokens.empty())
		{
			result = ReadAccess(request, {});
		}
		if (result != access::ALLOWED)
		{
			return unauthorised(result == access::BAD_TOKEN);
		}
		return Info();
	}

	if (!path.starts_with(filtersPrefix))
	{
		return json_error(404, "not_found", "No such path");
	}
	auto rest = path.substr(filtersPrefix.size());
	const auto slash = rest.find('/');
	const auto id = rest.substr(0, slash);
	const auto resource = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);

	const auto reg = Find(id);
	if (!reg)
	{
		return json_error(404, "unknown_filter", "No such filter");
	}
	if (const auto result = ReadAccess(request, reg->token); result != access::ALLOWED)
	{
		return unauthorised(result == access::BAD_TOKEN);
	}

	std::lock_guard lock{reg->lock};
	if (reg->target == nullptr)
	{
		return json_error(404, "unknown_filter", "No such filter");
	}
	if (resource == "state")
	{
		if (!get)
		{
			return json_error(405, "method_not_allowed", "Use GET");
		}
		return State(request, *reg);
	}
	if (resource == "controls")
	{
		if (!get)
		{
			return json_error(405, "method_not_allowed", "Use GET");
		}
		return Controls(*reg);
	}
	if (resource.starts_with("history/"))
	{
		if (!get)
		{
			return json_error(405, "method_not_allowed", "Use GET");
		}
		const auto kind = resource.substr(8);
		const auto& status = reg->target->Status();
		http_response r;
		if (kind == "latency")
		{
			r.body = status.LatencyHistoryJson();
		}
		else if (kind == "signal")
		{
			r.body = status.SignalHistoryJson();
		}
		else if (kind == "display")
		{
			r.body = status.DisplayHistoryJson();
		}
		else
		{
			return json_error(404, "not_found", "No such history, use latency, signal or display");
		}
		return no_store(std::move(r));
	}
	if (resource.starts_with("controls/"))
	{
		const auto* def = find_control(resource.substr(9));
		if (def == nullptr)
		{
			return json_error(404, "unknown_control", "No such control");
		}
		return Control(request, *reg, *def);
	}
	return json_error(404, "not_found", "No such path");
}

http_response web_api::Controls(web_registration& reg)
{
	web_json::writer w;
	w.begin_object();
	bool pending = false;
	w.key("controls").begin_array();
	for (const auto& def : web_controls())
	{
		w.begin_object();
		write_control(w, def, *reg.target);
		w.end_object();
		pending |= reg.target->ControlPending(def);
	}
	w.end_array();
	w.key("pending_reload").value(pending);
	w.end_object();
	http_response r;
	r.body = w.take();
	return no_store(std::move(r));
}

http_response web_api::Control(const http_request& request, web_registration& reg, const control_def& def)
{
	const bool get = request.method == "GET" || request.method == "HEAD";
	if (!get)
	{
		if (request.method != "PUT" && request.method != "POST")
		{
			return json_error(405, "method_not_allowed", "Use GET, PUT or POST");
		}
		// a change always needs the token, whatever the address. A browser cannot add an Authorization header to a
		// cross origin request without a CORS preflight, which is never answered, so another site cannot send one.
		const auto presented = BearerToken(request.authorization);
		if (!TokenMatches(presented, reg.token))
		{
			return unauthorised(!presented.empty());
		}
		auto value = web_json::parse_member(request.body, "value");
		if (!value)
		{
			return json_error(400, "bad_request", R"(Send a JSON object such as {"value": true})");
		}
		std::string error;
		auto converted = to_control_value(def, *value, error);
		if (!converted)
		{
			return json_error(400, "bad_value", error);
		}
		if (!reg.target->SetControl(def, *converted))
		{
			return json_error(500, "not_saved", "The setting could not be saved");
		}
	}
	web_json::writer w;
	w.begin_object();
	write_control(w, def, *reg.target);
	w.end_object();
	http_response r;
	r.body = w.take();
	return no_store(std::move(r));
}
