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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <streams.h>
#include "../common/dv_tunnel_lav.h"

#include <algorithm>
#include <array>
#include <memory>

#include <cstdint>
#include <vector>

namespace
{
	// an independent sender, the inverse of the decoder
	struct tunnel_frame
	{
		uint32_t width;
		uint32_t height;
		size_t stride;
		std::vector<uint8_t> bgr;

		tunnel_frame(uint32_t w, uint32_t h) : width(w), height(h), stride(w * 3), bgr(stride * h)
		{
		}

		uint8_t* pixel(size_t idx)
		{
			return bgr.data() + idx / width * stride + idx % width * 3;
		}

		void set_codes(size_t idx, uint16_t i, uint16_t c)
		{
			auto* p = pixel(idx);
			p[0] = static_cast<uint8_t>((c & 0xF) << 4 | (i & 0xF)); // B
			p[1] = static_cast<uint8_t>(i >> 4); // G
			p[2] = static_cast<uint8_t>(c >> 4); // R
		}

		// sets the parity of the pixel by flipping the I LSB if necessary
		void set_bit(size_t idx, uint8_t bit)
		{
			auto* p = pixel(idx);
			if (dv_tunnel::detail::parity8(static_cast<uint8_t>(p[0] ^ p[1] ^ p[2])) != bit)
			{
				p[0] ^= 1;
			}
		}

		void write_packet(size_t index, const std::array<uint8_t, 128>& packet, int corruptCopies = 0)
		{
			for (size_t copy = 0; copy < 3; ++copy)
			{
				const size_t first = (index * 3 + copy) * 1024;
				for (size_t bit = 0; bit < 1024; ++bit)
				{
					auto b = static_cast<uint8_t>(packet[bit / 8] >> (7 - bit % 8) & 1);
					if (static_cast<int>(copy) < corruptCopies && bit == 100)
					{
						b ^= 1;
					}
					set_bit(first + bit, b);
				}
			}
		}
	};

	void put16(std::vector<uint8_t>& v, size_t at, uint16_t x)
	{
		v[at] = static_cast<uint8_t>(x >> 8);
		v[at + 1] = static_cast<uint8_t>(x);
	}

	void put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
	{
		v[at] = static_cast<uint8_t>(x >> 24);
		v[at + 1] = static_cast<uint8_t>(x >> 16);
		v[at + 2] = static_cast<uint8_t>(x >> 8);
		v[at + 3] = static_cast<uint8_t>(x);
	}

	std::vector<uint8_t> metadata_body(size_t extraLevel2Bytes = 0)
	{
		std::vector<uint8_t> body(71);
		body[1] = 1; // scene refresh
		for (size_t i = 0; i < 9; ++i)
		{
			put16(body, 2 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(i == 0 ? 8192 : -100 + i)));
			put16(body, 32 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(i == 4 ? 16384 : 50 * i)));
		}
		for (size_t i = 0; i < 3; ++i)
		{
			put32(body, 20 + i * 4, (1U << 27) * static_cast<uint32_t>(i));
		}
		put16(body, 50, 0xFFFF);
		body[60] = 12;
		body[61] = 0;
		body[62] = 0;
		body[63] = 1;
		put16(body, 64, 62);
		put16(body, 66, 3696);
		put16(body, 68, 42);
		// L1
		std::vector<uint8_t> l1(11);
		put32(l1, 0, 6);
		l1[4] = 1;
		put16(l1, 5, 10);
		put16(l1, 7, 3000);
		put16(l1, 9, 1500);
		body.insert(body.end(), l1.begin(), l1.end());
		body[70] = 1;
		if (extraLevel2Bytes > 0)
		{
			std::vector<uint8_t> l2(5 + extraLevel2Bytes, 0x5A);
			put32(l2, 0, static_cast<uint32_t>(extraLevel2Bytes));
			l2[4] = 2;
			body.insert(body.end(), l2.begin(), l2.end());
			body[70] = 2;
		}
		return body;
	}

	std::array<uint8_t, 128> make_packet(uint8_t kind, bool noMd, uint8_t affected, uint8_t current, bool eos)
	{
		std::array<uint8_t, 128> p{};
		p[0] = static_cast<uint8_t>(kind << 6 | (noMd ? 1 : 0));
		p[1] = static_cast<uint8_t>(affected << 4 | current);
		p[2] = eos ? 1 : 0;
		return p;
	}

	void seal(std::array<uint8_t, 128>& p)
	{
		const auto crc = dv_tunnel::detail::crc32_mpeg2(p.data(), 124);
		p[124] = static_cast<uint8_t>(crc >> 24);
		p[125] = static_cast<uint8_t>(crc >> 16);
		p[126] = static_cast<uint8_t>(crc >> 8);
		p[127] = static_cast<uint8_t>(crc);
	}

	// writes the metadata (or a no_md header when body is empty) into the frame
	void send(tunnel_frame& f, const std::vector<uint8_t>& body, uint8_t affected = 0, uint8_t current = 0,
	          bool eos = false, int corruptCopies = 0)
	{
		if (body.empty())
		{
			auto p = make_packet(0, true, affected, current, eos);
			seal(p);
			f.write_packet(0, p, corruptCopies);
			return;
		}
		const bool single = body.size() <= 119;
		auto first = make_packet(single ? 0 : 1, false, affected, current, eos);
		first[3] = static_cast<uint8_t>(body.size() >> 8);
		first[4] = static_cast<uint8_t>(body.size());
		size_t copied = std::min(body.size(), size_t{119});
		std::copy_n(body.begin(), copied, first.begin() + 5);
		seal(first);
		f.write_packet(0, first, corruptCopies);
		for (size_t index = 1; copied < body.size(); ++index)
		{
			const size_t chunk = std::min(body.size() - copied, size_t{121});
			auto p = make_packet(copied + chunk == body.size() ? 3 : 2, false, affected, current, eos);
			std::copy_n(body.begin() + copied, chunk, p.begin() + 3);
			copied += chunk;
			seal(p);
			f.write_packet(index, p);
		}
	}
}

