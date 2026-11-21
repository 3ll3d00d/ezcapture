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
#ifndef WEB_SERVER_HEADER
#define WEB_SERVER_HEADER

#include "web_api.h"

#include <cstdint>
#include <memory>
#include <string>

struct mg_context;
struct mg_connection;

/*
 * A CivetWeb server answering with a web_api. Filters in the same module that use the same address and port share
 * one, it stops when the last of them releases it.
 */
class web_server
{
public:
	web_server(const web_server&) = delete;
	web_server& operator=(const web_server&) = delete;
	~web_server();

	/**
	 * Gets the server for bindAddress:port, starting it if this is the first user. Never throws.
	 * @param error set to a description of the failure when null is returned.
	 */
	static std::shared_ptr<web_server> Acquire(const std::string& bindAddress, uint16_t port, const std::string& version,
	                                           std::string& error);

	web_api& Api() { return mApi; }

	// a new random access token (128 bits as 32 hex digits), empty if no random numbers are available
	static std::string NewToken();

	// e.g. http://127.0.0.1:9474/
	const std::string& Url() const { return mUrl; }

private:
	web_server(const std::string& bindAddress, uint16_t port, const std::string& version);
	bool Start(const std::string& bindAddress, uint16_t port, std::string& error);
	static int OnRequest(mg_connection* conn, void* cbdata);

	web_api mApi;
	mg_context* mContext{nullptr};
	std::string mUrl;
};

#endif
