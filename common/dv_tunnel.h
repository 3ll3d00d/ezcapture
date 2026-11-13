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
#ifndef DV_TUNNEL_HEADER
#define DV_TUNNEL_HEADER

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

/*
 * Decoder for the HDMI Dolby Vision RGB tunnel (the "TV led" DV format) captured as 8-bit RGB, see
 * pixel_layout for the supported capture layouts.
 *
 * The decode logic follows CRGBDolbyVisionTunnelFormatter from billslack2/videoprocessor (GPL-3.0),
 * see docs/magewell-video-comparison.md section 5 for the format.
 *
 * - picture: each pixel carries one 12-bit I sample (G << 4 | B & 0xF) and one 12-bit chroma sample
 *   (R << 4 | B >> 4), chroma alternates Cb/Cr so the picture is 12-bit 4:2:2 IPT-PQ.
 * - metadata: from pixel 0 in raster order, the parity of each pixel's 24 bits is one bit of a 128
 *   byte packet, each packet is sent 3 times and a copy is valid when its CRC-32/MPEG-2 is 0.
 */
namespace dv_tunnel
{
	inline constexpr size_t packetBits = 1024;
	inline constexpr size_t packetBytes = 128;
	inline constexpr size_t repeats = 3;
	inline constexpr size_t maxMetadataBytes = 0x2F00;
	inline constexpr uint32_t packetLossLimit = 24;

	struct extension_block
	{
		uint8_t level{0};
		std::vector<uint8_t> payload{};
	};

	// display management metadata, raw values as transmitted (big endian fields already decoded)
	struct metadata
	{
		bool valid{false};
		uint8_t metadataId{0};
		uint8_t sceneRefreshFlag{0};
		std::array<int16_t, 9> yccToRgb{}; // / 8192
		std::array<uint32_t, 3> yccToRgbOffset{}; // / 2^28
		std::array<int16_t, 9> rgbToLms{}; // / 16384
		uint16_t signalEotf{0};
		uint16_t signalEotfParam0{0};
		uint16_t signalEotfParam1{0};
		uint32_t signalEotfParam2{0};
		uint8_t signalBitDepth{0};
		uint8_t signalColorSpace{0};
		uint8_t signalChromaFormat{0};
		uint8_t signalFullRangeFlag{0};
		uint16_t sourceMinPq{0};
		uint16_t sourceMaxPq{0};
		uint16_t sourceDiagonal{0};
		bool hasL1{false};
		uint16_t l1MinPq{0};
		uint16_t l1MaxPq{0};
		uint16_t l1AvgPq{0};
		std::vector<extension_block> extensions{};

		bool fullRange() const { return signalFullRangeFlag != 0; }
	};

	namespace detail
	{
		inline uint16_t be16(const uint8_t* b)
		{
			return static_cast<uint16_t>(b[0] << 8 | b[1]);
		}

		inline uint32_t be32(const uint8_t* b)
		{
			return static_cast<uint32_t>(b[0]) << 24 | static_cast<uint32_t>(b[1]) << 16 |
				static_cast<uint32_t>(b[2]) << 8 | b[3];
		}

		inline uint8_t parity8(uint8_t v)
		{
			v ^= v >> 4;
			v ^= v >> 2;
			v ^= v >> 1;
			return v & 1;
		}

		// ISO/IEC 13818-1 CRC-32 (MPEG-2), 0 over a packet that includes its own CRC means it is valid
		inline uint32_t crc32_mpeg2(const uint8_t* bytes, size_t len)
		{
			uint32_t crc = 0xFFFFFFFFU;
			for (size_t i = 0; i < len; ++i)
			{
				crc ^= static_cast<uint32_t>(bytes[i]) << 24;
				for (int bit = 0; bit < 8; ++bit)
				{
					crc = crc & 0x80000000U ? crc << 1 ^ 0x04C11DB7U : crc << 1;
				}
			}
			return crc;
		}

		struct packet_header
		{
			uint8_t kind{0}; // 0 single, 1 first, 2 middle, 3 last
			uint8_t affectedId{0};
			uint8_t currentId{0};
			bool noMetadata{false};
			bool endOfStream{false};
		};

		inline bool parse_header(const uint8_t* packet, packet_header& h)
		{
			if ((packet[0] & 0x3EU) != 0 || (packet[2] & 0xFEU) != 0)
			{
				return false;
			}
			h.kind = packet[0] >> 6;
			h.noMetadata = (packet[0] & 1U) != 0;
			h.affectedId = packet[1] >> 4;
			h.currentId = packet[1] & 0xF;
			h.endOfStream = (packet[2] & 1U) != 0;
			return h.affectedId == h.currentId || h.affectedId == ((h.currentId + 1U) & 0xF);
		}

