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
#pragma once
#include <string>
#include <array>
#include <ks.h>
#include <ksmedia.h>

constexpr auto not_present = 1024;
constexpr LONGLONG dshowTicksPerSecond = 10LL * 1000 * 1000; // 100ns

enum colour_format :std::uint8_t
{
	COLOUR_FORMAT_UNKNOWN = 0, ///<unknown color format
	RGB = 1, ///<RGB
	REC601 = 2, ///<REC601
	REC709 = 3, ///<REC709
	BT2020 = 4, ///<BT2020
	BT2020C = 5, ///<BT2020C
	P3D65 = 6 ///<P3D65
};

enum pixel_encoding :std::uint8_t
{
	RGB_444 = 0, ///<RGB444
	YUV_422 = 1, ///<YUV422
	YUV_444 = 2, ///<YUV444
	YUV_420 = 3 ///<YUV420
};

enum quantisation_range : std::uint8_t
{
	QUANTISATION_UNKNOWN = 0x00, ///<the default quantisation range
	QUANTISATION_FULL = 0x01,
	///<Full range, which has 8-bit data. The black-white color range is 0-255/1023/4095/65535.
	QUANTISATION_LIMITED = 0x02
	///<Limited range, which has 8-bit data. The black-white color range is 16/64/256/4096-235(240)/940(960)/3760(3840)/60160(61440).
};

enum saturation_range : std::uint8_t
{
	SATURATION_UNKNOWN = 0x00, ///<The default saturation range
	SATURATION_FULL = 0x01, ///<Full range, which has 8-bit data. The black-white color range is 0-255/1023/4095/65535
	SATURATION_LIMITED = 0x02,
	///<Limited range, which has 8-bit data. The black-white color range is 16/64/256/4096-235(240)/940(960)/3760(3840)/60160(61440)
	EXTENDED_GAMUT = 0x03
	///<Extended range, which has 8-bit data. The black-white color range is 1/4/16/256-254/1019/4079/65279
};

struct pixel_format
{
	enum format:uint8_t
	{
		NV12,
		NV16,
		P010,
		P210,
		AYUV,
		BGR24,
		BGR10,
		YUV2,
		YV16,
		V210,
		AY10,
		ARGB,
		BGRA,
		RGBA,
		R210,
		R12B,
		R12L,
		R10B,
		R10L
	};

	pixel_format(format pf, char a, char b, char c, char d, uint8_t pBitDepth, bool pRgb,
	             pixel_encoding pPixelEncoding, DWORD pByteAlignment = 2)
	{
		format = pf;
		fourcc = MAKEFOURCC(a, b, c, d);
		bitDepth = pBitDepth;
		bitCount = pBitDepth * 3;
		name = std::string{a, b, c, d};
		rgb = pRgb;
		byteAlignment = pByteAlignment;
		subsampling = pPixelEncoding;
	}

	format format;
	DWORD fourcc;
	uint8_t bitDepth;
	uint8_t bitCount;
	std::string name;
	bool rgb;
	DWORD byteAlignment;
	pixel_encoding subsampling;

	void GetImageDimensions(int cx, int cy, DWORD* rowBytes, DWORD* imageBytes) const
	{
		DWORD cbLine;
		switch (format)
		{
		case R210:
		case AY10:
		case R10B:
		case R10L:
			cbLine = (cx + 63) / 64 * 256;
			break;
		case V210:
			cbLine = (cx + 47) / 48 * 128;
			break;
		case YUV2:
		case P010:
		case P210:
			cbLine = cx * 2;
			break;
		case BGR24:
			cbLine = cx * 3;
			break;
		case NV16:
		case YV16:
		case NV12:
			cbLine = cx;
			break;
		case AYUV:
		case BGR10:
		case ARGB:
		case BGRA:
		case RGBA:
			cbLine = cx * 4;
			break;
		case R12B:
		case R12L:
		default: // NOLINT(clang-diagnostic-covered-switch-default)
			cbLine = cx * bitCount / 8;
		}

		*rowBytes = (cbLine + byteAlignment - 1) & ~(byteAlignment - 1);
		*imageBytes = *rowBytes * cy;

		if (format == NV12 || format == P010)
		{
			*imageBytes = *imageBytes * 3 / 2;
		} else if (format == YV16 || format == NV16 || format == P210)
		{
			*imageBytes = *imageBytes * 2;
		}
	}

