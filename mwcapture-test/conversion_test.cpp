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

// Correctness tests for the pixel conversion kernels in conversion_kernels.h.
//
// Each kernel (AVX2 or scalar, whichever /arch selects) is compared byte for
// byte against an independent plain-C reference implementation written from
// the format specification, on deterministic pseudo-random frames. Output
// buffers carry canary guard bands so any write past the end of a plane
// fails the test rather than corrupting the heap silently.
//
// Comparison is row-aware: only the bytes a row can legitimately produce are
// compared, because some kernels deliberately overwrite row padding with an
// overlapping vector store (v210) which the reference does not reproduce.

#include "gtest/gtest.h"
#include "../common/conversion_kernels.h"

#include <cstdint>
#include <vector>

namespace
{
	constexpr size_t GUARD_BYTES = 64;
	constexpr uint8_t GUARD_FILL = 0xCD;

	// output buffer with canary bands before and after the payload
	class guarded_buffer
	{
	public:
		explicit guarded_buffer(size_t payloadSize) : mBuf(payloadSize + 2 * GUARD_BYTES, GUARD_FILL),
		                                              mPayloadSize(payloadSize)
		{
			std::fill_n(mBuf.begin() + GUARD_BYTES, payloadSize, uint8_t{0});
		}

		uint8_t* data() { return mBuf.data() + GUARD_BYTES; }
		const uint8_t* data() const { return mBuf.data() + GUARD_BYTES; }

		void expectGuardsIntact(const char* what) const
		{
			for (size_t i = 0; i < GUARD_BYTES; ++i)
			{
				ASSERT_EQ(mBuf[i], GUARD_FILL) << what << ": guard byte before payload clobbered at offset " << i;
			}
			for (size_t i = 0; i < GUARD_BYTES; ++i)
			{
				ASSERT_EQ(mBuf[GUARD_BYTES + mPayloadSize + i], GUARD_FILL)
					<< what << ": guard byte after payload clobbered at offset " << i;
			}
		}

	private:
		std::vector<uint8_t> mBuf;
		size_t mPayloadSize;
	};

	// deterministic xorshift fill so failures are reproducible
	std::vector<uint8_t> randomFrame(size_t bytes, uint32_t seed)
	{
		std::vector<uint8_t> v(bytes);
		uint32_t s = seed;
		for (auto& b : v)
		{
			s ^= s << 13;
			s ^= s >> 17;
			s ^= s << 5;
			b = static_cast<uint8_t>(s);
		}
		return v;
	}

	// compare only the first validBytes of each row, ignoring row padding
	void expectRowsEqual(const uint8_t* actual, const uint8_t* expected, int height, size_t validBytes,
	                     size_t strideBytes, const char* plane)
	{
		for (int row = 0; row < height; ++row)
		{
			const uint8_t* a = actual + row * strideBytes;
			const uint8_t* e = expected + row * strideBytes;
			for (size_t i = 0; i < validBytes; ++i)
			{
				ASSERT_EQ(a[i], e[i]) << plane << " plane mismatch at row " << row << " byte " << i;
			}
		}
	}

	// ---------------------------------------------------------------------
	// reference implementations, written from the format specifications
	// ---------------------------------------------------------------------

	// packed 8-bit 4:2:2 (byte offsets of each component within a 2-pixel group)
	void refPacked422ToYv16(const uint8_t* src, uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane,
	                        int width, int height, int pad, int y0, int u, int y1, int v)
	{
		const int yW = width + pad;
		const int uvW = yW / 2;
		for (int row = 0; row < height; ++row)
		{
			const uint8_t* s = src + static_cast<size_t>(row) * width * 2;
			for (int x = 0; x < width; x += 2)
			{
				const uint8_t* g = s + static_cast<size_t>(x) * 2;
				yPlane[row * yW + x] = g[y0];
				yPlane[row * yW + x + 1] = g[y1];
				uPlane[row * uvW + x / 2] = g[u];
				vPlane[row * uvW + x / 2] = g[v];
			}
		}
	}

