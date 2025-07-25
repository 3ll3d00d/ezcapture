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
#ifndef DOMAIN_HEADER
#define DOMAIN_HEADER

#define NOMINMAX // quill does not compile without this

#include <string>
#include <array>
#include <map>
#include <optional>
#include <cmath>     // std::lround
#include <chrono>
#include "metric.h"

#define BACKOFF Sleep(20)
#define SHORT_BACKOFF Sleep(1)

#define TO_4CC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |   \
                ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))

inline constexpr auto not_present = 1024;
inline constexpr LONGLONG dshowTicksPerSecond = 10LL * 1000 * 1000; // 100ns
inline constexpr auto unity = 1.0;

#define ROOT_REG_KEY L"Software\\3ll3d00d\\"

enum device_type : uint8_t
{
	MW_USB_PLUS,
	MW_USB_PRO,
	MW_PRO,
	BM_DECKLINK
};

inline const char* devicetype_to_name(device_type e)
{
	switch (e)
	{
	case MW_USB_PLUS: return "USB_PLUS";
	case MW_USB_PRO: return "USB_PRO";
	case MW_PRO: return "PRO";
	case BM_DECKLINK: return "BM";
	default: return "unknown";
	}
}

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
		RGB48,
		YUV2,
		YUY2,
		UYVY,
		YV16,
		V210,
		Y210,
		AY10,
		ARGB,
		BGRA,
		RGBA,
		R210,
		R12B,
		R12L,
		R10B,
		R10L,
		FAIL
	};

	pixel_format(format pf, char a, char b, char c, char d, uint8_t pBitDepth, uint8_t pBitsPerPixel, bool pRgb,
	             pixel_encoding pPixelEncoding, DWORD pByteAlignment = 2)
	{
		format = pf;
		fourcc = TO_4CC(a, b, c, d);
		bitDepth = pBitDepth;
		bitsPerPixel = pBitsPerPixel;
		name = std::string{a, b, c, d};
		rgb = pRgb;
		byteAlignment = pByteAlignment;
		subsampling = pPixelEncoding;
	}

	format format;
	DWORD fourcc;
	uint8_t bitDepth;
	uint8_t bitsPerPixel;
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
		case YUY2:
		case UYVY:
		case P010:
		case P210:
		case Y210:
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
		case RGB48:
			cbLine = cx * 6;
			break;
		case R12B:
		case R12L:
		default: // NOLINT(clang-diagnostic-covered-switch-default)
			cbLine = cx * bitsPerPixel / 8;
		}

		*rowBytes = (cbLine + byteAlignment - 1) & ~(byteAlignment - 1);
		*imageBytes = *rowBytes * cy;

		if (format == NV12 || format == P010)
		{
			*imageBytes = *imageBytes * 3 / 2;
		}
		else if (format == YV16 || format == NV16 || format == P210 || format == Y210)
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
const inline pixel_format NV12{pixel_format::NV12, 'N', 'V', '1', '2', 8, 12, false, YUV_420};
const inline pixel_format NV16{pixel_format::NV16, 'N', 'V', '1', '6', 8, 16, false, YUV_422};
const inline pixel_format P010{pixel_format::P010, 'P', '0', '1', '0', 10, 24, false, YUV_420};
const inline pixel_format P210{pixel_format::P210, 'P', '2', '1', '0', 10, 32, false, YUV_422};
const inline pixel_format AYUV{pixel_format::AYUV, 'A', 'Y', 'U', 'V', 8, 32, false, YUV_444};
const inline pixel_format BGR24{pixel_format::BGR24, 'B', 'G', 'R', ' ', 8, 24, true, RGB_444};
const inline pixel_format BGR10{pixel_format::BGR10, 'B', 'G', '1', '0', 10, 32, true, RGB_444};
// magewell usb
const inline pixel_format YUY2{pixel_format::YUY2, 'Y', 'U', 'Y', '2', 8, 16, false, YUV_422};
const inline pixel_format UYVY{pixel_format::UYVY, 'U', 'Y', 'V', 'Y', 8, 16, false, YUV_422};
const inline pixel_format Y210{pixel_format::Y210, 'Y', '2', '1', '0', 10, 16, false, YUV_422};
// blackmagic, generally require conversion due to lack of native renderer support
const inline pixel_format YUV2{pixel_format::YUV2, '2', 'V', 'U', 'Y', 8, 16, false, YUV_422};
const inline pixel_format V210{pixel_format::V210, 'v', '2', '1', '0', 10, 16, false, YUV_422, 128};
const inline pixel_format AY10{pixel_format::AY10, 'A', 'y', '1', '0', 10, 32, false, YUV_422, 256};
const inline pixel_format ARGB{pixel_format::ARGB, 'A', 'R', 'G', 'B', 8, 32, true, RGB_444};
const inline pixel_format BGRA{pixel_format::BGRA, 'B', 'G', 'R', 'A', 8, 32, true, RGB_444};
const inline pixel_format RGBA{pixel_format::RGBA, 'R', 'G', 'B', 'A', 8, 32, true, RGB_444};
const inline pixel_format R210{pixel_format::R210, 'r', '2', '1', '0', 10, 32, false, RGB_444, 256};
const inline pixel_format R12B{pixel_format::R12B, 'R', '1', '2', 'B', 12, 36, false, RGB_444};
const inline pixel_format R12L{pixel_format::R12L, 'R', '1', '2', 'L', 12, 36, false, RGB_444};
const inline pixel_format R10L{pixel_format::R10L, 'R', '1', '0', 'l', 10, 32, false, RGB_444, 256};
const inline pixel_format R10B{pixel_format::R10B, 'R', '1', '0', 'b', 10, 32, false, RGB_444, 256};
// jrvr
const inline pixel_format YV16{pixel_format::YV16, 'Y', 'V', '1', '6', 8, 16, false, YUV_422};
const inline pixel_format RGB48{pixel_format::RGB48, 'R', 'G', 'B', '0', 16, 48, false, RGB_444};
// indicates not supported
const inline pixel_format NA{pixel_format::FAIL, 'x', 'x', 'x', 'x', 0, 0, false, RGB_444};

