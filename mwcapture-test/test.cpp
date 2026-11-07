#define NOMINMAX // quill does not compile without this

#include "gtest/gtest.h"
#include "LibMWCapture/MWCapture.h"
#include "../mwcapture/mw_domain.h"

TEST(HDR, CanParseHDRInfoFrame)
{
	hdr_meta o{};
	HDMI_HDR_INFOFRAME_PAYLOAD i{
		//02 00 34 21 AA 9B 96 19 FC 08 48 8A 08 39 13 3D 42 40 9F 0F 32 00 A0 0F E8 03 
		.byEOTF = 0x02,
		.byMetadataDescriptorID = 0x00,

		.display_primaries_lsb_x0 = 0x34,
		.display_primaries_msb_x0 = 0x21,
		.display_primaries_lsb_y0 = 0xAA,
		.display_primaries_msb_y0 = 0x9B,

		.display_primaries_lsb_x1 = 0x96,
		.display_primaries_msb_x1 = 0x19,
		.display_primaries_lsb_y1 = 0xFC,
		.display_primaries_msb_y1 = 0x08,

		.display_primaries_lsb_x2 = 0x48,
		.display_primaries_msb_x2 = 0x8A,
		.display_primaries_lsb_y2 = 0x08,
		.display_primaries_msb_y2 = 0x39,

		.white_point_lsb_x = 0x13,
		.white_point_msb_x = 0x3D,
		.white_point_lsb_y = 0x42,
		.white_point_msb_y = 0x40,

		.max_display_mastering_lsb_luminance = 0x9F,
		.max_display_mastering_msb_luminance = 0x0F,
		.min_display_mastering_lsb_luminance = 0x32,
		.min_display_mastering_msb_luminance = 0x00,

		.maximum_content_light_level_lsb = 0xA0,
		.maximum_content_light_level_msb = 0x0F,

		.maximum_frame_average_light_level_lsb = 0xE8,
		.maximum_frame_average_light_level_msb = 0x03
	};

	LoadHdrMeta(&o, &i);

	// hdr_meta holds physical values: the raw CTA-861.3 fields are scaled by
	// 0.00002 (chromaticity), 0.0001 cd/m2 (min luminance), 1 cd/m2 (max luminance).
	// This payload is BT.2020 primaries with a D65 white point.
	constexpr double tol = 1e-9;
	EXPECT_TRUE(o.exists());
	EXPECT_NEAR(o.r_primary_x, 0.708, tol);   // raw 35400
	EXPECT_NEAR(o.r_primary_y, 0.292, tol);   // raw 14600
	EXPECT_NEAR(o.g_primary_x, 0.170, tol);   // raw 8500
	EXPECT_NEAR(o.g_primary_y, 0.797, tol);   // raw 39850
	EXPECT_NEAR(o.b_primary_x, 0.131, tol);   // raw 6550
	EXPECT_NEAR(o.b_primary_y, 0.046, tol);   // raw 2300
	EXPECT_NEAR(o.whitepoint_x, 0.3127, tol); // raw 15635
	EXPECT_NEAR(o.whitepoint_y, 0.3290, tol); // raw 16450
	EXPECT_EQ(o.maxCLL, 4000);
	EXPECT_EQ(o.maxFALL, 1000);
	EXPECT_NEAR(o.minDML, 0.005, tol);        // raw 50
	EXPECT_NEAR(o.maxDML, 3999.0, tol);       // raw 3999
	EXPECT_EQ(o.transferFunction, 15);        // EOTF 2 = ST2084 (PQ)
}

TEST(DIMS, CanCalcImageDims)
{
	DWORD img, line;

	for (int i = 0; i < bitDepthCount; ++i)
	{
		for (int j = 0; j < subsamplingCount; ++j)
		{
			proPixelFormats[i][j].GetImageDimensions(3840, 2160, &line, &img);

			EXPECT_EQ(line, FOURCC_CalcMinStride(proPixelFormats[i][j].fourcc, 3840, 2));
			EXPECT_EQ(img, FOURCC_CalcImageSize(proPixelFormats[i][j].fourcc, 3840, 2160, line));

			proPixelFormats[i][j].GetImageDimensions(1920, 1080, &line, &img);

			EXPECT_EQ(line, FOURCC_CalcMinStride(proPixelFormats[i][j].fourcc, 1920, 2));
			EXPECT_EQ(img, FOURCC_CalcImageSize(proPixelFormats[i][j].fourcc, 1920, 1080, line));

			proPixelFormats[i][j].GetImageDimensions(720, 480, &line, &img);

			EXPECT_EQ(line, FOURCC_CalcMinStride(proPixelFormats[i][j].fourcc, 720, 2));
			EXPECT_EQ(img, FOURCC_CalcImageSize(proPixelFormats[i][j].fourcc, 720, 480, line));
		}
	}
}

TEST(PIX, NoFallbacksRequired)
{
	auto pro = generatePixelFormatMatrix(MW_USB_PRO, {BGR24.fourcc, Y210.fourcc, P010.fourcc});
	auto plus = generatePixelFormatMatrix(MW_USB_PLUS, {BGR24.fourcc, UYVY.fourcc, NV12.fourcc});
	for (int i = 0; i < bitDepthCount; ++i)
	{
		for (int j = 0; j < subsamplingCount; ++j)
		{
			EXPECT_EQ(pro[i][j], usbProPixelFormats[i][j]);
			EXPECT_EQ(plus[i][j], usbPlusPixelFormats[i][j]);
		}
	}
}

