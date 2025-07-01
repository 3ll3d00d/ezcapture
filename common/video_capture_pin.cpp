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

#include "video_capture_pin.h"

#include <DXVA.h>

void video_capture_pin::VideoFormatToMediaType(CMediaType* pmt, video_format* videoFormat) const
{
	auto pvi = reinterpret_cast<VIDEOINFOHEADER2*>(pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));
	ZeroMemory(pvi, sizeof(VIDEOINFOHEADER2));

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VIDEOINFO2);
	pmt->SetTemporalCompression(FALSE);
	pmt->SetSampleSize(videoFormat->imageSize);

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle
	pvi->dwBitRate = static_cast<DWORD>(videoFormat->imageSize * 8 * videoFormat->fps);
	pvi->dwBitErrorRate = 0;
	pvi->AvgTimePerFrame = static_cast<DWORD>(static_cast<double>(dshowTicksPerSecond) / videoFormat->fps);
	pvi->dwInterlaceFlags = 0;
	pvi->dwPictAspectRatioX = videoFormat->aspectX;
	pvi->dwPictAspectRatioY = videoFormat->aspectY;

	// dwControlFlags is a 32bit int. With AMCONTROL_COLORINFO_PRESENT the upper 24 bits are used by DXVA_ExtendedFormat.
	// That struct is 32 bits so it's lower member (SampleFormat) is actually overbooked with the value of dwConotrolFlags
	// so can't be used. LAV has defined some out-of-spec but compatible with madVR values for the more modern formats,
	// which we use as well see
	// https://github.com/Nevcairiel/LAVFilters/blob/ddef56ae155d436f4301346408f4fdba755197d6/decoder/LAVVideo/Media.cpp

	auto colorimetry = reinterpret_cast<DXVA_ExtendedFormat*>(&(pvi->dwControlFlags));
	// 1 = REC.709, 4 = BT.2020
	colorimetry->VideoTransferMatrix = videoFormat->colourFormat == BT2020
		? static_cast<DXVA_VideoTransferMatrix>(4)
		: DXVA_VideoTransferMatrix_BT709;
	// 1 = REC.709, 9 = BT.2020
	colorimetry->VideoPrimaries = videoFormat->colourFormat == BT2020
		? static_cast<DXVA_VideoPrimaries>(9)
		: DXVA_VideoPrimaries_BT709;
	// 4 = REC.709, 15 = SMPTE ST 2084 (PQ), 16 = HLG (JRVR only)
	colorimetry->VideoTransferFunction = static_cast<DXVA_VideoTransferFunction>(videoFormat->hdrMeta.transferFunction);
	// 0 = unknown, 1 = 0-255, 2 = 16-235
	colorimetry->NominalRange = static_cast<DXVA_NominalRange>(videoFormat->quantisation);

	#ifndef NO_QUILL
	LOG_TRACE_L3(mLogData.logger, "[{}] DXVA_ExtendedFormat {} {} {} {}", mLogData.prefix,
		static_cast<int>(colorimetry->VideoTransferMatrix),
		static_cast<int>(colorimetry->VideoPrimaries), static_cast<int>(colorimetry->VideoTransferFunction),
		static_cast<int>(colorimetry->NominalRange));
	#endif

	pvi->dwControlFlags += AMCONTROL_USED;
	pvi->dwControlFlags += AMCONTROL_COLORINFO_PRESENT;

	auto isRgb = videoFormat->pixelFormat.GetBiCompression() == BI_RGB;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biWidth = videoFormat->cx;
	pvi->bmiHeader.biHeight = isRgb && videoFormat->bottomUpDib ? -(videoFormat->cy) : videoFormat->cy;
	// RGB on windows is upside down
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biBitCount = videoFormat->pixelFormat.bitsPerPixel;
	pvi->bmiHeader.biCompression = videoFormat->pixelFormat.GetBiCompression();
	pvi->bmiHeader.biSizeImage = videoFormat->imageSize;
	pvi->bmiHeader.biXPelsPerMeter = 0;
	pvi->bmiHeader.biYPelsPerMeter = 0;
	pvi->bmiHeader.biClrUsed = 0;
	pvi->bmiHeader.biClrImportant = 0;

	// Work out the GUID for the subtype from the header info.
	const auto subTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
	pmt->SetSubtype(&subTypeGUID);
}