		inline bool parse_metadata(const uint8_t* dm, size_t length, metadata& out)
		{
			if (length < 71)
			{
				return false;
			}
			// 12-bit opponent colour only
			if (dm[60] != 12 || (dm[61] != 0 && dm[61] != 2))
			{
				return false;
			}
			metadata md;
			md.valid = true;
			md.metadataId = dm[0];
			md.sceneRefreshFlag = dm[1];
			for (size_t i = 0; i < 9; ++i)
			{
				md.yccToRgb[i] = static_cast<int16_t>(be16(dm + 2 + i * 2));
				md.rgbToLms[i] = static_cast<int16_t>(be16(dm + 32 + i * 2));
			}
			for (size_t i = 0; i < 3; ++i)
			{
				md.yccToRgbOffset[i] = be32(dm + 20 + i * 4);
			}
			md.signalEotf = be16(dm + 50);
			md.signalEotfParam0 = be16(dm + 52);
			md.signalEotfParam1 = be16(dm + 54);
			md.signalEotfParam2 = be32(dm + 56);
			md.signalBitDepth = dm[60];
			md.signalColorSpace = dm[61];
			md.signalChromaFormat = dm[62];
			md.signalFullRangeFlag = dm[63];
			md.sourceMinPq = be16(dm + 64);
			md.sourceMaxPq = be16(dm + 66);
			md.sourceDiagonal = be16(dm + 68);
			size_t offset = 71;
			for (unsigned block = 0; block < dm[70]; ++block)
			{
				if (length - offset < 5)
				{
					return false;
				}
				const uint32_t payloadLength = be32(dm + offset);
				const uint8_t level = dm[offset + 4];
				if (payloadLength > length - offset - 5)
				{
					return false;
				}
				const uint8_t* payload = dm + offset + 5;
				if (level == 1)
				{
					if (payloadLength != 6)
					{
						return false;
					}
					md.l1MinPq = be16(payload);
					md.l1MaxPq = be16(payload + 2);
					md.l1AvgPq = be16(payload + 4);
					if (md.l1MinPq > 4095 || md.l1MaxPq > 4095 || md.l1AvgPq > 4095)
					{
						return false;
					}
					md.hasL1 = true;
				}
				md.extensions.push_back({level, {payload, payload + payloadLength}});
				offset += 5 + payloadLength;
			}
			out = std::move(md);
			return true;
		}
	}

	/**
	 * How the 8-bit RGB tunnel pixels are laid out in the captured frame.
	 * - BGR24: 3 bytes B, G, R (Magewell Pro).
	 * - ARGB, BGRA: 4 bytes in that order in memory, the alpha byte is ignored (DeckLink 8-bit RGB).
	 * - R210_HIGH8, R210_LOW8: DeckLink r210, a big endian 32-bit word with 10-bit R, G, B in bits 29-20,
	 *   19-10 and 9-0. The 8-bit value is in the top 8 bits of each 10-bit lane (HIGH8) or the bottom 8 (LOW8).
	 */
	enum class pixel_layout : uint8_t
	{
		BGR24,
		ARGB,
		BGRA,
		R210_HIGH8,
		R210_LOW8
	};

	constexpr const char* to_string(pixel_layout layout)
	{
		switch (layout)
		{
		case pixel_layout::BGR24: return "BGR24";
		case pixel_layout::ARGB: return "ARGB";
		case pixel_layout::BGRA: return "BGRA";
		case pixel_layout::R210_HIGH8: return "r210 high8";
		case pixel_layout::R210_LOW8: return "r210 low8";
		}
		return "?";
	}

	constexpr bool is_r210(pixel_layout layout)
	{
		return layout == pixel_layout::R210_HIGH8 || layout == pixel_layout::R210_LOW8;
	}

	/**
	 * The layout a card may really be delivering instead, the decoder tries both and the CRC decides.
	 * - r210: either bit placement.
	 * - ARGB and BGRA: the alpha byte is first or last, a 32-bit ARGB pixel stored little endian is BGRA in memory.
	 */
	constexpr pixel_layout alternative(pixel_layout layout)
	{
		switch (layout)
		{
		case pixel_layout::ARGB: return pixel_layout::BGRA;
		case pixel_layout::BGRA: return pixel_layout::ARGB;
		case pixel_layout::R210_HIGH8: return pixel_layout::R210_LOW8;
		case pixel_layout::R210_LOW8: return pixel_layout::R210_HIGH8;
		case pixel_layout::BGR24:
		default: return layout;
		}
	}

