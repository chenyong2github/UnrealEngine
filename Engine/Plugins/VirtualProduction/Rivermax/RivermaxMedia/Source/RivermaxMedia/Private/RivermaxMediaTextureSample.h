// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaIOCoreTextureSampleBase.h"
#include "MediaShaders.h"

/**
 * Implements a media texture sample for RivermaxMedia.
 */
class FRivermaxMediaTextureSample
	: public FMediaIOCoreTextureSampleBase
{
	using Super = FMediaIOCoreTextureSampleBase;

public:

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoData The video frame data.
	 * @param InSampleFormat The sample format.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param bInIsSRGB Whether the sample is in sRGB space.
	 */
	bool InitializeProgressive(void* InVideoBuffer, uint32 InWidth, uint32 InHeight, uint32 InVideoBufferSize, uint32 InStride, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, bool bInIsSRGB)
	{
		return Super::Initialize(InVideoBuffer
			, InVideoBufferSize
			, InStride
			, InWidth
			, InHeight
			, InSampleFormat
			, InTime
			, InFrameRate
			, InTimecode
			, bInIsSRGB);
	}

	/**
	 * Get YUV to RGB conversion matrix
	 *
	 * @return MediaIOCore Yuv To Rgb matrix
	 */
	virtual const FMatrix& GetYUVToRGBMatrix() const override
	{
		return MediaShaders::YuvToRgbRec709Scaled;
	}

};

/*
 * Implements a pool for Rivermax texture sample objects.
 */
class FRivermaxMediaTextureSamplePool : public TMediaObjectPool<FRivermaxMediaTextureSample> { };