bool video_capture_pin::ShouldChangeMediaType(video_format* newVideoFormat, bool pixelFallBackIsActive)
{
	auto reconnect = false;
	if (newVideoFormat->cx != mVideoFormat.cx || newVideoFormat->cy != mVideoFormat.cy)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video dimension change {}x{} to {}x{}",
			mLogData.prefix, mVideoFormat.cx, mVideoFormat.cy,
			newVideoFormat->cx, newVideoFormat->cy);
		#endif
	}
	if (newVideoFormat->aspectX != mVideoFormat.aspectX || newVideoFormat->aspectY != mVideoFormat.aspectY)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video AR change {}x{} to {}x{}",
			mLogData.prefix, mVideoFormat.aspectX, mVideoFormat.aspectY,
			newVideoFormat->aspectX, newVideoFormat->aspectY);
		#endif
	}
	if (abs(newVideoFormat->frameInterval - mVideoFormat.frameInterval) >= 100)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video FPS change {:.3f} to {:.3f}",
			mLogData.prefix, mVideoFormat.fps, newVideoFormat->fps);
		#endif
	}
	if (mVideoFormat.pixelFormat.format != newVideoFormat->pixelFormat.format && !pixelFallBackIsActive)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video pixel format change {} to {}",
			mLogData.prefix, mVideoFormat.pixelFormat.name, newVideoFormat->pixelFormat.name);
		#endif
	}
	if (mVideoFormat.colourFormat != newVideoFormat->colourFormat)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video colour format change {} to {}",
			mLogData.prefix, mVideoFormat.colourFormatName, newVideoFormat->colourFormatName);
		#endif
	}
	if (mVideoFormat.quantisation != newVideoFormat->quantisation || mVideoFormat.saturation != newVideoFormat->
		saturation)
	{
		reconnect = true;

		#ifndef NO_QUILL
		LOG_INFO(mLogData.logger, "[{}] Video colorimetry change quant {} to {} sat {} to {}",
			mLogData.prefix,
			static_cast<int>(mVideoFormat.quantisation), static_cast<int>(newVideoFormat->quantisation),
			static_cast<int>(mVideoFormat.saturation), static_cast<int>(newVideoFormat->saturation)
		);
		#endif
	}
	auto incomingTransferFunction = newVideoFormat->hdrMeta.transferFunction;
	if (mVideoFormat.hdrMeta.transferFunction != newVideoFormat->hdrMeta.transferFunction)
	{
		reconnect = true;

		#ifndef NO_QUILL
		auto formatFrom = mVideoFormat.hdrMeta.transferFunction == 0
			? "?"
			: mVideoFormat.hdrMeta.transferFunction == 4
			? "REC.709"
			: mVideoFormat.hdrMeta.transferFunction == 15
			? "SMPTE ST 2084 (PQ)"
			: "HLG";
		auto formatTo = incomingTransferFunction == 0
			? "?"
			: incomingTransferFunction == 4
			? "REC.709"
			: incomingTransferFunction == 15
			? "SMPTE ST 2084 (PQ)"
			: "HLG";
		LOG_INFO(mLogData.logger, "[{}] Video transfer function change {} ({}) to {} ({})",
			mLogData.prefix, formatFrom, mVideoFormat.hdrMeta.transferFunction, formatTo,
			incomingTransferFunction);
		#endif
	}

	return reconnect;
}