TEST(DvTunnel, UnpacksEvery12BitCode)
{
	tunnel_frame f(4096, 2);
	for (uint32_t x = 0; x < 4096; ++x)
	{
		f.set_codes(x, static_cast<uint16_t>(x), static_cast<uint16_t>(4095 - x));
		f.set_codes(4096 + x, static_cast<uint16_t>(4095 - x), static_cast<uint16_t>(x));
	}
	const size_t dstStride = 4096 * 2;
	std::vector<uint8_t> out(dstStride * 2 * 2);
	dv_tunnel::unpack_bgr24(f.bgr.data(), f.stride, out.data(), out.data() + dstStride * 2, dstStride, 4096, 2);
	const auto* y = reinterpret_cast<const uint16_t*>(out.data());
	const auto* uv = reinterpret_cast<const uint16_t*>(out.data() + dstStride * 2);
	for (uint32_t x = 0; x < 4096; ++x)
	{
		ASSERT_EQ(y[x], x << 4) << x;
		ASSERT_EQ(uv[x], (4095 - x) << 4) << x;
		ASSERT_EQ(y[4096 + x], (4095 - x) << 4) << x;
		ASSERT_EQ(uv[4096 + x], x << 4) << x;
	}
}

TEST(DvTunnel, PlainRgbIsNotATunnel)
{
	tunnel_frame f(1920, 16);
	for (size_t i = 0; i < f.bgr.size(); ++i)
	{
		f.bgr[i] = static_cast<uint8_t>(i * 7);
	}
	dv_tunnel::decoder d;
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	EXPECT_FALSE(d.locked());
	EXPECT_FALSE(d.render());
}

TEST(DvTunnel, DecodesSinglePacketMetadata)
{
	tunnel_frame f(1920, 16);
	send(f, metadata_body());
	dv_tunnel::decoder d;
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	ASSERT_TRUE(d.locked());
	ASSERT_TRUE(d.render());
	const auto& md = d.active();
	EXPECT_EQ(md.yccToRgb[0], 8192);
	EXPECT_EQ(md.yccToRgb[1], -99);
	EXPECT_EQ(md.rgbToLms[4], 16384);
	EXPECT_EQ(md.yccToRgbOffset[2], 1U << 28);
	EXPECT_EQ(md.signalBitDepth, 12);
	EXPECT_TRUE(md.fullRange());
	EXPECT_EQ(md.sourceMinPq, 62);
	EXPECT_EQ(md.sourceMaxPq, 3696);
	EXPECT_EQ(md.sourceDiagonal, 42);
	ASSERT_TRUE(md.hasL1);
	EXPECT_EQ(md.l1MinPq, 10);
	EXPECT_EQ(md.l1MaxPq, 3000);
	EXPECT_EQ(md.l1AvgPq, 1500);
	EXPECT_EQ(d.stats().lastPacketCount, 1u);
}

