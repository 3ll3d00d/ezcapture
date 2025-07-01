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
#ifndef MWDOMAIN_HEADER
#define MWDOMAIN_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <LibMWCapture/MWCaptureDef.h>
#include <LibMWCapture/MWHDMIPackets.h>
#include "domain.h"

enum DeviceType : uint8_t
{
	USB_PLUS,
	USB_PRO,
	PRO
};

inline const char* devicetype_to_name(DeviceType e)
{
	switch (e)
	{
	case USB_PLUS: return "USB_PLUS";
	case USB_PRO: return "USB_PRO";
	case PRO: return "PRO";
	default: return "unknown";
	}
}

inline constexpr auto chromaticity_scale_factor = 0.00002;
inline constexpr auto high_luminance_scale_factor = 1.0;
inline constexpr auto low_luminance_scale_factor = 0.0001;

inline constexpr int bitDepthCount = 3;
inline constexpr int subsamplingCount = 4;

typedef std::array<std::array<pixel_format, 4>, 3> pixel_format_by_bit_depth_subsampling;
typedef std::map<pixel_format, std::vector<pixel_format>> pixel_format_alternatives;

// bit depth -> pixel_encoding -> pixel_format
// rgb - 4:2:2 - 4:4:4 - 4:2:0
// 8 - 10 - 12 bit
const pixel_format_by_bit_depth_subsampling proPixelFormats{
	{
		{BGR24, NV16, AYUV, NV12},
		{BGR10, P210, AYUV, P010},
		{BGR10, P210, AYUV, P010}
	}
};

const pixel_format_by_bit_depth_subsampling usbPlusPixelFormats{
	{
		{BGR24, UYVY, UYVY, NV12},
		{BGR24, UYVY, UYVY, NV12},
		{BGR24, UYVY, UYVY, NV12},
	}
};

const pixel_format_by_bit_depth_subsampling usbProPixelFormats{
	{
		{BGR24, Y210, Y210, P010},
		{BGR24, Y210, Y210, P010},
		{BGR24, Y210, Y210, P010},
	}
};

const pixel_format_alternatives usbProPixelFormatAlternatives = {
	{BGR24, {Y210, UYVY, YUY2, P010, NV12}},
	{Y210, {P010, BGR24, UYVY, YUY2, NV12}},
	{P010, {Y210, BGR24, NV12, UYVY, YUY2}},
};

const pixel_format_alternatives usbPlusPixelFormatAlternatives = {
	{BGR24, {UYVY, YUY2, NV12}},
	{UYVY, {YUY2, BGR24, NV12}},
	{NV12, {UYVY, YUY2, BGR24}},
};

const std::map<DeviceType, pixel_format_alternatives> formatAlternativesByDeviceType = {
	{USB_PLUS, usbPlusPixelFormatAlternatives},
	{USB_PRO, usbProPixelFormatAlternatives}
};

inline pixel_format_by_bit_depth_subsampling generatePixelFormatMatrix(DeviceType deviceType,
                                                                       const std::vector<DWORD>& fourccs)
{
	if (deviceType == PRO)
	{
		return proPixelFormats;
	}

	pixel_format_alternatives alternatives = deviceType == USB_PRO
		                                         ? usbProPixelFormatAlternatives
		                                         : usbPlusPixelFormatAlternatives;
	pixel_format_by_bit_depth_subsampling proposed = deviceType == USB_PRO ? usbProPixelFormats : usbPlusPixelFormats;


	auto formatExists = [&, fourccs](DWORD target) { return std::ranges::find(fourccs, target) != fourccs.end(); };
	auto findReplacement = [&, deviceType](const pixel_format& format)
	{
		auto formatAlternatives = formatAlternativesByDeviceType.find(deviceType);
		if (formatAlternatives != formatAlternativesByDeviceType.end())
		{
			auto fallbacksByDeviceType = formatAlternatives->second;
			auto fallbacks = fallbacksByDeviceType.find(format);
			if (fallbacks != fallbacksByDeviceType.end())
			{
				for (auto& fallback : fallbacks->second)
				{
					if (formatExists(fallback.fourcc))
					{
						return fallback;
					}
				}
			}
		}
		return NA;
	};


	for (int i = 0; i < bitDepthCount; ++i)
	{
		for (int j = 0; j < subsamplingCount; j++)
		{
			auto& format = proposed[i][j];
			if (!formatExists(format.fourcc))
			{
				auto replacement = findReplacement(format);
				if (replacement != NA)
				{
					proposed[i][j] = replacement;
				}
			}
		}
	}
	return proposed;
}

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
}

// HDMI Audio Bitstream Codec Identification metadata

// IEC 61937-1 Chapter 6.1.7 Field Pa
constexpr auto IEC61937_SYNCWORD_1 = 0xF872;
// IEC 61937-1 Chapter 6.1.7 Field Pb
constexpr auto IEC61937_SYNCWORD_2 = 0x4E1F;