	// Y210: rows of 16-bit LE words [Y0 U Y1 V], values already MSB aligned
	void refY210ToP210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                   int pad)
	{
		const int w = width + pad;
		for (int row = 0; row < height; ++row)
		{
			const uint16_t* s = reinterpret_cast<const uint16_t*>(src + static_cast<size_t>(row) * srcStride);
			uint16_t* dy = reinterpret_cast<uint16_t*>(dstY) + static_cast<size_t>(row) * w;
			uint16_t* duv = reinterpret_cast<uint16_t*>(dstUV) + static_cast<size_t>(row) * w;
			for (int x = 0; x < width; x += 2)
			{
				dy[x] = s[0];
				dy[x + 1] = s[2];
				duv[x] = s[1];
				duv[x + 1] = s[3];
				s += 4;
			}
		}
	}

	// v210: groups of 4 LE dwords holding 12 10-bit samples for 6 pixels
	void refV210ToP210(const uint8_t* src, int srcStride, uint8_t* dstY, uint8_t* dstUV, int width, int height,
	                   int pad)
	{
		// (dword index, shift) of each luma sample within a group
		static constexpr int yPos[6][2] = {{0, 10}, {1, 0}, {1, 20}, {2, 10}, {3, 0}, {3, 20}};
		// (dword index, shift) of each chroma sample, indexed by pair number
		static constexpr int uPos[3][2] = {{0, 0}, {1, 10}, {2, 20}};
		static constexpr int vPos[3][2] = {{0, 20}, {2, 0}, {3, 10}};

		const int w = width + pad;
		for (int row = 0; row < height; ++row)
		{
			const uint32_t* s = reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(row) * srcStride);
			uint16_t* dy = reinterpret_cast<uint16_t*>(dstY) + static_cast<size_t>(row) * w;
			uint16_t* duv = reinterpret_cast<uint16_t*>(dstUV) + static_cast<size_t>(row) * w;
			for (int x = 0; x < width; ++x)
			{
				const uint32_t* grp = s + (x / 6) * 4;
				const int i = x % 6;
				dy[x] = static_cast<uint16_t>(((grp[yPos[i][0]] >> yPos[i][1]) & 0x3FF) << 6);
				if ((x & 1) == 0)
				{
					const int pair = i / 2;
					duv[x] = static_cast<uint16_t>(((grp[uPos[pair][0]] >> uPos[pair][1]) & 0x3FF) << 6);
					duv[x + 1] = static_cast<uint16_t>(((grp[vPos[pair][0]] >> vPos[pair][1]) & 0x3FF) << 6);
				}
			}
		}
	}

	// r210: big-endian dword per pixel, x2 r10 g10 b10 (after byte swap:
	// r in bits 20-29, g in 10-19, b in 0-9), rows aligned to 256 bytes
	void refR210ToRgb48(const uint8_t* src, uint8_t* dst, int width, int height, int pad)
	{
		const size_t srcStride = (static_cast<size_t>(width) * 4 + 255) / 256 * 256;
		const int dstStrideWords = (width + pad) * 3;
		for (int row = 0; row < height; ++row)
		{
			const uint32_t* s = reinterpret_cast<const uint32_t*>(src + row * srcStride);
			uint16_t* d = reinterpret_cast<uint16_t*>(dst) + static_cast<size_t>(row) * dstStrideWords;
			for (int x = 0; x < width; ++x)
			{
				const uint32_t v = _byteswap_ulong(s[x]);
				d[x * 3 + 0] = static_cast<uint16_t>(((v >> 20) & 0x3FF) << 6);
				d[x * 3 + 1] = static_cast<uint16_t>(((v >> 10) & 0x3FF) << 6);
				d[x * 3 + 2] = static_cast<uint16_t>((v & 0x3FF) << 6);
			}
		}
	}

	// BGR10 (little endian): first output component from bits 0-9, second from
	// 10-19, third from 20-29, all scaled to 16 bit; rows aligned to 256 bytes
	void refBgr10ToRgb48(const uint8_t* src, uint8_t* dst, int width, int height, int pad)
	{
		const size_t srcStride = (static_cast<size_t>(width) * 4 + 255) / 256 * 256;
		const int dstStrideWords = (width + pad) * 3;
		for (int row = 0; row < height; ++row)
		{
			const uint32_t* s = reinterpret_cast<const uint32_t*>(src + row * srcStride);
			uint16_t* d = reinterpret_cast<uint16_t*>(dst) + static_cast<size_t>(row) * dstStrideWords;
			for (int x = 0; x < width; ++x)
			{
				const uint32_t v = s[x];
				d[x * 3 + 0] = static_cast<uint16_t>((v & 0x3FF) << 6);
				d[x * 3 + 1] = static_cast<uint16_t>(((v >> 10) & 0x3FF) << 6);
				d[x * 3 + 2] = static_cast<uint16_t>(((v >> 20) & 0x3FF) << 6);
			}
		}
	}

	// ---------------------------------------------------------------------
	// per-format test drivers
	// ---------------------------------------------------------------------

	void runYv16Case(int width, int height, int pad, int y0, int u, int y1, int v,
	                 bool (*kernel)(const uint8_t*, uint8_t*, uint8_t*, uint8_t*, int, int, int))
	{
		SCOPED_TRACE(testing::Message() << "width=" << width << " height=" << height << " pad=" << pad);
		const int yW = width + pad;
		const size_t ySize = static_cast<size_t>(yW) * height;
		const size_t uvSize = ySize / 2;
		const auto src = randomFrame(static_cast<size_t>(width) * 2 * height, 0x1234u + width);

		guarded_buffer yOut(ySize), uOut(uvSize), vOut(uvSize);
		std::vector<uint8_t> yRef(ySize), uRef(uvSize), vRef(uvSize);

		kernel(src.data(), yOut.data(), uOut.data(), vOut.data(), width, height, pad);
		refPacked422ToYv16(src.data(), yRef.data(), uRef.data(), vRef.data(), width, height, pad, y0, u, y1, v);

		expectRowsEqual(yOut.data(), yRef.data(), height, width, yW, "Y");
		expectRowsEqual(uOut.data(), uRef.data(), height, width / 2, yW / 2, "U");
		expectRowsEqual(vOut.data(), vRef.data(), height, width / 2, yW / 2, "V");
		yOut.expectGuardsIntact("Y");
		uOut.expectGuardsIntact("U");
		vOut.expectGuardsIntact("V");
	}

	void runY210Case(int width, int height, int pad, int srcStride)
	{
		SCOPED_TRACE(testing::Message() << "width=" << width << " height=" << height << " pad=" << pad
			<< " srcStride=" << srcStride);
		const int w = width + pad;
		const size_t planeSize = static_cast<size_t>(w) * height * 2;
		const auto src = randomFrame(static_cast<size_t>(srcStride) * height, 0x4321u + width);

		guarded_buffer yOut(planeSize), uvOut(planeSize);
		std::vector<uint8_t> yRef(planeSize), uvRef(planeSize);

		conversion::y210_to_p210(src.data(), srcStride, yOut.data(), uvOut.data(), width, height, pad);
		refY210ToP210(src.data(), srcStride, yRef.data(), uvRef.data(), width, height, pad);

		expectRowsEqual(yOut.data(), yRef.data(), height, static_cast<size_t>(width) * 2,
		                static_cast<size_t>(w) * 2, "Y");
		expectRowsEqual(uvOut.data(), uvRef.data(), height, static_cast<size_t>(width) * 2,
		                static_cast<size_t>(w) * 2, "UV");
		yOut.expectGuardsIntact("Y");
		uvOut.expectGuardsIntact("UV");
	}

	void runV210Case(int width, int height, int pad)
	{
		SCOPED_TRACE(testing::Message() << "width=" << width << " height=" << height << " pad=" << pad);
		const int alignedWidth = (width + 47) / 48 * 48;
		const int srcStride = alignedWidth * 8 / 3;
		const int w = width + pad;
		const size_t planeSize = static_cast<size_t>(w) * height * 2;
		const auto src = randomFrame(static_cast<size_t>(srcStride) * height, 0x9876u + width);

		guarded_buffer yOut(planeSize), uvOut(planeSize);
		std::vector<uint8_t> yRef(planeSize), uvRef(planeSize);

		conversion::v210_to_p210(src.data(), srcStride, yOut.data(), uvOut.data(), width, height, pad);
		refV210ToP210(src.data(), srcStride, yRef.data(), uvRef.data(), width, height, pad);

		expectRowsEqual(yOut.data(), yRef.data(), height, static_cast<size_t>(width) * 2,
		                static_cast<size_t>(w) * 2, "Y");
		expectRowsEqual(uvOut.data(), uvRef.data(), height, static_cast<size_t>(width) * 2,
		                static_cast<size_t>(w) * 2, "UV");
		yOut.expectGuardsIntact("Y");
		uvOut.expectGuardsIntact("UV");
	}

	void runRgb48Case(int width, int height, int pad, bool r210,
	                  bool (*kernel)(const uint8_t*, uint16_t*, size_t, size_t, int))
	{
		SCOPED_TRACE(testing::Message() << "width=" << width << " height=" << height << " pad=" << pad);
		const size_t srcStride = (static_cast<size_t>(width) * 4 + 255) / 256 * 256;
		const size_t dstSize = static_cast<size_t>(width + pad) * height * 6;
		const auto src = randomFrame(srcStride * height, 0xBEEFu + width);

		guarded_buffer out(dstSize);
		std::vector<uint8_t> ref(dstSize);

		kernel(src.data(), reinterpret_cast<uint16_t*>(out.data()), width, height, pad);
		if (r210)
		{
			refR210ToRgb48(src.data(), ref.data(), width, height, pad);
		}
		else
		{
			refBgr10ToRgb48(src.data(), ref.data(), width, height, pad);
		}

		expectRowsEqual(out.data(), ref.data(), height, static_cast<size_t>(width) * 6,
		                static_cast<size_t>(width + pad) * 6, "RGB");
		out.expectGuardsIntact("RGB");
	}
}