HRESULT video_capture_pin::DoChangeMediaType(const CMediaType* pNewMt, const video_format* newVideoFormat)
{
	#ifndef NO_QUILL
	LOG_WARNING(mLogData.logger,
		"[{}] Proposing new video format {} x {} ({}:{}) @ {:.3f} Hz in {} bits ({} {} tf: {}) size {} bytes",
		mLogData.prefix, newVideoFormat->cx, newVideoFormat->cy, newVideoFormat->aspectX,
		newVideoFormat->aspectY, newVideoFormat->fps, newVideoFormat->pixelFormat.bitDepth,
		newVideoFormat->pixelFormat.name, newVideoFormat->colourFormatName,
		newVideoFormat->hdrMeta.transferFunction, newVideoFormat->imageSize);

	if (mLogData.logger->should_log_statement(quill::LogLevel::TraceL3))
	{
		AM_MEDIA_TYPE currentMt;
		if (SUCCEEDED(m_Connected->ConnectionMediaType(&currentMt)))
		{
			LOG_TRACE_L3(mLogData.logger, "[{}] MT,lSampleSize,biBitCount,biWidth,biHeight,biSizeImage",
				mLogData.prefix);

			auto currentHeader = reinterpret_cast<VIDEOINFOHEADER2*>(currentMt.pbFormat);
			auto currentBmi = currentHeader->bmiHeader;

			LOG_TRACE_L3(mLogData.logger, "[{}] current,{},{},{},{},{}", mLogData.prefix, currentMt.lSampleSize,
				currentBmi.biBitCount, currentBmi.biWidth, currentBmi.biHeight, currentBmi.biSizeImage);

			auto newHeader = reinterpret_cast<VIDEOINFOHEADER2*>(pNewMt->pbFormat);
			auto newBmi = newHeader->bmiHeader;

			LOG_TRACE_L3(mLogData.logger, "[{}] proposed,{},{},{},{},{}", mLogData.prefix, pNewMt->lSampleSize,
				newBmi.biBitCount, newBmi.biWidth, newBmi.biHeight, newBmi.biSizeImage);

			FreeMediaType(currentMt);
		}
	}
	#endif

	auto retVal = RenegotiateMediaType(pNewMt, newVideoFormat->imageSize,
		newVideoFormat->imageSize != mVideoFormat.imageSize);
	if (retVal == S_OK)
	{
		mVideoFormat = *newVideoFormat;
		OnChangeMediaType();
	}

	return retVal;
}

STDMETHODIMP video_capture_pin::GetNumberOfCapabilities(int* piCount, int* piSize)
{
	*piCount = 1;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

STDMETHODIMP video_capture_pin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC)
{
	if (iIndex > 0)
	{
		return S_FALSE;
	}
	if (iIndex < 0)
	{
		return E_INVALIDARG;
	}
	CMediaType cmt;
	GetMediaType(0, &cmt);
	*pmt = CreateMediaType(&cmt);

	auto pvi = reinterpret_cast<VIDEOINFOHEADER2*>((*pmt)->pbFormat);

	auto pvscc = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);

	pvscc->guid = FORMAT_VideoInfo2;
	pvscc->VideoStandard = AnalogVideo_PAL_D;
	pvscc->InputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->InputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MinCroppingSize.cx = 80;
	pvscc->MinCroppingSize.cy = 60;
	pvscc->MaxCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->CropGranularityX = 80;
	pvscc->CropGranularityY = 60;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = 80;
	pvscc->MinOutputSize.cy = 60;
	pvscc->MaxOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MaxFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MinBitsPerSecond = pvi->dwBitRate;
	pvscc->MaxBitsPerSecond = pvi->dwBitRate;

	return S_OK;
}

bool video_capture_pin::ProposeBuffers(ALLOCATOR_PROPERTIES* pProperties)
{
	pProperties->cbBuffer = mVideoFormat.imageSize;
	if (pProperties->cBuffers < 1)
	{
		// 1 works for mpc-vr, 16 works for madVR so go with that as a default if the input pin doesn't suggest a number.
		pProperties->cBuffers = 16;
		return false;
	}
	return true;
}

