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
#ifndef WEB_API_HEADER
#define WEB_API_HEADER

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace web_json
{
	struct scalar;
}

class status_snapshot;

inline constexpr size_t webMaxBodyBytes{4096};

struct http_request
{
	std::string method;
	// the path without the query string
	std::string path;
	std::string host;
	std::string authorization;
	std::string ifNoneMatch;
	std::string body;
};

struct http_response
{
	int status{200};
	std::string contentType{"application/json; charset=utf-8"};
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;
};

using control_value = std::variant<bool, int64_t, std::string>;

enum class control_type : uint8_t
{
	BOOLEAN,
	INTEGER,
	STRING
};

enum class control_applies : uint8_t
{
	NOW,
	// when playback next starts
	NEXT_START,
	// when the filter is next loaded
	NEXT_LOAD
};

// a setting the API can read and change
struct control_def
{
	std::string_view name;
	control_type type;
	control_applies applies;
	int64_t min{0};
	int64_t max{0};
};

// every control, in the order the API lists them
std::span<const control_def> web_controls();
const control_def* find_control(std::string_view name);
const char* to_string(control_applies applies);

/**
 * Converts a request value to the control's type, leniently so Home Assistant bodies stay simple: a boolean accepts
 * true/false, 1/0, "on"/"off", "true"/"false", "yes"/"no" and "1"/"0"; an integer accepts a whole number or a string
 * holding one. Ranges are checked.
 * @param error set to why the value was rejected.
 */
std::optional<control_value> to_control_value(const control_def& def, const web_json::scalar& value,
                                              std::string& error);

/*
 * What the API needs from a filter. The web server calls it from its own threads while the filter is registered,
 * web_api::Unregister waits for any call in progress so the filter can be destroyed straight after.
 */
class web_target
{
public:
	virtual ~web_target() = default;
	virtual const status_snapshot& Status() const = 0;
	// the current value of a control from web_controls()
	virtual control_value GetControl(const control_def& def) const = 0;
	// sets a control with a value already converted and checked by to_control_value, false if it could not be saved
	virtual bool SetControl(const control_def& def, const control_value& value) = 0;
	// true if the control has been changed since the filter was loaded and the change is not yet in effect
	virtual bool ControlPending(const control_def& def) const = 0;
};

// one filter served by a web_api
struct web_registration
{
	std::string id;
	std::string name;
	std::string token;
	// part of every ETag so a cached response from an earlier filter instance never matches
	std::string instance;

	// held while the target is called, the target is null once unregistered
	std::mutex lock;
	web_target* target{nullptr};
};

/*
 * Routes and answers requests for the filters registered on one listening address. Knows nothing about HTTP
 * transport so it can be tested directly, web_server feeds it requests.
 */
class web_api
{
public:
	/**
	 * @param loopbackOnly true when the server only listens on a loopback address, reads then need no token as long
	 *                     as the Host header is a loopback name.
	 * @param port the listening port, the Host header must use it.
	 * @param version the filter version reported by /api/v1/info.
	 */
	web_api(bool loopbackOnly, uint16_t port, std::string version);

	// the id is made unique among the registered filters by adding -2, -3 and so on
	std::shared_ptr<web_registration> Register(web_target* target, const std::string& id, const std::string& name,
	                                           const std::string& token);
	// blocks until any call into the target has completed
	void Unregister(const std::shared_ptr<web_registration>& registration);
	size_t Count() const;

	http_response Handle(const http_request& request) const;

	// true for a loopback address, e.g. 127.0.0.1, ::1, localhost
	static bool IsLoopbackAddress(std::string_view address);
	// true if host (a Host header) is a loopback name with no port or the given port
	static bool IsLoopbackHost(std::string_view host, uint16_t port);
	// the token from an "Authorization: Bearer <token>" header, empty if there is none
	static std::string_view BearerToken(std::string_view authorization);
	static bool TokenMatches(std::string_view presented, std::string_view expected);

private:
	enum class access : uint8_t
	{
		ALLOWED,
		NO_TOKEN,
		BAD_TOKEN
	};

	access ReadAccess(const http_request& request, const std::string& token) const;
	std::shared_ptr<web_registration> Find(std::string_view id) const;
	http_response Info() const;
	static http_response State(const http_request& request, web_registration& reg);
	static http_response Controls(web_registration& reg);
	static http_response Control(const http_request& request, web_registration& reg, const control_def& def);

	bool mLoopbackOnly;
	uint16_t mPort;
	std::string mVersion;
	mutable std::mutex mLock;
	std::vector<std::shared_ptr<web_registration>> mRegistrations;
	uint32_t mNextInstance{1};
};

http_response json_error(int status, std::string_view code, std::string_view message);

#endif
