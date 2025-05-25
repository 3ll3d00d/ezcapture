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
#define NOMINMAX // quill does not compile without this

#include <map>
#include <LibMWCapture/MWHDMIPackets.h>
#include "domain.h"

constexpr auto chromaticity_scale_factor = 0.00002;
constexpr auto high_luminance_scale_factor = 1.0;
constexpr auto low_luminance_scale_factor = 0.0001;

// bit depth -> pixel_encoding -> pixel_format
// rgb - 4:2:2 - 4:4:4 - 4:2:0
const pixel_format proPixelFormats[3][4] = {
	// 8 bit
	{BGR24, NV16, AYUV, NV12},
	// 10 bit
	{BGR10, P210, AYUV, P010},
	// 12 bit
	{BGR10, P210, AYUV, P010}
};

const pixel_format usbPlusPixelFormats[3][4] = {
	{BGR24, YUY2, NA, NV12},
	{NA, NA, NA, NA},
	{NA, NA, NA, NA}
};

const pixel_format usbProPixelFormats[3][4] = {
	{BGR24, YUY2, NA, NV12},
	{NA, V210, NA, P010},
	{NA, NA, NA, NA}
};

enum frame_writer_strategy :uint8_t
{
	YUY2_YV16,
	V210_P210,
	BGR10_BGR48,
	STRAIGHT_THROUGH
};

inline const char* to_string(frame_writer_strategy e)
{
	switch (e)
	{
	case YUY2_YV16: return "YUY2_YV16";
	case V210_P210: return "V210_P210";
	case BGR10_BGR48: return "BGR10_BGR48";
	case STRAIGHT_THROUGH: return "STRAIGHT_THROUGH";
	default: return "unknown";
	}
}

typedef std::map<pixel_format, std::pair<pixel_format, frame_writer_strategy>> pixel_format_fallbacks;

const pixel_format_fallbacks pixelFormatFallbacks{
	{YUY2, {YV16, YUY2_YV16}},
	{V210, {P210, V210_P210}},
	{BGR10, {RGB48, BGR10_BGR48}},
};

// utility functions
inline void LoadHdrMeta(HDR_META* meta, const HDMI_HDR_INFOFRAME_PAYLOAD* frame)
{
	auto hdrIn = *frame;
	auto hdrOut = meta;

	// https://shop.cta.tech/products/hdr-static-metadata-extensions
	// hdrInfo.byEOTF : 0 = SDR gamma, 1 = HDR gamma, 2 = ST2084
	int primaries_x[] = {
		hdrIn.display_primaries_lsb_x0 + (hdrIn.display_primaries_msb_x0 << 8),
		hdrIn.display_primaries_lsb_x1 + (hdrIn.display_primaries_msb_x1 << 8),
		hdrIn.display_primaries_lsb_x2 + (hdrIn.display_primaries_msb_x2 << 8)
	};
	int primaries_y[] = {
		hdrIn.display_primaries_lsb_y0 + (hdrIn.display_primaries_msb_y0 << 8),
		hdrIn.display_primaries_lsb_y1 + (hdrIn.display_primaries_msb_y1 << 8),
		hdrIn.display_primaries_lsb_y2 + (hdrIn.display_primaries_msb_y2 << 8)
	};
	// red = largest x, green = largest y, blue = remaining 
	auto r_idx = 0;
	auto maxVal = primaries_x[0];
	for (int i = 1; i < 3; ++i)
	{
		if (primaries_x[i] > maxVal)
		{
			maxVal = primaries_x[i];
			r_idx = i;
		}
	}

	auto g_idx = 0;
	maxVal = primaries_y[0];
	for (int i = 1; i < 3; ++i)
	{
		if (primaries_y[i] > maxVal)
		{
			maxVal = primaries_y[i];
			g_idx = i;
		}
	}

	if (g_idx != r_idx)
	{
		auto b_idx = 3 - g_idx - r_idx;
		if (b_idx != g_idx && b_idx != r_idx)
		{
			hdrOut->r_primary_x = primaries_x[r_idx] * chromaticity_scale_factor;
			hdrOut->r_primary_y = primaries_y[r_idx] * chromaticity_scale_factor;
			hdrOut->g_primary_x = primaries_x[g_idx] * chromaticity_scale_factor;
			hdrOut->g_primary_y = primaries_y[g_idx] * chromaticity_scale_factor;
			hdrOut->b_primary_x = primaries_x[b_idx] * chromaticity_scale_factor;
			hdrOut->b_primary_y = primaries_y[b_idx] * chromaticity_scale_factor;
		}
	}

	hdrOut->whitepoint_x = (hdrIn.white_point_lsb_x + (hdrIn.white_point_msb_x << 8)) * chromaticity_scale_factor;
	hdrOut->whitepoint_y = (hdrIn.white_point_lsb_y + (hdrIn.white_point_msb_y << 8)) * chromaticity_scale_factor;

	hdrOut->maxDML = (hdrIn.max_display_mastering_lsb_luminance + (hdrIn.max_display_mastering_msb_luminance << 8)) *
		high_luminance_scale_factor;
	hdrOut->minDML = (hdrIn.min_display_mastering_lsb_luminance + (hdrIn.min_display_mastering_msb_luminance << 8)) *
		low_luminance_scale_factor;

	hdrOut->maxCLL = hdrIn.maximum_content_light_level_lsb + (hdrIn.maximum_content_light_level_msb << 8);
	hdrOut->maxFALL = hdrIn.maximum_frame_average_light_level_lsb + (hdrIn.maximum_frame_average_light_level_msb << 8);

	hdrOut->transferFunction = hdrIn.byEOTF == 0x2 ? 15 : 4;

	hdrOut->exists = hdrMetaExists(hdrOut);
}
