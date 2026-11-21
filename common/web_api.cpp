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
	return json_error(404, "not_found", "No such path");
}
