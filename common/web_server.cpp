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
#include "web_server.h"

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <civetweb.h>

#pragma comment(lib, "bcrypt.lib")

#include <format>
#include <map>
#include <mutex>

namespace
{
	std::mutex serversLock;
	std::map<std::string, std::weak_ptr<web_server>> servers;

	// CivetWeb wants IPv6 addresses in brackets
	std::string listen_host(const std::string& bindAddress)
	{
		if (bindAddress.find(':') != std::string::npos && !bindAddress.starts_with('['))
		{
			return "[" + bindAddress + "]";
		}
		return bindAddress;
	}

	std::string header(mg_connection* conn, const char* name)
	{
		const char* value = mg_get_header(conn, name);
		return value == nullptr ? std::string{} : std::string{value};
	}
}

std::string web_server::NewToken()
{
	unsigned char bytes[16];
	if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
	{
		return {};
	}
	std::string token;
	for (const auto b : bytes)
	{
		token += std::format("{:02x}", b);
	}
	return token;
}

web_server::web_server(const std::string& bindAddress, uint16_t port, const std::string& version) :
	mApi(web_api::IsLoopbackAddress(bindAddress), port, version)
{
}

web_server::~web_server()
{
	if (mContext != nullptr)
	{
		// waits for the worker threads, any request in progress completes first
		mg_stop(mContext);
		mContext = nullptr;
		mg_exit_library();
	}
}

std::shared_ptr<web_server> web_server::Acquire(const std::string& bindAddress, uint16_t port,
                                                const std::string& version, std::string& error)
{
	const auto key = std::format("{}:{}", listen_host(bindAddress), port);
	std::lock_guard lock{serversLock};
	if (auto existing = servers[key].lock())
	{
		return existing;
	}
	std::shared_ptr<web_server> server;
	try
	{
		server.reset(new web_server(bindAddress, port, version));
	}
	catch (const std::exception& e)
	{
		error = e.what();
		return nullptr;
	}
	if (!server->Start(bindAddress, port, error))
	{
		return nullptr;
	}
	servers[key] = server;
	return server;
}

bool web_server::Start(const std::string& bindAddress, uint16_t port, std::string& error)
{
	const auto listen = std::format("{}:{}", listen_host(bindAddress), port);
	const char* options[] = {
		"listening_ports", listen.c_str(),
		// one request at a time is plenty, the second stops a slow client blocking everything
		"num_threads", "2",
		"request_timeout_ms", "5000",
		"enable_keep_alive", "yes",
		"keep_alive_timeout_ms", "5000",
		"enable_auth_domain_check", "no",
		// no document_root so nothing is ever served from disk
		nullptr
	};

	mg_init_library(0);
	mg_callbacks callbacks{};
	mg_init_data init{};
	init.callbacks = &callbacks;
	init.user_data = this;
	init.configuration_options = options;
	char errorText[256]{};
	mg_error_data err{};
	err.text = errorText;
	err.text_buffer_size = sizeof(errorText);
	mContext = mg_start2(&init, &err);
	if (mContext == nullptr)
	{
		mg_exit_library();
		error = std::format("Unable to listen on {} ({})", listen, errorText[0] ? errorText : "unknown error");
		return false;
	}
	mg_set_request_handler(mContext, "/", &web_server::OnRequest, this);
	mUrl = std::format("http://{}/", listen);
	return true;
}

int web_server::OnRequest(mg_connection* conn, void* cbdata)
{
	auto* self = static_cast<web_server*>(cbdata);
	const auto* ri = mg_get_request_info(conn);

	http_response response;
	bool head = false;
	try
	{
		http_request request;
		request.method = ri->request_method != nullptr ? ri->request_method : "";
		request.path = ri->local_uri != nullptr ? ri->local_uri : "/";
		request.host = header(conn, "Host");
		request.authorization = header(conn, "Authorization");
		request.ifNoneMatch = header(conn, "If-None-Match");
		head = request.method == "HEAD";

		bool tooBig = ri->content_length > static_cast<long long>(webMaxBodyBytes);
		if (!tooBig && ri->content_length != 0)
		{
			char buf[1024];
			int n;
			while ((n = mg_read(conn, buf, sizeof(buf))) > 0)
			{
				request.body.append(buf, static_cast<size_t>(n));
				if (request.body.size() > webMaxBodyBytes)
				{
					tooBig = true;
					break;
				}
			}
		}
		response = tooBig
			           ? json_error(413, "too_large", "The request body is too large")
			           : self->mApi.Handle(request);
	}
	catch (...)
	{
		response = json_error(500, "internal_error", "The request could not be handled");
	}

	mg_response_header_start(conn, response.status);
	if (!response.contentType.empty())
	{
		mg_response_header_add(conn, "Content-Type", response.contentType.c_str(), -1);
	}
	mg_response_header_add(conn, "X-Content-Type-Options", "nosniff", -1);
	for (const auto& [name, value] : response.headers)
	{
		mg_response_header_add(conn, name.c_str(), value.c_str(), -1);
	}
	const bool hasBody = response.status != 304 && response.status != 204;
	if (hasBody)
	{
		const auto len = std::to_string(response.body.size());
		mg_response_header_add(conn, "Content-Length", len.c_str(), -1);
	}
	mg_response_header_send(conn);
	if (hasBody && !head && !response.body.empty())
	{
		mg_write(conn, response.body.data(), response.body.size());
	}
	return response.status;
}
