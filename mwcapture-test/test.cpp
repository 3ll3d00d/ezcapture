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

	EXPECT_TRUE(o.exists());
	EXPECT_EQ(o.r_primary_x, 35400);
	EXPECT_EQ(o.r_primary_y, 14600);
	EXPECT_EQ(o.g_primary_x, 8500);
	EXPECT_EQ(o.g_primary_y, 39850);
	EXPECT_EQ(o.b_primary_x, 6550);
	EXPECT_EQ(o.b_primary_y, 2300);
	EXPECT_EQ(o.maxCLL, 4000);
	EXPECT_EQ(o.maxFALL, 1000);
	EXPECT_EQ(o.minDML, 50);
	EXPECT_EQ(o.maxDML, 3999);
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

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