const std::array all_pixel_formats = {
	NV12,
	NV16,
	P010,
	P210,
	AYUV,
	BGR24,
	BGR10,
	YUV2,
	YUY2,
	UYVY,
	YV16,
	V210,
	Y210,
	AY10,
	ARGB,
	BGRA,
	RGBA,
	R210,
	R12B,
	R12L,
	R10L,
	R10B,
	RGB48
};

inline std::optional<pixel_format> findByFourCC(uint32_t fourcc)
{
	const auto match = std::ranges::find_if(all_pixel_formats,
	                                        [&fourcc](const pixel_format& arg) { return arg.fourcc == fourcc; });
	if (match != all_pixel_formats.end())
	{
		return {*match};
	}
	return std::nullopt;
}

enum protocol
{
	PCIE,
	USB
};

struct device_status
{
	protocol protocol{PCIE};
	std::string deviceDesc{};
	double temperature{0.0};
	int16_t fanSpeed{-1};
	int64_t linkSpeed{0};
	int64_t linkWidth{-1};
	int16_t maxPayloadSize{-1};
	int16_t maxReadRequestSize{-1};
};

struct hdr_meta
{
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

	boolean exists() const
	{
		return
			r_primary_x != 0.0 &&
			r_primary_y != 0.0 &&
			g_primary_x != 0.0 &&
			g_primary_y != 0.0 &&
			b_primary_x != 0.0 &&
			b_primary_y != 0.0 &&
			whitepoint_x != 0.0 &&
			whitepoint_y != 0.0 &&
			minDML != 0.0 &&
			maxDML != 0.0 &&
			maxCLL != 0 &&
			maxFALL != 0;
	}
};

inline boolean hdrMetaExists(const hdr_meta* hdrOut)
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

struct audio_input_status
{
	bool audioInStatus;
	bool audioInIsPcm;
	unsigned char audioInBitDepth;
	unsigned long audioInFs;
	unsigned short audioInChannelPairs;
	unsigned char audioInChannelMap;
	unsigned char audioInLfeLevel;
};

struct audio_output_status
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