TEST(DvTunnel, ReassemblesMultiplePackets)
{
	tunnel_frame f(1920, 32);
	send(f, metadata_body(200));
	dv_tunnel::decoder d;
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	ASSERT_TRUE(d.render());
	EXPECT_EQ(d.stats().lastPacketCount, 3u);
	ASSERT_EQ(d.active().extensions.size(), 2u);
	EXPECT_EQ(d.active().extensions[1].level, 2);
	EXPECT_EQ(d.active().extensions[1].payload.size(), 200u);
	EXPECT_TRUE(d.active().hasL1);
}

TEST(DvTunnel, RecoversFromACorruptCopy)
{
	tunnel_frame f(1920, 16);
	send(f, metadata_body(), 0, 0, false, 2);
	dv_tunnel::decoder d;
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	EXPECT_TRUE(d.render());
	EXPECT_EQ(d.stats().crcFailedCopies, 2u);
	EXPECT_EQ(d.stats().recoveredPackets, 1u);
}

TEST(DvTunnel, PendingMetadataAppliesToTheNextFrame)
{
	dv_tunnel::decoder d;
	tunnel_frame f(1920, 16);
	// metadata for id 1 announced while id 0 is current
	send(f, metadata_body(), 1, 0);
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	EXPECT_TRUE(d.locked());
	EXPECT_FALSE(d.render()) << "nothing active yet so the frame must be black";

	tunnel_frame next(1920, 16);
	send(next, {}, 1, 1);
	d.process(next.bgr.data(), next.stride, next.width, next.height, 2);
	EXPECT_TRUE(d.render());
	EXPECT_EQ(d.active().l1MaxPq, 3000);
}

TEST(DvTunnel, NoMetadataKeepsTheActiveMetadata)
{
	dv_tunnel::decoder d;
	tunnel_frame f(1920, 16);
	send(f, metadata_body());
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	tunnel_frame next(1920, 16);
	send(next, {});
	d.process(next.bgr.data(), next.stride, next.width, next.height, 2);
	EXPECT_TRUE(d.render());
	EXPECT_EQ(d.stats().noMetadataFrames, 1u);
}

TEST(DvTunnel, StaysLockedAndBlackWithoutMetadataThenExpires)
{
	dv_tunnel::decoder d;
	tunnel_frame f(1920, 16);
	send(f, metadata_body());
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	ASSERT_TRUE(d.render());

	tunnel_frame plain(1920, 16);
	d.process(plain.bgr.data(), plain.stride, plain.width, plain.height, 2);
	EXPECT_TRUE(d.locked()) << "never fall back to raw RGB once locked";
	EXPECT_FALSE(d.render());
	for (uint64_t i = 3; i < 3 + dv_tunnel::packetLossLimit; ++i)
	{
		d.process(plain.bgr.data(), plain.stride, plain.width, plain.height, i);
	}
	EXPECT_TRUE(d.locked());
	EXPECT_FALSE(d.active().valid) << "metadata discarded after too many frames without it";
}

TEST(DvTunnel, EndOfStreamUnlocksAfterTheNextFrame)
{
	dv_tunnel::decoder d;
	tunnel_frame f(1920, 16);
	send(f, metadata_body(), 0, 0, true);
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	EXPECT_TRUE(d.locked());
	tunnel_frame plain(1920, 16);
	d.process(plain.bgr.data(), plain.stride, plain.width, plain.height, 2);
	EXPECT_FALSE(d.locked());
}