// IEC 61937-2 Table 2
enum IEC61937DataType : uint8_t
{
	IEC61937_NULL = 0x0, ///< NULL
	IEC61937_AC3 = 0x01, ///< AC-3 data
	IEC61937_PAUSE = 0x03, ///< Pause
	IEC61937_MPEG1_LAYER1 = 0x04, ///< MPEG-1 layer 1
	IEC61937_MPEG1_LAYER23 = 0x05, ///< MPEG-1 layer 2 or 3 data or MPEG-2 without extension
	IEC61937_MPEG2_EXT = 0x06, ///< MPEG-2 data with extension
	IEC61937_MPEG2_AAC = 0x07, ///< MPEG-2 AAC ADTS
	IEC61937_MPEG2_LAYER1_LSF = 0x08, ///< MPEG-2, layer-1 low sampling frequency
	IEC61937_MPEG2_LAYER2_LSF = 0x09, ///< MPEG-2, layer-2 low sampling frequency
	IEC61937_MPEG2_LAYER3_LSF = 0x0A, ///< MPEG-2, layer-3 low sampling frequency
	IEC61937_DTS1 = 0x0B, ///< DTS type I   (512 samples)
	IEC61937_DTS2 = 0x0C, ///< DTS type II  (1024 samples)
	IEC61937_DTS3 = 0x0D, ///< DTS type III (2048 samples)
	IEC61937_ATRAC = 0x0E, ///< ATRAC data
	IEC61937_ATRAC3 = 0x0F, ///< ATRAC3 data
	IEC61937_ATRACX = 0x10, ///< ATRAC3+ data
	IEC61937_DTSHD = 0x11, ///< DTS HD data
	IEC61937_WMAPRO = 0x12, ///< WMA 9 Professional data
	IEC61937_MPEG2_AAC_LSF_2048 = 0x13, ///< MPEG-2 AAC ADTS half-rate low sampling frequency
	IEC61937_MPEG2_AAC_LSF_4096 = 0x13 | 0x20, ///< MPEG-2 AAC ADTS quarter-rate low sampling frequency
	IEC61937_EAC3 = 0x15, ///< E-AC-3 data
	IEC61937_TRUEHD = 0x16, ///< TrueHD/MAT data
};

constexpr int maxBitDepthInBytes = sizeof(DWORD);
constexpr int maxFrameLengthInBytes = MWCAP_AUDIO_SAMPLES_PER_FRAME * MWCAP_AUDIO_MAX_NUM_CHANNELS * maxBitDepthInBytes;

EXTERN_C const GUID CLSID_MWCAPTURE_FILTER;

struct USB_CAPTURE_FORMATS
{
	bool usb{false};
	MWCAP_VIDEO_OUTPUT_FOURCC fourccs;
	MWCAP_VIDEO_OUTPUT_FRAME_INTERVAL frameIntervals;
	MWCAP_VIDEO_OUTPUT_FRAME_SIZE frameSizes;
};

struct VIDEO_SIGNAL
{
	MWCAP_INPUT_SPECIFIC_STATUS inputStatus;
	MWCAP_VIDEO_SIGNAL_STATUS signalStatus;
	MWCAP_VIDEO_BUFFER_INFO bufferInfo;
	MWCAP_VIDEO_FRAME_INFO frameInfo;
	MWCAP_VIDEO_CAPTURE_STATUS captureStatus;
	HDMI_HDR_INFOFRAME_PAYLOAD hdrInfo;
	HDMI_AVI_INFOFRAME_PAYLOAD aviInfo;
};

struct AUDIO_SIGNAL
{
	MWCAP_AUDIO_SIGNAL_STATUS signalStatus;
	MWCAP_AUDIO_CAPTURE_FRAME frameInfo;
	HDMI_AUDIO_INFOFRAME_PAYLOAD audioInfo;
};

struct DEVICE_INFO
{
	DeviceType deviceType;
	std::string serialNo{};
	WCHAR devicePath[128];
	HCHANNEL hChannel;
	double temperature{0.0};
	int64_t linkSpeed{0};
	// pcie only
	int64_t linkWidth{-1};
	int16_t maxPayloadSize{-1};
	int16_t maxReadRequestSize{-1};
	// usb pro only
	int16_t fanSpeed{-1};
};

struct captured_frame
{
	uint8_t* data;
	uint64_t length;
	uint64_t ts;
};

struct video_sample_buffer
{
	uint64_t index;
	uint8_t* data;
	int width;
	int height;

	int GetWidth() const { return width; }

	int GetHeight() const { return height; }

	uint64_t GetFrameIndex() const { return index; }

	void Start(void** buffer) const
	{
		*buffer = data;
	}

	void End() const
	{
		// nop
	}
};

#endif