struct video_input_status
{
	int inX{-1};
	int inY{-1};
	int inAspectX{-1};
	int inAspectY{-1};
	std::string signalStatus;
	std::string inColourFormat;
	std::string inQuantisation;
	std::string inSaturation;
	double inFps{0.0};
	uint64_t inFrameDuration{0};
	int inBitDepth{0};
	std::string inPixelLayout;
	bool validSignal{false};
};

struct video_output_status
{
	int outX{-1};
	int outY{-1};
	int outAspectX{-1};
	int outAspectY{-1};
	std::string outColourFormat;
	std::string outQuantisation;
	std::string outSaturation;
	double outFps{0.0};
	int outBitDepth{0};
	std::string outSubsampling;
	std::string outPixelStructure;
	std::string outTransferFunction;
};

struct display_status
{
	int freq{0};
	std::wstring status;
};

struct latency_stats
{
	std::string name;
	uint64_t min{0};
	double mean{0.0};
	uint64_t max{0};
};

struct hdr_status
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

struct video_format
{
	colour_format colourFormat{REC709};
	pixel_format pixelFormat{NV12};
	int cx{3840};
	int cy{2160};
	double fps{50.0};
	LONGLONG frameInterval{200000};
	hdr_meta hdrMeta{};
	int aspectX{16};
	int aspectY{9};
	quantisation_range quantisation{QUANTISATION_UNKNOWN};
	saturation_range saturation{SATURATION_UNKNOWN};
	// derived from the above attributes
	std::string colourFormatName{"REC709"};
	DWORD lineLength{0};
	DWORD imageSize{0};
	bool bottomUpDib{true};

	void CalculateDimensions()
	{
		pixelFormat.GetImageDimensions(cx, cy, &lineLength, &imageSize);
	}

	long CalcRefreshRate() const
	{
		return std::lround(fps - 0.49); // 23.976 will become 23, 24 will become 24 etc;
	}
};

enum codec
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

struct audio_format
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
	WORD channelMask{0};
	std::string channelLayout;
	int lfeChannelIndex{not_present};
	double lfeLevelAdjustment{1.0};
	codec codec{PCM};
	// encoded content only
	uint16_t dataBurstSize{0};
};

enum frame_writer_strategy :uint8_t
{
	UNKNOWN,
	ANY_RGB,
	YUV2_YV16,
	YUY2_YV16,
	UYVY_YV16,
	V210_P210,
	Y210_P210,
	R210_BGR48,
	BGR10_BGR48,
	STRAIGHT_THROUGH
};

inline const char* to_string(frame_writer_strategy e)
{
	switch (e)
	{
	case ANY_RGB: return "ANY_RGB";
	case YUV2_YV16: return "YUV2_YV16";
	case YUY2_YV16: return "YUY2_YV16";
	case UYVY_YV16: return "UYVY_YV16";
	case V210_P210: return "V210_P210";
	case Y210_P210: return "Y210_P210";
	case R210_BGR48: return "R210_BGR48";
	case BGR10_BGR48: return "BGR10_BGR48";
	case STRAIGHT_THROUGH: return "STRAIGHT_THROUGH";
	default: return "unknown";
	}
}

// TODO support a list of fall back options
typedef std::map<pixel_format, std::pair<pixel_format, frame_writer_strategy>> pixel_format_fallbacks;

struct frame_metrics
{
	int64_t startTs{0};
	int64_t endTs{0};
	double actualFrameRate;
	metric m1;
	std::string name1{"Capture"};
	metric m2;
	std::string name2{"Conversion"};
	metric m3;
	std::string name3{};

	void start(int64_t ts)
	{
		startTs = ts;
	}

	void end(int64_t ts)
	{
		endTs = ts;
		actualFrameRate = 10000000.0 / (static_cast<double>(ts - startTs) / (m1.capacity() - 1));
	}

	void refreshRate(double rate)
	{
		// aim for metrics to update approx once every 1500ms
		auto newSize = std::lrint(rate * 3 / 2);
		if (std::in_range<uint16_t>(newSize))
		{
			const uint16_t sz = static_cast<uint16_t>(newSize);
			m1.resize(sz);
			m2.resize(sz);
			m3.resize(sz);
		}
		else
		{
			const auto sz = std::numeric_limits<uint16_t>::max();
			m1.resize(sz);
			m2.resize(sz);
			m3.resize(sz);
		}
	}
};