	constexpr size_t bytes_per_pixel(pixel_layout layout)
	{
		return layout == pixel_layout::BGR24 ? 3 : 4;
	}

	struct rgb8
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
	};

	template <pixel_layout L>
	rgb8 read_rgb(const uint8_t* p)
	{
		if constexpr (L == pixel_layout::BGR24)
		{
			return {p[2], p[1], p[0]};
		}
		else if constexpr (L == pixel_layout::ARGB)
		{
			return {p[1], p[2], p[3]};
		}
		else if constexpr (L == pixel_layout::BGRA)
		{
			return {p[2], p[1], p[0]};
		}
		else
		{
			constexpr unsigned shift = L == pixel_layout::R210_HIGH8 ? 2U : 0U;
			const uint32_t w = detail::be32(p);
			return {
				static_cast<uint8_t>((w >> 20 & 0x3FFU) >> shift),
				static_cast<uint8_t>((w >> 10 & 0x3FFU) >> shift),
				static_cast<uint8_t>((w & 0x3FFU) >> shift)
			};
		}
	}

	// calls f with std::integral_constant<pixel_layout, layout> so per pixel work is specialised by layout
	template <typename F>
	decltype(auto) with_layout(pixel_layout layout, F&& f)
	{
		switch (layout)
		{
		case pixel_layout::ARGB: return f(std::integral_constant<pixel_layout, pixel_layout::ARGB>{});
		case pixel_layout::BGRA: return f(std::integral_constant<pixel_layout, pixel_layout::BGRA>{});
		case pixel_layout::R210_HIGH8: return f(std::integral_constant<pixel_layout, pixel_layout::R210_HIGH8>{});
		case pixel_layout::R210_LOW8: return f(std::integral_constant<pixel_layout, pixel_layout::R210_LOW8>{});
		case pixel_layout::BGR24:
		default: return f(std::integral_constant<pixel_layout, pixel_layout::BGR24>{});
		}
	}

	// the start of the given row, frame is the top row and a negative stride reads a bottom-up frame
	inline const uint8_t* row_start(const uint8_t* frame, ptrdiff_t stride, size_t row)
	{
		return frame + static_cast<ptrdiff_t>(row) * stride;
	}

	// the metadata bit carried by the pixel at index `pixel` (raster order, from the top row) of a frame
	template <pixel_layout L>
	uint8_t pixel_bit(const uint8_t* frame, ptrdiff_t stride, uint32_t width, size_t pixel)
	{
		const auto [r, g, b] = read_rgb<L>(row_start(frame, stride, pixel / width) + pixel % width * bytes_per_pixel(L));
		return detail::parity8(static_cast<uint8_t>(r ^ g ^ b));
	}

	inline uint8_t bgr24_bit(const uint8_t* frame, ptrdiff_t stride, uint32_t width, size_t pixel)
	{
		return pixel_bit<pixel_layout::BGR24>(frame, stride, width, pixel);
	}

	/**
	 * Unpacks a tunnel frame to P210/P216 layout, i.e. 16-bit samples with the 12-bit value in the top bits, a
	 * Y plane followed by an interleaved CbCr plane at full height, top-down.
	 * @param src the top row of the frame, with a negative srcStride if the frame is bottom-up.
	 * @param dstStride the row length of each plane in bytes.
	 */
	template <pixel_layout L>
	void unpack(const uint8_t* src, ptrdiff_t srcStride, uint8_t* dstY, uint8_t* dstUV, size_t dstStride,
	            uint32_t width, uint32_t height)
	{
		constexpr size_t bpp = bytes_per_pixel(L);
		for (uint32_t row = 0; row < height; ++row)
		{
			const uint8_t* in = row_start(src, srcStride, row);
			auto* y = reinterpret_cast<uint16_t*>(dstY + row * dstStride);
			auto* uv = reinterpret_cast<uint16_t*>(dstUV + row * dstStride);
			for (uint32_t x = 0; x < width; ++x)
			{
				const auto [r, g, b] = read_rgb<L>(in + x * bpp);
				y[x] = static_cast<uint16_t>(g << 8 | (b & 0x0F) << 4);
				uv[x] = static_cast<uint16_t>(r << 8 | (b & 0xF0));
			}
		}
	}

	inline void unpack(pixel_layout layout, const uint8_t* src, ptrdiff_t srcStride, uint8_t* dstY, uint8_t* dstUV,
	                   size_t dstStride, uint32_t width, uint32_t height)
	{
		with_layout(layout, [&](auto l)
		{
			unpack<decltype(l)::value>(src, srcStride, dstY, dstUV, dstStride, width, height);
		});
	}

	inline void unpack_bgr24(const uint8_t* src, ptrdiff_t srcStride, uint8_t* dstY, uint8_t* dstUV, size_t dstStride,
	                         uint32_t width, uint32_t height)
	{
		unpack<pixel_layout::BGR24>(src, srcStride, dstY, dstUV, dstStride, width, height);
	}

	// black in the same layout, used when the tunnel is locked but there is no usable metadata
	inline void fill_black(uint8_t* dstY, uint8_t* dstUV, size_t dstStride, uint32_t width, uint32_t height)
	{
		for (uint32_t row = 0; row < height; ++row)
		{
			auto* y = reinterpret_cast<uint16_t*>(dstY + row * dstStride);
			auto* uv = reinterpret_cast<uint16_t*>(dstUV + row * dstStride);
			std::fill_n(y, width, static_cast<uint16_t>(0));
			std::fill_n(uv, width, static_cast<uint16_t>(0x8000));
		}
	}

	/**
	 * What the sink's EDID tells a source about Dolby Vision. A source only sends the RGB tunnel when the
	 * EDID has a Dolby Vision vendor specific video data block that supports "standard" (TV led) DV, a
	 * low latency only block gets LLDV (source tone mapped, ordinary 12-bit YCbCr) instead.
	 * Field layout as decoded by edid-decode (cta_dolby_video).
	 */
	struct edid_dolby_vision
	{
		bool present{false};
		uint8_t version{0};
		bool standard{false};
		bool lowLatency{false};
		// only signalled by v0 and v1 blocks
		bool supports2160p60Known{false};
		bool supports2160p60{false};
		std::vector<uint8_t> payload{};
	};

	inline edid_dolby_vision find_dolby_vision(const uint8_t* edid, size_t length)
	{
		edid_dolby_vision result;
		for (size_t offset = 128; offset + 128 <= length; offset += 128)
		{
			const uint8_t* block = edid + offset;
			// CTA-861 extension, data blocks run from byte 4 to the DTD offset
			if (block[0] != 0x02 || block[2] < 4)
			{
				continue;
			}
			const size_t end = std::min<size_t>(block[2], 127);
			for (size_t i = 4; i < end;)
			{
				const uint8_t tag = block[i] >> 5;
				const size_t len = block[i] & 0x1F;
				if (i + 1 + len > end)
				{
					break;
				}
				// extended tag 0x01 (vendor specific video) with Dolby's OUI 00-D0-46 (little endian)
				if (tag == 7 && len >= 5 && block[i + 1] == 0x01 && block[i + 2] == 0x46 && block[i + 3] == 0xD0 &&
					block[i + 4] == 0x00)
				{
					const uint8_t* x = block + i + 5;
					const size_t n = len - 4;
					result.present = true;
					result.payload.assign(x, x + n);
					result.version = x[0] >> 5 & 0x07;
					switch (result.version)
					{
					case 0:
						result.standard = true;
						result.supports2160p60Known = true;
						result.supports2160p60 = (x[0] & 0x02) != 0;
						break;
					case 1:
						result.standard = true;
						result.lowLatency = n > 3 && (x[3] & 0x01) != 0;
						result.supports2160p60Known = true;
						result.supports2160p60 = (x[0] & 0x02) != 0;
						break;
					case 2:
						if (n > 2)
						{
							// 0 low latency, 1 low latency + low latency HDMI, 2 standard + low latency, 3 all
							result.standard = (x[2] & 0x03) >= 2;
							result.lowLatency = true;
						}
						break;
					default:
						break;
					}
					return result;
				}
				i += 1 + len;
			}
		}
		return result;
	}

	struct decoder_stats
	{
		uint64_t frames{0};
		uint64_t rejectedFrames{0};
		uint64_t noMetadataFrames{0};
		uint64_t crcFailedCopies{0};
		uint64_t recoveredPackets{0};
		size_t lastMetadataLength{0};
		size_t lastPacketCount{0};
	};

	/**
	 * Tracks the tunnel across frames. Call process() for every captured frame then:
	 * - !locked(): not a DV tunnel, the frame is ordinary RGB
	 * - render(): decode the picture and send active() as its metadata
	 * - otherwise: locked but nothing usable, output black (never the raw RGB)
	 */
	class decoder
	{
	public:
		void reset()
		{
			*this = decoder{};
		}

		/**
		 * @param layout the layout the card reports. Its alternative() is also tried, the one the frame favours
		 * (or that last passed the CRC) first, and layout() reports the one in use.
		 * @param frame the top row of the frame, with a negative stride if the frame is bottom-up.
		 */
		void process(const uint8_t* frame, ptrdiff_t stride, uint32_t width, uint32_t height, uint64_t counter,
		             pixel_layout layout = pixel_layout::BGR24)
		{
			mStats.frames++;
			// end of stream applies from the picture after the one that signalled it
			if (mEndOfStream && counter > mEndOfStreamAfter)
			{
				mActive = {};
				mHasActiveId = false;
				mHasPending = false;
				mEndOfStream = false;
				mLocked = false;
			}
			// metadata announced for the next picture
			if (mHasPending && counter == mPendingAfter + 1)
			{
				mActive = mPending;
				mActiveId = mPendingAffectedId;
				mHasActiveId = true;
				mHasPending = false;
			}

			const size_t availablePackets = static_cast<size_t>(width) * height / (packetBits * repeats);
			std::array<uint8_t, packetBytes> packet{};
			auto readPacket = [&](size_t index)
			{
				if (index >= availablePackets)
				{
					return false;
				}
				for (size_t copy = 0; copy < repeats; ++copy)
				{
					packet.fill(0);
					const size_t first = (index * repeats + copy) * packetBits;
					with_layout(mLayout, [&](auto l)
					{
						for (size_t bit = 0; bit < packetBits; ++bit)
						{
							packet[bit / 8] |= static_cast<uint8_t>(
								pixel_bit<decltype(l)::value>(frame, stride, width, first + bit) << (7 - bit % 8));
						}
					});
					if (detail::crc32_mpeg2(packet.data(), packetBytes) == 0)
					{
						if (copy != 0)
						{
							mStats.recoveredPackets++;
						}
						return true;
					}
					mStats.crcFailedCopies++;
				}
				return false;
			};

			// only the CRC can confirm which of the two layouts is right
			std::array<pixel_layout, 2> candidates{layout, layout};
			size_t candidateCount = 1;
			if (alternative(layout) != layout)
			{
				auto preferred = mLayout == layout || mLayout == alternative(layout) ? mLayout : layout;
				// a dark picture can pass the CRC in both so try the one the unused bits favour first
				const auto misfits = count_misfits(frame, stride, width, height, layout);
				const auto altMisfits = count_misfits(frame, stride, width, height, alternative(layout));
				if (misfits != altMisfits)
				{
					preferred = misfits < altMisfits ? layout : alternative(layout);
				}
				candidates = {preferred, alternative(preferred)};
				candidateCount = 2;
			}
			bool firstPacket = false;
			for (size_t i = 0; i < candidateCount && !firstPacket; ++i)
			{
				mLayout = candidates[i];
				firstPacket = readPacket(0);
			}
			if (!firstPacket)
			{
				mLayout = candidates[0];
			}

			bool accepted = false;
			bool validHeader = false;
			detail::packet_header header;
			// the first packet must be a single or first packet
			if (firstPacket && detail::parse_header(packet.data(), header) && header.kind <= 1)
			{
				validHeader = true;
				mLocked = true;
				if (mHasPending)
				{
					if (header.currentId == mPendingAffectedId)
					{
						mActive = mPending;
						mActiveId = mPendingAffectedId;
						mHasActiveId = true;
						mHasPending = false;
					}
					else if (header.currentId != mPendingCurrentId)
					{
						mHasPending = false;
					}
				}
				if (mHasActiveId && header.currentId != mActiveId)
				{
					mActive = {};
					mHasActiveId = false;
				}
				if (header.endOfStream)
				{
					mHasPending = false;
					mEndOfStreamAfter = counter;
					mEndOfStream = true;
				}
				accepted = header.noMetadata || read_metadata(readPacket, packet, header, availablePackets);
				if (accepted)
				{
					if (header.noMetadata)
					{
						mStats.noMetadataFrames++;
					}
					else if (header.affectedId == header.currentId)
					{
						mActive = mParsed;
						mActiveId = header.currentId;
						mHasActiveId = true;
					}
					else if (!header.endOfStream)
					{
						mPending = mParsed;
						mPendingAffectedId = header.affectedId;
						mPendingCurrentId = header.currentId;
						mPendingAfter = counter;
						mHasPending = true;
					}
				}
			}
			mValidHeader = validHeader;
			if (accepted)
			{
				mMissingFrames = 0;
			}
			else
			{
				mStats.rejectedFrames++;
				mMissingFrames++;
				if (mLocked && mMissingFrames >= packetLossLimit)
				{
					mActive = {};
					mHasActiveId = false;
					mHasPending = false;
				}
			}
		}

		bool locked() const { return mLocked; }
		bool render() const { return mLocked && mValidHeader && mActive.valid; }
		const metadata& active() const { return mActive; }
		const decoder_stats& stats() const { return mStats; }
		// the layout of the last frame, i.e. the one that passed the CRC or the one tried first
		pixel_layout layout() const { return mLayout; }

	private:
		/**
		 * Samples the frame for pixels whose unused bits do not look unused in the given layout, i.e. r210 lanes
		 * with bits set outside the 8-bit value, or an alpha byte that differs from the first pixel's.
		 */
		static size_t count_misfits(const uint8_t* frame, ptrdiff_t stride, uint32_t width, uint32_t height,
		                            pixel_layout layout)
		{
			if (layout == pixel_layout::BGR24)
			{
				return 0;
			}
			const size_t pixels = static_cast<size_t>(width) * height;
			const size_t step = std::max<size_t>(1, pixels / 4096);
			const size_t alphaOffset = layout == pixel_layout::ARGB ? 0 : 3;
			const uint32_t r210Mask = layout == pixel_layout::R210_HIGH8 ? 0x00300C03U : 0x300C0300U;
			size_t misfits = 0;
			for (size_t pixel = 0; pixel < pixels; pixel += step)
			{
				const uint8_t* p = row_start(frame, stride, pixel / width) + pixel % width * 4;
				const bool misfit = is_r210(layout)
					                    ? (detail::be32(p) & r210Mask) != 0
					                    : p[alphaOffset] != frame[alphaOffset];
				misfits += misfit ? 1 : 0;
			}
			return misfits;
		}

		template <typename R>
		bool read_metadata(R& readPacket, std::array<uint8_t, packetBytes>& packet, const detail::packet_header& header,
		                   size_t availablePackets)
		{
			const size_t length = detail::be16(packet.data() + 3);
			if (length < 71 || length > maxMetadataBytes || (header.kind == 0 ? length > 119 : length <= 119))
			{
				return false;
			}
			const size_t packetCount = 1 + (length > 119 ? (length - 119 + 120) / 121 : 0);
			if (packetCount > availablePackets)
			{
				return false;
			}
			mBody.resize(length);
			size_t copied = std::min(length, size_t{119});
			std::memcpy(mBody.data(), packet.data() + 5, copied);
			const uint8_t flags = packet[0] & 0x3F;
			const uint8_t ids = packet[1];
			const uint8_t eos = packet[2];
			for (size_t index = 1; index < packetCount; ++index)
			{
				const unsigned expectedKind = index + 1 == packetCount ? 3 : 2;
				if (!readPacket(index) || (packet[0] >> 6) != expectedKind || (packet[0] & 0x3F) != flags ||
					packet[1] != ids || packet[2] != eos)
				{
					return false;
				}
				const size_t chunk = std::min(length - copied, size_t{121});
				std::memcpy(mBody.data() + copied, packet.data() + 3, chunk);
				copied += chunk;
			}
			if (copied != length || !detail::parse_metadata(mBody.data(), length, mParsed))
			{
				return false;
			}
			mStats.lastMetadataLength = length;
			mStats.lastPacketCount = packetCount;
			return true;
		}

		pixel_layout mLayout{pixel_layout::BGR24};
		bool mLocked{false};
		bool mValidHeader{false};
		uint32_t mMissingFrames{0};
		metadata mActive{};
		uint8_t mActiveId{0};
		bool mHasActiveId{false};
		metadata mPending{};
		uint8_t mPendingAffectedId{0};
		uint8_t mPendingCurrentId{0};
		uint64_t mPendingAfter{0};
		bool mHasPending{false};
		uint64_t mEndOfStreamAfter{0};
		bool mEndOfStream{false};
		metadata mParsed{};
		std::vector<uint8_t> mBody{};
		decoder_stats mStats{};
	};
}

#endif
