/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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

// packed formats
constexpr DWORD FOURCC_NV12 = MAKEFOURCC('N', 'V', '1', '2');
constexpr uint8_t BITS_NV12 = 12;
constexpr DWORD FOURCC_NV16 = MAKEFOURCC('N', 'V', '1', '6');
constexpr uint8_t BITS_NV16 = 16;
constexpr DWORD FOURCC_P010 = MAKEFOURCC('P', '0', '1', '0');
constexpr uint8_t BITS_P010 = 24;
constexpr DWORD FOURCC_P210 = MAKEFOURCC('P', '2', '1', '0');
constexpr uint8_t BITS_P210 = 32;
// unpacked formats
constexpr DWORD FOURCC_BGR24 = MAKEFOURCC('B', 'G', 'R', ' ');
constexpr uint8_t BITS_BGR24 = 24;
constexpr DWORD FOURCC_BGR10 = MAKEFOURCC('B', 'G', '1', '0');
constexpr uint8_t BITS_BGR10 = 32;
constexpr DWORD FOURCC_AYUV = MAKEFOURCC('A', 'Y', 'U', 'V');
constexpr uint8_t BITS_AYUV = 32;
// blackmagic
constexpr DWORD FOURCC_R210 = MAKEFOURCC('r', '2', '1', '0');
constexpr uint8_t BITS_R210 = 32;
constexpr DWORD FOURCC_RGBA = MAKEFOURCC('R', 'G', 'B', 'A');
constexpr uint8_t BITS_RGBA = 32;

// bit depth -> pixel encoding -> fourcc
// RGB444, YUV422, YUV444, YUY420
constexpr DWORD fourcc[3][4] = {
	// 8 bit
	{FOURCC_BGR24, FOURCC_NV16, FOURCC_AYUV, FOURCC_NV12},
	// 10 bit
	{FOURCC_BGR10, FOURCC_P210, FOURCC_AYUV, FOURCC_P010},
	// 12 bit
	{FOURCC_BGR10, FOURCC_P210, FOURCC_AYUV, FOURCC_P010},
};
constexpr std::string_view fourccName[3][4] = {
	{"BGR24", "NV16", "AYUV", "NV12"},
	{"BGR10", "P210", "AYUV", "P010"},
	{"BGR10", "P210", "AYUV", "P010"},
};
constexpr uint8_t fourccBitCount[3][4] = {
	// 8 bit
	{BITS_BGR24, BITS_NV16, BITS_AYUV, BITS_NV12},
	// 10 bit
	{BITS_BGR10, BITS_P210, BITS_AYUV, BITS_P010},
	// 12 bit
	{BITS_BGR10, BITS_P210, BITS_AYUV, BITS_P010},
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
	std::string outPixelLayout;
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

enum colour_format :std::uint8_t
{
	COLOUR_FORMAT_UNKNOWN = 0, ///<unknown color format
	RGB = 1, ///<RGB
	YUV601 = 2, ///<YUV601
	YUV709 = 3, ///<YUV709
	YUV2020 = 4, ///<YUV2020
	YUV2020C = 5, ///<YUV2020C
	P3D65 = 6 ///<P3D65
};

enum pixel_encoding:std::uint8_t
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

struct VIDEO_FORMAT
{
	colour_format colourFormat{YUV709};
	pixel_encoding pixelEncoding{YUV_420};
	byte bitDepth{8};
	int cx{3840};
	int cy{2160};
	double fps{50.0};
	LONGLONG frameInterval{200000};
	HDR_META hdrMeta{};
	int aspectX{16};
	int aspectY{9};
	quantisation_range quantisation{QUANTISATION_LIMITED};
	saturation_range saturation{SATURATION_LIMITED};
	// derived from the above attributes
	byte bitCount;
	DWORD pixelStructure; // fourcc
	std::string pixelStructureName;
	std::string colourFormatName{"YUV709"};
	DWORD lineLength;
	DWORD imageSize;
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

inline void GetImageDimensions(DWORD fourcc, int cx, int cy, DWORD* rowBytes, DWORD* imageBytes)
{
	DWORD byteAlignment = 2;
	DWORD cbLine;

	switch (fourcc)
	{
	case FOURCC_BGR24:
		cbLine = (cx * BITS_BGR24) / 8;
		break;
	case FOURCC_RGBA:
		cbLine = (cx * BITS_RGBA) / 8;
		break;
	case FOURCC_BGR10:
		cbLine = (cx * BITS_BGR10) / 8;
		break;
	case FOURCC_AYUV:
		cbLine = (cx * BITS_AYUV) / 8;
		break;
	case FOURCC_P010:
	case FOURCC_P210:
		cbLine = cx * 2;
		break;
	case FOURCC_R210:
		cbLine = (cx + 63) / 64 * 256;
		byteAlignment = 256;
		break;
	default:
		cbLine = cx;
		break;
	}

	*rowBytes = (cbLine + byteAlignment - 1) & ~(byteAlignment - 1);

	switch (fourcc)
	{
	case FOURCC_NV12:
	case FOURCC_P010:
		*imageBytes = *rowBytes * cy * 3 / 2;
		break;
	case FOURCC_NV16:
	case FOURCC_P210:
		*imageBytes = *rowBytes * cy * 2;
		break;
	default:
		*imageBytes = *rowBytes * cy;
	}
}