enum ts_type : uint8_t
{
	WAITING, // user code starts waiting for a frame
	WAIT_COMPLETE, // user code signalled that frame is available
	BUFFER_ALLOCATED, // buffer provided by the allocator
	BUFFERING, // frame starts to arrive in card memory 
	BUFFERED, // frame completely buffered in card memory
	READING, // user code starts to read the frame
	READ, // user code has access to the entire frame in memory
	CONVERTED, // user code has processed frame into output format
	COMPLETE // system time for comparison
};

static int64_t high_res_now()
{
	using std::chrono::high_resolution_clock;
	using std::chrono::microseconds;
	using std::chrono::duration_cast;
	return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count() * 10;
}

class frame_ts
{
public:
	frame_ts(device_type pType, bool pVideo):
		mDeviceType(pType),
		mVideo(pVideo)
	{
	}

	void initialise(int64_t pInitTime, int64_t pEndTime)
	{
		mReferenceStartTime = pInitTime;
		mReferenceEndTime = pEndTime;
		reset();
	}

	void snap(int64_t val, ts_type type)
	{
		mTs[type] = offset(val);
	}

	void end()
	{
		mTs[COMPLETE] = high_res_now() - mReferenceEndTime;
	}

	int64_t get(ts_type type) const
	{
		return mTs[type];
	}

	void reset()
	{
		mTs.fill(0);
	}

	boolean recordTo(frame_metrics& metrics) const
	{
		/*
		 * Magewell PRO Video: WAITING, WAIT_COMPLETE, BUFFER_ALLOCATED, BUFFERING, BUFFERED, READING, READ, CONVERTED
		 * Magewell PRO Audio: WAITING, WAIT_COMPLETE, BUFFERING, READING, READ, BUFFER_ALLOCATED, CONVERTED
		 * Magewell USB: WAIT_COMPLETE, BUFFERING, READ, CONVERTED
		 * Decklink: WAIT_COMPLETE, BUFFER_ALLOCATED, READ, CONVERTED
		 */
		bool propagate;
		if (mDeviceType == MW_PRO)
		{
			if (mVideo)
			{
				propagate = metrics.m1.sample(mTs[READ] - mTs[BUFFERING]);
				metrics.m2.sample(mTs[CONVERTED] - mTs[READ]);
				metrics.m3.sample(mTs[READ] - mTs[BUFFERED]);
				metrics.name3 = "Host";
			}
			else
			{
				propagate = metrics.m1.sample(mTs[BUFFER_ALLOCATED] - mTs[BUFFERING]);
				metrics.m2.sample(mTs[CONVERTED] - mTs[BUFFER_ALLOCATED]);
			}
		}
		else if (mDeviceType == MW_USB_PLUS || mDeviceType == MW_USB_PRO)
		{
			propagate = metrics.m1.sample(mTs[READ] - mTs[BUFFERING]);
			metrics.m2.sample(mTs[CONVERTED] - mTs[READ]);
		}
		else
		{
			propagate = metrics.m1.sample(mTs[READ] - mTs[WAIT_COMPLETE]);
			metrics.m2.sample(mTs[CONVERTED] - mTs[READ]);
			metrics.m3.sample(mTs[BUFFER_ALLOCATED] - mTs[WAIT_COMPLETE]);
			metrics.name3 = "Handoff";
		}
		if (metrics.m1.size() == 1)
		{
			metrics.start(mTs[COMPLETE]);
		}
		else if (propagate)
		{
			metrics.end(mTs[COMPLETE]);
		}
		return propagate;
	}

private:
	device_type mDeviceType;
	bool mVideo;
	int64_t mReferenceStartTime{0}; // time used to offset all measurement timestamps
	int64_t mReferenceEndTime{0}; // time used to offset the end timestamp
	std::array<int64_t, 9> mTs{};

	int64_t offset(int64_t val) const
	{
		return val - mReferenceStartTime;
	}
};

#endif
