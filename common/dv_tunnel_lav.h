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
#ifndef DV_TUNNEL_LAV_HEADER
#define DV_TUNNEL_LAV_HEADER

// requires the windows/COM headers to be included first (as for lavfilters_side_data.h)
#include "dv_tunnel.h"
#include "lavfilters_side_data.h"

#include <cstddef>
#include <cstring>

namespace dv_tunnel
{
	constexpr uint8_t lavCoefLog2Denom = 23;

	/**
	 * Describes tunnel metadata as if LAV had decoded a single layer, 12-bit stream. The HDMI tunnel
	 * carries pixels that the source has already reshaped so the mapping is the identity and only the
	 * colour metadata (IPT-PQ matrices) and the extension blocks are meaningful. This mirrors what
	 * videoprocessor hands libplacebo directly (a pl_dovi_metadata with no reshaping).
	 */
	inline void to_lav(const metadata& md, MediaSideDataDOVIMetadata& out)
	{
		std::memset(&out, 0, sizeof(out));

		auto& header = out.Header;
		header.rpu_type = 2;
		header.rpu_format = 18;
		header.vdr_rpu_profile = 1;
		header.vdr_rpu_level = 0;
		header.coef_data_type = 0;
		header.coef_log2_denom = lavCoefLog2Denom;
		header.vdr_rpu_normalized_idc = 1;
		header.bl_video_full_range_flag = md.fullRange() ? 1 : 0;
		header.bl_bit_depth = 12;
		header.el_bit_depth = 12;
		header.vdr_bit_depth = 12;
		header.disable_residual_flag = 1;

		auto& mapping = out.Mapping;
		for (auto& curve : mapping.curves)
		{
			curve.num_pivots = 2;
			curve.pivots[0] = 0;
			curve.pivots[1] = (1 << 12) - 1;
			curve.mapping_idc[0] = 0; // polynomial
			curve.poly_order[0] = 1;
			curve.poly_coef[0][0] = 0;
			curve.poly_coef[0][1] = 1LL << lavCoefLog2Denom;
			curve.poly_coef[0][2] = 0;
		}
		mapping.nlq_method_idc = 0xFF; // none
		mapping.num_x_partitions = 1;
		mapping.num_y_partitions = 1;

		auto& colour = out.ColorMetadata;
		colour.dm_metadata_id = 0;
		colour.scene_refresh_flag = md.sceneRefreshFlag;
		for (size_t i = 0; i < 9; ++i)
		{
			colour.ycc_to_rgb_matrix[i] = md.yccToRgb[i] / 8192.0;
			colour.rgb_to_lms_matrix[i] = md.rgbToLms[i] / 16384.0;
		}
		for (size_t i = 0; i < 3; ++i)
		{
			colour.ycc_to_rgb_offset[i] = md.yccToRgbOffset[i] / static_cast<double>(1U << 28);
		}
		colour.signal_eotf = md.signalEotf;
		colour.signal_eotf_param0 = md.signalEotfParam0;
		colour.signal_eotf_param1 = md.signalEotfParam1;
		colour.signal_eotf_param2 = md.signalEotfParam2;
		colour.signal_bit_depth = md.signalBitDepth;
		colour.signal_color_space = md.signalColorSpace;
		colour.signal_chroma_format = md.signalChromaFormat;
		colour.signal_full_range_flag = md.signalFullRangeFlag;
		colour.source_min_pq = md.sourceMinPq;
		colour.source_max_pq = md.sourceMaxPq;
		colour.source_diagonal = md.sourceDiagonal;

		// only L1 is mapped for now, the byte layout of the other levels in the tunnel is unverified
		if (md.hasL1)
		{
			out.Extensions[0].level = 1;
			out.Extensions[0].Level1.min_pq = md.l1MinPq;
			out.Extensions[0].Level1.max_pq = md.l1MaxPq;
			out.Extensions[0].Level1.avg_pq = md.l1AvgPq;
		}
	}

	/**
	 * Attaches the metadata to the sample, if it supports side data, as LAV does: V2 plus the legacy layout
	 * without extensions.
	 * @param scratch reused for each frame as the struct is large.
	 */
	inline void append_side_data(IMediaSample* pms, const metadata& md, MediaSideDataDOVIMetadata& scratch)
	{
		IMediaSideData* sideData = nullptr;
		if (FAILED(pms->QueryInterface(&sideData)))
		{
			return;
		}
		to_lav(md, scratch);
		sideData->SetSideData(IID_MediaSideDataDOVIMetadataV2, reinterpret_cast<const BYTE*>(&scratch),
		                      sizeof(MediaSideDataDOVIMetadata));
		sideData->SetSideData(IID_MediaSideDataDOVIMetadata, reinterpret_cast<const BYTE*>(&scratch),
		                      offsetof(MediaSideDataDOVIMetadata, Extensions));
		sideData->Release();
	}
}

#endif