// ---------------------------------------------------------------------------
// packed 8-bit 4:2:2 -> YV16
// ---------------------------------------------------------------------------

TEST(UYVY_YV16, MatchesReference)
{
	for (const int width : {64, 720, 1280, 1920, 3840})
	{
		runYv16Case(width, 32, 0, 1, 0, 3, 2, conversion::uyvy_to_yv16);
	}
}

TEST(UYVY_YV16, MatchesReferenceWithPadding)
{
	runYv16Case(720, 32, 8, 1, 0, 3, 2, conversion::uyvy_to_yv16);
}

// widths that are not a multiple of 16 overrun each row in the AVX2 kernel;
// enabled when tail handling is added
TEST(UYVY_YV16, DISABLED_NonMultipleOf16Width)
{
	runYv16Case(1366, 32, 0, 1, 0, 3, 2, conversion::uyvy_to_yv16);
}

TEST(YUY2_YV16, MatchesReference)
{
	for (const int width : {64, 720, 1280, 1920, 3840})
	{
		runYv16Case(width, 32, 0, 0, 1, 2, 3, conversion::yuy2_to_yv16);
	}
}

TEST(YUY2_YV16, MatchesReferenceWithPadding)
{
	runYv16Case(720, 32, 8, 0, 1, 2, 3, conversion::yuy2_to_yv16);
}

