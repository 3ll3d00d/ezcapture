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
#include "web_json.h"

#include <charconv>
#include <cmath>
#include <format>

namespace web_json
{
	void append_quoted(std::string& out, std::string_view s)
	{
		out.push_back('"');
		for (const char c : s)
		{
			switch (c)
			{
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
				}
				else
				{
					out.push_back(c);
				}
			}
		}
		out.push_back('"');
	}

	std::string slug(std::string_view s)
	{
		if (s == "?")
		{
			return {};
		}
		std::string out;
		out.reserve(s.size());
		bool pendingSep = false;
		for (const char c : s)
		{
			const auto u = static_cast<unsigned char>(c);
			if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9'))
			{
				if (pendingSep && !out.empty())
				{
					out.push_back('_');
				}
				pendingSep = false;
				out.push_back(c);
			}
			else if (u >= 'A' && u <= 'Z')
			{
				if (pendingSep && !out.empty())
				{
					out.push_back('_');
				}
				pendingSep = false;
				out.push_back(static_cast<char>(u - 'A' + 'a'));
			}
			else if (c != '.' && c != ':' && c != '\'')
			{
				pendingSep = true;
			}
		}
		return out;
	}

	std::string to_utf8(std::wstring_view s)
	{
		std::string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size(); ++i)
		{
			uint32_t cp = s[i];
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
			{
				cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
				++i;
			}
			else if (cp >= 0xD800 && cp <= 0xDFFF)
			{
				cp = 0xFFFD;
			}
			if (cp < 0x80)
			{
				out.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else if (cp < 0x10000)
			{
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		return out;
	}

	void writer::separate()
	{
		if (mAfterKey)
		{
			mAfterKey = false;
			return;
		}
		if (!mFirst.empty())
		{
			if (mFirst.back())
			{
				mFirst.back() = false;
			}
			else
			{
				mOut.push_back(',');
			}
		}
	}

	writer& writer::begin_object()
	{
		separate();
		mOut.push_back('{');
		mFirst.push_back(true);
		return *this;
	}

	writer& writer::end_object()
	{
		mOut.push_back('}');
		mFirst.pop_back();
		return *this;
	}

	writer& writer::begin_array()
	{
		separate();
		mOut.push_back('[');
		mFirst.push_back(true);
		return *this;
	}

	writer& writer::end_array()
	{
		mOut.push_back(']');
		mFirst.pop_back();
		return *this;
	}

	writer& writer::key(std::string_view k)
	{
		separate();
		append_quoted(mOut, k);
		mOut.push_back(':');
		mAfterKey = true;
		return *this;
	}

	writer& writer::value(std::string_view v)
	{
		separate();
		append_quoted(mOut, v);
		return *this;
	}

	writer& writer::value(bool v)
	{
		separate();
		mOut += v ? "true" : "false";
		return *this;
	}

	writer& writer::value(int64_t v)
	{
		separate();
		mOut += std::to_string(v);
		return *this;
	}

	writer& writer::value(uint64_t v)
	{
		separate();
		mOut += std::to_string(v);
		return *this;
	}

	writer& writer::value(double v)
	{
		if (!std::isfinite(v))
		{
			return null();
		}
		separate();
		mOut += std::format("{:.6g}", v);
		return *this;
	}

	writer& writer::null()
	{
		separate();
		mOut += "null";
		return *this;
	}

	namespace
	{
		struct parser
		{
			std::string_view in;
			size_t pos{0};

			void skip_ws()
			{
				while (pos < in.size() && (in[pos] == ' ' || in[pos] == '\t' || in[pos] == '\n' || in[pos] == '\r'))
				{
					++pos;
				}
			}

			bool consume(char c)
			{
				skip_ws();
				if (pos < in.size() && in[pos] == c)
				{
					++pos;
					return true;
				}
				return false;
			}

			bool literal(std::string_view lit)
			{
				if (in.substr(pos, lit.size()) == lit)
				{
					pos += lit.size();
					return true;
				}
				return false;
			}

			static void append_utf8(std::string& out, uint32_t cp)
			{
				const wchar_t w[2] = {static_cast<wchar_t>(cp), 0};
				if (cp < 0x10000)
				{
					out += to_utf8(std::wstring_view{w, 1});
				}
				else
				{
					const wchar_t pair[2] = {
						static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10)),
						static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF))
					};
					out += to_utf8(std::wstring_view{pair, 2});
				}
			}

			bool hex4(uint32_t& out)
			{
				if (pos + 4 > in.size())
				{
					return false;
				}
				out = 0;
				for (int i = 0; i < 4; ++i)
				{
					const char c = in[pos++];
					out <<= 4;
					if (c >= '0' && c <= '9') out |= c - '0';
					else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
					else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
					else return false;
				}
				return true;
			}

			std::optional<std::string> string()
			{
				skip_ws();
				if (pos >= in.size() || in[pos] != '"')
				{
					return std::nullopt;
				}
				++pos;
				std::string out;
				while (pos < in.size())
				{
					const char c = in[pos++];
					if (c == '"')
					{
						return out;
					}
					if (static_cast<unsigned char>(c) < 0x20)
					{
						return std::nullopt;
					}
					if (c != '\\')
					{
						out.push_back(c);
						continue;
					}
					if (pos >= in.size())
					{
						return std::nullopt;
					}
					switch (const char e = in[pos++])
					{
					case '"': out.push_back('"');
						break;
					case '\\': out.push_back('\\');
						break;
					case '/': out.push_back('/');
						break;
					case 'b': out.push_back('\b');
						break;
					case 'f': out.push_back('\f');
						break;
					case 'n': out.push_back('\n');
						break;
					case 'r': out.push_back('\r');
						break;
					case 't': out.push_back('\t');
						break;
					case 'u':
						{
							uint32_t cp;
							if (!hex4(cp))
							{
								return std::nullopt;
							}
							if (cp >= 0xD800 && cp <= 0xDBFF)
							{
								uint32_t lo;
								if (!literal("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
								{
									return std::nullopt;
								}
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							}
							else if (cp >= 0xDC00 && cp <= 0xDFFF)
							{
								return std::nullopt;
							}
							append_utf8(out, cp);
							break;
						}
					default:
						(void)e;
						return std::nullopt;
					}
				}
				return std::nullopt;
			}

			std::optional<scalar> value()
			{
				skip_ws();
				if (pos >= in.size())
				{
					return std::nullopt;
				}
				scalar s;
				const char c = in[pos];
				if (c == '"')
				{
					auto str = string();
					if (!str)
					{
						return std::nullopt;
					}
					s.type = scalar::STRING;
					s.s = std::move(*str);
					return s;
				}
				if (literal("true"))
				{
					s.type = scalar::BOOLEAN;
					s.b = true;
					return s;
				}
				if (literal("false"))
				{
					s.type = scalar::BOOLEAN;
					return s;
				}
				if (literal("null"))
				{
					return s;
				}
				if (c == '-' || (c >= '0' && c <= '9'))
				{
					// strict JSON number grammar: -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
					const size_t start = pos;
					if (in[pos] == '-') ++pos;
					if (pos >= in.size()) return std::nullopt;
					if (in[pos] == '0') ++pos;
					else if (in[pos] >= '1' && in[pos] <= '9') while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9') ++pos;
					else return std::nullopt;
					if (pos < in.size() && in[pos] == '.')
					{
						++pos;
						const size_t digits = pos;
						while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9') ++pos;
						if (pos == digits) return std::nullopt;
					}
					if (pos < in.size() && (in[pos] == 'e' || in[pos] == 'E'))
					{
						++pos;
						if (pos < in.size() && (in[pos] == '+' || in[pos] == '-')) ++pos;
						const size_t digits = pos;
						while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9') ++pos;
						if (pos == digits) return std::nullopt;
					}
					double d{0.0};
					const auto* first = in.data() + start;
					const auto* last = in.data() + pos;
					if (auto [ptr, ec] = std::from_chars(first, last, d); ec != std::errc{} || ptr != last)
					{
						return std::nullopt;
					}
					s.type = scalar::NUMBER;
					s.n = d;
					return s;
				}
				return std::nullopt;
			}

			bool at_end()
			{
				skip_ws();
				return pos == in.size();
			}
		};
	}

	std::optional<scalar> parse_scalar(std::string_view json)
	{
		parser p{json};
		auto v = p.value();
		if (!v || !p.at_end())
		{
			return std::nullopt;
		}
		return v;
	}

	std::optional<scalar> parse_member(std::string_view json, std::string_view key)
	{
		parser p{json};
		if (!p.consume('{'))
		{
			return std::nullopt;
		}
		std::optional<scalar> found;
		if (!p.consume('}'))
		{
			do
			{
				auto k = p.string();
				if (!k || !p.consume(':'))
				{
					return std::nullopt;
				}
				auto v = p.value();
				if (!v)
				{
					return std::nullopt;
				}
				if (*k == key)
				{
					found = std::move(v);
				}
			}
			while (p.consume(','));
			if (!p.consume('}'))
			{
				return std::nullopt;
			}
		}
		if (!p.at_end())
		{
			return std::nullopt;
		}
		return found;
	}
}