	DWORD GetBiCompression() const
	{
		return rgb ? BI_RGB : fourcc;
	}

	// required to allow use in a std::map
	bool operator<(const pixel_format& rhs) const noexcept
	{
		return this->format < rhs.format;
	}

	bool operator==(const pixel_format& rhs) const noexcept
	{
		return this->format == rhs.format;
	}
};

// magewell
const inline pixel_format NV12{pixel_format::NV12, 'N', 'V', '1', '2', 8, false, YUV_420};
const inline pixel_format NV16{pixel_format::NV16, 'N', 'V', '1', '6', 8, false, YUV_422};
const inline pixel_format P010{pixel_format::P010, 'P', '0', '1', '0', 10, false, YUV_420};
const inline pixel_format P210{pixel_format::P210, 'P', '2', '1', '0', 10, false, YUV_422};
const inline pixel_format AYUV{pixel_format::AYUV, 'A', 'Y', 'U', 'V', 8, false, YUV_444};
const inline pixel_format BGR24{pixel_format::BGR24, 'B', 'G', 'R', ' ', 8, true, RGB_444};
const inline pixel_format BGR10{pixel_format::BGR10, 'B', 'G', '1', '0', 10, true, RGB_444};
// blackmagic, generally require conversion due to lack of native renderer support
const inline pixel_format YUV2{pixel_format::YUV2, '2', 'V', 'U', 'Y', 8, false, YUV_422};
const inline pixel_format V210{pixel_format::V210, 'v', '2', '1', '0', 10, false, YUV_422, 128};
const inline pixel_format AY10{pixel_format::AY10, 'A', 'y', '1', '0', 10, false, YUV_422, 256};
const inline pixel_format ARGB{pixel_format::ARGB, 'A', 'R', 'G', 'B', 8, true, RGB_444};
const inline pixel_format BGRA{pixel_format::BGRA, 'B', 'G', 'R', 'A', 8, true, RGB_444};
const inline pixel_format RGBA{pixel_format::RGBA, 'R', 'G', 'B', 'A', 8, true, RGB_444};
const inline pixel_format R210{pixel_format::R210, 'r', '2', '1', '0', 10, false, RGB_444, 256};
const inline pixel_format R12B{pixel_format::R12B, 'R', '1', '2', 'B', 12, false, RGB_444};
const inline pixel_format R12L{pixel_format::R12L, 'R', '1', '2', 'L', 12, false, RGB_444};
const inline pixel_format R10L{pixel_format::R10L, 'R', '1', '0', 'l', 10, false, RGB_444, 256};
const inline pixel_format R10B{pixel_format::R10B, 'R', '1', '0', 'b', 10, false, RGB_444, 256};
// jrvr
const inline pixel_format YV16{pixel_format::YV16, 'Y', 'V', '1', '6', 8, false, YUV_422};

const pixel_format ALL_PIXEL_FORMATS[] = {
	NV12,
	NV16,
	P010,
	P210,
	AYUV,
	BGR24,
	BGR10,
	YUV2,
	YV16,
	V210,
	AY10,
	ARGB,
	BGRA,
	RGBA,
	R210,
	R12B,
	R12L,
	R10L,
	R10B
};

struct DEVICE_STATUS
{
	std::string deviceDesc{};
};

struct HDR_META
{
	bool exists{false};
	double r_primary_x{0.0};
	double r_primary_y{0.0};
	double g_primary_x{0.0};
	double g_primary_y{0.0};
	double b_primary_x{0.0};
	double b_primary_y{0.0};
	double whitepoint_x{0.0};
	double whitepoint_y{0.0};
	double minDML{0.0};
	double maxDML{0.0};
	int maxCLL{0};
	int maxFALL{0};
	int transferFunction{4};
};

inline boolean hdrMetaExists(const HDR_META* hdrOut)
{
	return
		hdrOut->r_primary_x != 0.0 &&
		hdrOut->r_primary_y != 0.0 &&
		hdrOut->g_primary_x != 0.0 &&
		hdrOut->g_primary_y != 0.0 &&
		hdrOut->b_primary_x != 0.0 &&
		hdrOut->b_primary_y != 0.0 &&
		hdrOut->whitepoint_x != 0.0 &&
		hdrOut->whitepoint_y != 0.0 &&
		hdrOut->minDML != 0.0 &&
		hdrOut->maxDML != 0.0 &&
		hdrOut->maxCLL != 0 &&
		hdrOut->maxFALL != 0;
}

