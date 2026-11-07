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
#ifndef BGR10_RGB48_HEADER
#define BGR10_RGB48_HEADER

#include "VideoFrameWriter.h"
#include "conversion_kernels.h"

#ifndef NO_LOGGING
#include <quill/StopWatch.h>
#endif

#ifdef RECORD_RAW
#include <atlcomcli.h>
#include <filesystem>
#endif

template <typename VF>
class bgr10_rgb48 : public IVideoFrameWriter<VF>
{
public:
	bgr10_rgb48(const log_data& pLogData, uint32_t pX, uint32_t pY) : IVideoFrameWriter<VF>(pLogData, pX, pY, &RGB48)
	{
	}

	~bgr10_rgb48() override = default;

	HRESULT WriteTo(VF* srcFrame, IMediaSample* dstFrame) override
	{
		const auto width = srcFrame->GetWidth();
		const auto height = srcFrame->GetHeight();

		auto hr = this->DetectPadding(srcFrame->GetFrameIndex(), width, dstFrame);
		if (S_FALSE == hr)
		{
			return S_FALSE;
		}

		void* d;
		srcFrame->Start(&d);
		const uint8_t* sourceData = static_cast<const uint8_t*>(d);

		BYTE* outData;
		dstFrame->GetPointer(&outData);

		#ifdef RECORD_RAW
		if (++this->mFrameCounter % 60 == 0)
		{
			char filename[MAX_PATH];
			strcpy_s(filename, std::filesystem::temp_directory_path().string().c_str());
			CHAR rawFileName[128];
			sprintf_s(rawFileName, "\\bgr10-%d.raw", this->mFrameCounter);
			strcat_s(filename, rawFileName);
			FILE* file;
			if (fopen_s(&file, filename, "wb") != 0)
			{
				LOG_WARNING(this->mLogData.logger, "[{}] Failed to open {}", this->mLogData.prefix, filename);
			}
			else
			{
				LOG_TRACE_L3(this->mLogData.logger, "[{}] Wrote input frame {} to raw file", this->mLogData.prefix, this->mFrameCounter);
				fwrite(sourceData, srcFrame->GetLength(), 1, file);
				fclose(file);
			}
		}
		#endif

		#ifndef NO_LOGGING
		const quill::StopWatchTsc swt;
		#endif

		conversion::bgr10_to_rgb48(sourceData, reinterpret_cast<uint16_t*>(outData), width, height, this->mPixelsToPad);

		#ifndef NO_LOGGING
		auto execTime = swt.elapsed_as<std::chrono::microseconds>().count() / 1000.0;
		LOG_TRACE_L3(this->mLogData.logger, "[{}] Converted frame to RGB48 in {:.3f} ms", this->mLogData.prefix, execTime);
		#endif

		srcFrame->End();

		return S_OK;

	}

private:
	#ifdef RECORD_RAW
	uint32_t mFrameCounter;
	#endif
};
#endif