TEST(DvTunnel, MapsToLavWithIdentityReshaping)
{
	tunnel_frame f(1920, 16);
	send(f, metadata_body());
	dv_tunnel::decoder d;
	d.process(f.bgr.data(), f.stride, f.width, f.height, 1);
	ASSERT_TRUE(d.render());

	auto lav = std::make_unique<MediaSideDataDOVIMetadata>();
	dv_tunnel::to_lav(d.active(), *lav);
	EXPECT_EQ(lav->Header.bl_bit_depth, 12);
	EXPECT_EQ(lav->Header.bl_video_full_range_flag, 1);
	EXPECT_EQ(lav->Header.disable_residual_flag, 1);
	for (const auto& curve : lav->Mapping.curves)
	{
		EXPECT_EQ(curve.num_pivots, 2);
		EXPECT_EQ(curve.pivots[1], 4095);
		EXPECT_EQ(curve.poly_order[0], 1);
		EXPECT_EQ(curve.poly_coef[0][0], 0);
		EXPECT_EQ(curve.poly_coef[0][1], 1LL << lav->Header.coef_log2_denom);
	}
	EXPECT_DOUBLE_EQ(lav->ColorMetadata.ycc_to_rgb_matrix[0], 1.0);
	EXPECT_DOUBLE_EQ(lav->ColorMetadata.rgb_to_lms_matrix[4], 1.0);
	EXPECT_DOUBLE_EQ(lav->ColorMetadata.ycc_to_rgb_offset[1], 0.5);
	EXPECT_EQ(lav->ColorMetadata.source_max_pq, 3696);
	EXPECT_EQ(lav->ColorMetadata.signal_full_range_flag, 1);
	EXPECT_EQ(lav->Extensions[0].level, 1);
	EXPECT_EQ(lav->Extensions[0].Level1.max_pq, 3000);
	EXPECT_EQ(lav->Extensions[1].level, 0);
}

namespace
{
	// a base block plus one CTA extension holding a Dolby Vision VSVDB with the given payload
	std::vector<uint8_t> edid_with_dv(std::vector<uint8_t> payload, bool includeDv = true)
	{
		std::vector<uint8_t> edid(256);
		edid[126] = 1;
		uint8_t* cta = edid.data() + 128;
		cta[0] = 0x02;
		cta[1] = 0x03;
		size_t i = 4;
		// an unrelated video data block first
		cta[i++] = 2 << 5 | 2;
		cta[i++] = 16;
		cta[i++] = 97;
		if (includeDv)
		{
			cta[i++] = static_cast<uint8_t>(7 << 5 | (4 + payload.size()));
			cta[i++] = 0x01;
			cta[i++] = 0x46;
			cta[i++] = 0xD0;
			cta[i++] = 0x00;
			for (auto b : payload)
			{
				cta[i++] = b;
			}
		}
		cta[2] = static_cast<uint8_t>(i);
		return edid;
	}
}

TEST(DvEdid, NoDolbyVisionBlock)
{
	const auto edid = edid_with_dv({}, false);
	EXPECT_FALSE(dv_tunnel::find_dolby_vision(edid.data(), edid.size()).present);
	// base block only
	EXPECT_FALSE(dv_tunnel::find_dolby_vision(edid.data(), 128).present);
}

TEST(DvEdid, Version2LowLatencyOnlyCannotCarryTheTunnel)
{
	const auto edid = edid_with_dv({2 << 5 | 1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	const auto dv = dv_tunnel::find_dolby_vision(edid.data(), edid.size());
	EXPECT_TRUE(dv.present);
	EXPECT_EQ(dv.version, 2);
	EXPECT_FALSE(dv.standard);
	EXPECT_TRUE(dv.lowLatency);
	EXPECT_FALSE(dv.supports2160p60Known);
	EXPECT_EQ(dv.payload.size(), 7u);
}

TEST(DvEdid, Version2StandardSupportsTheTunnel)
{
	const auto edid = edid_with_dv({2 << 5 | 1, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
	const auto dv = dv_tunnel::find_dolby_vision(edid.data(), edid.size());
	EXPECT_TRUE(dv.standard);
	EXPECT_TRUE(dv.lowLatency);
}

TEST(DvEdid, Version1SignalsLowLatencyAnd2160p60)
{
	const auto edid = edid_with_dv({1 << 5 | 0x02, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0});
	const auto dv = dv_tunnel::find_dolby_vision(edid.data(), edid.size());
	EXPECT_EQ(dv.version, 1);
	EXPECT_TRUE(dv.standard);
	EXPECT_TRUE(dv.lowLatency);
	EXPECT_TRUE(dv.supports2160p60Known);
	EXPECT_TRUE(dv.supports2160p60);
}

TEST(DvEdid, Version0IsStandardOnly)
{
	const auto edid = edid_with_dv(std::vector<uint8_t>(21, 0));
	const auto dv = dv_tunnel::find_dolby_vision(edid.data(), edid.size());
	EXPECT_EQ(dv.version, 0);
	EXPECT_TRUE(dv.standard);
	EXPECT_FALSE(dv.lowLatency);
	EXPECT_FALSE(dv.supports2160p60);
}