TEST(PIX, FirstFallback)
{
	auto pro = generatePixelFormatMatrix(MW_USB_PRO, {BGR24.fourcc, UYVY.fourcc, P010.fourcc});
	for (int i = 0; i < bitDepthCount; ++i)
	{
		for (int j = 0; j < subsamplingCount; ++j)
		{
			switch (j)
			{
			case 0:
				EXPECT_EQ(pro[i][j], BGR24);
				break;
			case 1:
				EXPECT_EQ(pro[i][j], P010);
				break;
			case 2:
				EXPECT_EQ(pro[i][j], P010);
				break;
			case 3:
				EXPECT_EQ(pro[i][j], P010);
				break;
			}
		}
	}
}

TEST(PIX, ManyFallbacks)
{
	auto pro = generatePixelFormatMatrix(MW_USB_PRO, {NV12.fourcc});
	for (int i = 0; i < bitDepthCount; ++i)
	{
		for (int j = 0; j < subsamplingCount; ++j)
		{
			EXPECT_EQ(pro[i][j], NV12);
		}
	}
}

TEST(Bandwidth, MatchesMagewellRatings)
{
	EXPECT_DOUBLE_EQ(pcieCaptureBandwidth(2, 4), 1600e6);
	EXPECT_DOUBLE_EQ(pcieCaptureBandwidth(1, 4), 800e6);
	EXPECT_DOUBLE_EQ(pcieCaptureBandwidth(2, 2), 800e6);
	EXPECT_DOUBLE_EQ(pcieCaptureBandwidth(0, 4), 0.0);
	EXPECT_DOUBLE_EQ(pcieCaptureBandwidth(2, -1), 0.0);
}

TEST(Bandwidth, KeepsFormatsThatFit)
{
	const auto gen2x4 = pcieCaptureBandwidth(2, 4);
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 23.976, gen2x4), P210);
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 30.0, gen2x4), P210);
	EXPECT_EQ(fitToBandwidth(P010, 3840, 2160, 59.94, gen2x4), P010);
	EXPECT_EQ(fitToBandwidth(BGR24, 3840, 2160, 60.0, gen2x4), BGR24);
	EXPECT_EQ(fitToBandwidth(P210, 1920, 1080, 60.0, gen2x4), P210);
}

TEST(Bandwidth, GivesUpChromaBeforeBitDepth)
{
	const auto gen2x4 = pcieCaptureBandwidth(2, 4);
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 59.94, gen2x4), P010);
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 50.0, gen2x4), P010);
	EXPECT_EQ(fitToBandwidth(BGR10, 3840, 2160, 60.0, gen2x4), P010);
	EXPECT_EQ(fitToBandwidth(AYUV, 3840, 2160, 60.0, gen2x4), BGR24);
}

TEST(Bandwidth, SlowLinks)
{
	const auto gen2x2 = pcieCaptureBandwidth(2, 2);
	EXPECT_EQ(fitToBandwidth(BGR24, 3840, 2160, 60.0, gen2x2), NV12);
	EXPECT_EQ(fitToBandwidth(AYUV, 3840, 2160, 30.0, gen2x2), BGR24);
	// 10-bit never drops to 8-bit, the closest fit is used even though it does not fit
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 60.0, gen2x2), P010);
	EXPECT_EQ(fitToBandwidth(P010, 3840, 2160, 60.0, gen2x2), P010);
}

TEST(Bandwidth, UsbEstimates)
{
	EXPECT_DOUBLE_EQ(usbCaptureBandwidth(3), 400e6);
	EXPECT_NEAR(usbCaptureBandwidth(4), 970e6, 1e6);
	EXPECT_DOUBLE_EQ(usbCaptureBandwidth(2), 40e6);
	EXPECT_DOUBLE_EQ(usbCaptureBandwidth(1), 0.0);
}

TEST(Bandwidth, UsesTheRealFrameSize)
{
	// 4:2:0 and packed formats are not a whole number of bytes per pixel
	EXPECT_DOUBLE_EQ(captureBytesPerSecond(NV12, 3840, 2160, 60.0), 3840.0 * 2160 * 1.5 * 60);
	EXPECT_DOUBLE_EQ(captureBytesPerSecond(P210, 3840, 2160, 60.0), 3840.0 * 2160 * 4 * 60);
	EXPECT_DOUBLE_EQ(captureBytesPerSecond(V210, 3840, 2160, 60.0), 10240.0 * 2160 * 60);
	EXPECT_DOUBLE_EQ(captureBytesPerSecond(R12B, 3840, 2160, 60.0), 3840.0 * 4.5 * 2160 * 60);
}

TEST(Bandwidth, UnknownLinkOrRateChangesNothing)
{
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 60.0, 0.0), P210);
	EXPECT_EQ(fitToBandwidth(P210, 3840, 2160, 0.0, 1600e6), P210);
}

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