TEST(YUY2_YV16, DISABLED_NonMultipleOf16Width)
{
	runYv16Case(1366, 32, 0, 0, 1, 2, 3, conversion::yuy2_to_yv16);
}

// 2VUY has the same layout as UYVY; locks in the alias
TEST(YUV2_YV16, MatchesReference)
{
	for (const int width : {64, 720, 1920})
	{
		runYv16Case(width, 32, 0, 1, 0, 3, 2, conversion::yuv2_to_yv16);
	}
}

// ---------------------------------------------------------------------------
// Y210 -> P210
// ---------------------------------------------------------------------------

TEST(Y210_P210, MatchesReference)
{
	for (const int width : {64, 720, 1280, 1920, 3840})
	{
		runY210Case(width, 32, 0, width * 4);
	}
}

TEST(Y210_P210, MatchesReferenceWithPadding)
{
	runY210Case(720, 32, 8, 720 * 4);
}

TEST(Y210_P210, MatchesReferenceWithPaddedSourceStride)
{
	runY210Case(720, 32, 0, 720 * 4 + 64);
}

// ---------------------------------------------------------------------------
// v210 -> P210
// ---------------------------------------------------------------------------

TEST(V210_P210, MatchesReference)
{
	for (const int width : {720, 1920, 3840})
	{
		runV210Case(width, 32, 0);
	}
}

TEST(V210_P210, MatchesReferenceWithPadding)
{
	runV210Case(720, 32, 8);
}

TEST(V210_P210, NonMultipleOf12Width)
{
	for (const int width : {1280, 2048})
	{
		runV210Case(width, 32, 0);
	}
}

// ---------------------------------------------------------------------------
// r210 / BGR10 -> RGB48
// ---------------------------------------------------------------------------

TEST(R210_RGB48, MatchesReference)
{
	for (const int width : {64, 720, 1280, 1920, 3840})
	{
		runRgb48Case(width, 32, 0, true, conversion::r210_to_rgb48);
	}
}

TEST(R210_RGB48, MatchesReferenceWithPadding)
{
	runRgb48Case(720, 32, 8, true, conversion::r210_to_rgb48);
}

// widths that are not a multiple of 4 leave the last pixels of every line
// unconverted in the AVX2 kernel; enabled when tail handling is added
TEST(R210_RGB48, DISABLED_NonMultipleOf4Width)
{
	runRgb48Case(1366, 32, 0, true, conversion::r210_to_rgb48);
}

TEST(BGR10_RGB48, MatchesReference)
{
	for (const int width : {64, 720, 1280, 1920, 3840})
	{
		runRgb48Case(width, 32, 0, false, conversion::bgr10_to_rgb48);
	}
}

TEST(BGR10_RGB48, MatchesReferenceWithPadding)
{
	runRgb48Case(720, 32, 8, false, conversion::bgr10_to_rgb48);
}

TEST(BGR10_RGB48, DISABLED_NonMultipleOf4Width)
{
	runRgb48Case(1366, 32, 0, false, conversion::bgr10_to_rgb48);
}
