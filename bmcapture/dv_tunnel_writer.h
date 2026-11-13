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
#ifndef BM_DV_TUNNEL_WRITER_HEADER
#define BM_DV_TUNNEL_WRITER_HEADER

#include "VideoFrameWriter.h"
#include "video_frame.h"
#include "dv_tunnel.h"

#ifndef NO_LOGGING
#include "quill/StopWatch.h"
#endif

/**
 * Writes a captured Dolby Vision tunnel frame as 12-bit IPT-PQ 4:2:2 in P210, or black when the tunnel is locked
 * but there is no usable metadata. The decode decision was made for this frame on the capture callback thread.
 */
class dv_tunnel_writer : public IVideoFrameWriter<video_frame>
{
public:
	dv_tunnel_writer(const log_data& pLogData, int pX, int pY) : IVideoFrameWriter(pLogData, pX, pY, &P210)
	{
	}

	~dv_tunnel_writer() override = default;

	HRESULT WriteTo(video_frame* srcFrame, IMediaSample* dstFrame) override
	{
		const auto format = srcFrame->GetVideoFormat();
		if (!format.dvTunnel)
		{
			#ifndef NO_LOGGING
			LOG_WARNING(mLogData.logger, "[{}] Discarding frame {}, not a DV tunnel frame", mLogData.prefix,
			            srcFrame->GetFrameIndex());
			#endif
			return S_FALSE;
		}
		const auto width = static_cast<uint32_t>(srcFrame->GetWidth());
		const auto height = static_cast<uint32_t>(srcFrame->GetHeight());
		if (S_FALSE == DetectPadding(srcFrame->GetFrameIndex(), static_cast<int>(width), dstFrame))
		{
			return S_FALSE;
		}
		const size_t dstStride = (static_cast<size_t>(width) + mPixelsToPad) * 2;
		const size_t planeSize = dstStride * height;

		BYTE* out;
		if (FAILED(dstFrame->GetPointer(&out)))
		{
			return S_FALSE;
		}

		#ifndef NO_LOGGING
		const quill::StopWatchTsc swt;
		#endif

		const auto& dv = srcFrame->GetDv();
		if (dv.render)
		{
			void* d;
			srcFrame->Start(&d);
			dv_tunnel::unpack(dv.layout, static_cast<const uint8_t*>(d), srcFrame->GetRowBytes(), out,
			                  out + planeSize, dstStride, width, height);
			srcFrame->End();
		}
		else
		{
			// never show the packed pixels
			dv_tunnel::fill_black(out, out + planeSize, dstStride, width, height);
		}

		#ifndef NO_LOGGING
		auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
		LOG_TRACE_L3(mLogData.logger, "[{}] Decoded DV tunnel frame ({}) to P210 in {:.3f} ms", mLogData.prefix,
		             dv.render ? dv_tunnel::to_string(dv.layout) : "black", execTime);
		#endif

		return S_OK;
	}
};

#endif