struct AUDIO_INPUT_STATUS
{
	bool audioInStatus;
	bool audioInIsPcm;
	unsigned char audioInBitDepth;
	unsigned long audioInFs;
	unsigned short audioInChannelPairs;
	unsigned char audioInChannelMap;
	unsigned char audioInLfeLevel;
};

struct AUDIO_OUTPUT_STATUS
{
	std::string audioOutChannelLayout;
	unsigned char audioOutBitDepth;
	std::string audioOutCodec;
	unsigned long audioOutFs;
	short audioOutLfeOffset;
	int audioOutLfeChannelIndex;
	unsigned short audioOutChannelCount;
	uint16_t audioOutDataBurstSize;
};

struct VIDEO_INPUT_STATUS
{
	int inX{-1};
	int inY{-1};
	int inAspectX{-1};
	int inAspectY{-1};
	std::string signalStatus;
	std::string inColourFormat;
	std::string inQuantisation;
	std::string inSaturation;
	double inFps;
	uint64_t inFrameDuration{0};
	int inBitDepth{0};
	std::string inPixelLayout;
	bool validSignal{false};
};

struct VIDEO_OUTPUT_STATUS
{
	int outX{-1};
	int outY{-1};
	int outAspectX{-1};
	int outAspectY{-1};
	std::string outColourFormat;
	std::string outQuantisation;
	std::string outSaturation;
	double outFps;
	int outBitDepth{0};
	std::string outSubsampling;
	std::string outPixelStructure;
	std::string outTransferFunction;
};

struct HDR_STATUS
{
	bool hdrOn{false};
	double hdrPrimaryRX;
	double hdrPrimaryRY;
	double hdrPrimaryGX;
	double hdrPrimaryGY;
	double hdrPrimaryBX;
	double hdrPrimaryBY;
	double hdrWpX;
	double hdrWpY;
	double hdrMinDML;
	double hdrMaxDML;
	double hdrMaxCLL;
	double hdrMaxFALL;
};

struct VIDEO_FORMAT
{
	colour_format colourFormat{REC709};
	pixel_format pixelFormat{NV12};
	int cx{3840};
	int cy{2160};
	double fps{50.0};
	LONGLONG frameInterval{200000};
	HDR_META hdrMeta{};
	int aspectX{16};
	int aspectY{9};
	quantisation_range quantisation{QUANTISATION_UNKNOWN};
	saturation_range saturation{SATURATION_UNKNOWN};
	// derived from the above attributes
	std::string colourFormatName{"REC709"};
	DWORD lineLength{0};
	DWORD imageSize{0};

	void CalculateDimensions()
	{
		pixelFormat.GetImageDimensions(cx, cy, &lineLength, &imageSize);
	}
};

enum Codec
{
	PCM,
	AC3,
	DTS,
	DTSHD,
	EAC3,
	TRUEHD,
	BITSTREAM,
	PAUSE_OR_NULL
};

static const std::string codecNames[8] = {
	"PCM",
	"AC3",
	"DTS",
	"DTSHD",
	"EAC3",
	"TrueHD",
	"Unidentified",
	"PAUSE_OR_NULL"
};

struct AUDIO_FORMAT
{
	boolean pcm{true};
	DWORD fs{48000};
	double sampleInterval{static_cast<double>(dshowTicksPerSecond) / 48000};
	BYTE bitDepth{16};
	BYTE bitDepthInBytes{2};
	BYTE channelAllocation{0x00};
	WORD channelValidityMask{0};
	WORD inputChannelCount{2};
	WORD outputChannelCount{2};
	std::array<int, 8> channelOffsets{
		0, 0, not_present, not_present, not_present, not_present, not_present, not_present
	};
	WORD channelMask{KSAUDIO_SPEAKER_STEREO};
	std::string channelLayout;
	int lfeChannelIndex{not_present};
	double lfeLevelAdjustment{1.0};
	Codec codec{PCM};
	// encoded content only
	uint16_t dataBurstSize{0};
};
