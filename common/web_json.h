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
#ifndef WEB_JSON_HEADER
#define WEB_JSON_HEADER

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Just enough JSON for the web API: a writer for the fixed shape documents it serves and a parser for the small
// request bodies it accepts.
namespace web_json
{
	// appends s as a quoted JSON string, s is expected to be UTF-8
	void append_quoted(std::string& out, std::string_view s);

	// lowercase snake_case form of a display string, e.g. "YUV 4:2:2" -> "yuv_422", "No Signal" -> "no_signal";
	// letters and digits are kept, '.', ':' and '\'' are dropped and any other run of characters becomes one '_'.
	// Returns an empty string for an empty string or "?".
	std::string slug(std::string_view s);

	// UTF-16 to UTF-8
	std::string to_utf8(std::wstring_view s);

	/*
	 * Writes a document in one pass, keeping track of the commas. Keys are only passed inside an object.
	 *
	 *   writer w;
	 *   w.begin_object();
	 *   w.key("fps").value(23.976);
	 *   w.end_object();
	 */
	class writer
	{
	public:
		writer& begin_object();
		writer& end_object();
		writer& begin_array();
		writer& end_array();
		writer& key(std::string_view k);

		writer& value(std::string_view v);
		writer& value(const char* v) { return value(std::string_view{v}); }
		writer& value(const std::string& v) { return value(std::string_view{v}); }
		writer& value(bool v);
		writer& value(int v) { return value(static_cast<int64_t>(v)); }
		writer& value(unsigned v) { return value(static_cast<uint64_t>(v)); }
		writer& value(long v) { return value(static_cast<int64_t>(v)); }
		writer& value(unsigned long v) { return value(static_cast<uint64_t>(v)); }
		writer& value(int64_t v);
		writer& value(uint64_t v);
		// null if not finite, otherwise the shortest form that round trips at 6 significant digits
		writer& value(double v);
		writer& null();

		// null when empty
		writer& value_or_null(std::string_view v) { return v.empty() ? null() : value(v); }

		template <typename T>
		writer& value(const std::optional<T>& v)
		{
			return v.has_value() ? value(*v) : null();
		}

		const std::string& str() const { return mOut; }
		std::string take() { return std::move(mOut); }

	private:
		void separate();

		std::string mOut;
		// one entry per open object/array, true until its first member is written
		std::vector<bool> mFirst;
		bool mAfterKey{false};
	};

	struct scalar
	{
		enum kind : uint8_t { NUL, BOOLEAN, NUMBER, STRING };

		kind type{NUL};
		bool b{false};
		double n{0.0};
		std::string s;
	};

	// parses a single JSON scalar (null, true, false, a number or a string), surrounded only by whitespace
	std::optional<scalar> parse_scalar(std::string_view json);

	// parses a flat JSON object whose values are all scalars and returns the value of key, nullopt if the body is not
	// such an object or does not have the key
	std::optional<scalar> parse_member(std::string_view json, std::string_view key);
}

#endif
