// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaIOCoreTextureSampleBase.h"
#include "MediaShaders.h"

/**
 * Implements a media texture sample for AjaMedia.
 */
class FAjaMediaTextureSample
	: public FMediaIOCoreTextureSampleBase
{
	using Super = FMediaIOCoreTextureSampleBase;

public:

	virtual void ShutdownPoolable() override
	{
		if (DestructionCallback)
		{
			DestructionCallback(Texture);
		}

		Super::FreeSample();
	}

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
	bool InitializeProgressive(const AJA::AJAVideoFrameData& InVideoData, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, bool bInIsSRGB)
	{
		return Super::Initialize(InVideoData.VideoBuffer
			, InVideoData.VideoBufferSize
			, InVideoData.Stride
			, InVideoData.Width
			, InVideoData.Height
			, InSampleFormat
			, InTime
			, InFrameRate
			, InTimecode
			, bInIsSRGB);
	}

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoData The video frame data.
	 * @param InSampleFormat The sample format.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param bEven Only take the even frame from the image.
	 * @param bInIsSRGB Whether the sample is in sRGB space.
	 */
	bool InitializeInterlaced_Halfed(const AJA::AJAVideoFrameData& InVideoData, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, bool bInEven, bool bInIsSRGB)
	{
		return Super::InitializeWithEvenOddLine(bInEven
			, InVideoData.VideoBuffer
			, InVideoData.VideoBufferSize
			, InVideoData.Stride
			, InVideoData.Width
			, InVideoData.Height
			, InSampleFormat
			, InTime
			, InFrameRate
			, InTimecode
			, bInIsSRGB);
	}

	void SetTexture(TRefCountPtr<FRHITexture> InRHITexture)
	{
		Texture = MoveTemp(InRHITexture);
	}

	void SetDestructionCallback(TFunction<void(TRefCountPtr<FRHITexture2D>)> InDestructionCallback)
	{
		DestructionCallback = InDestructionCallback;
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

	virtual const void* GetBuffer() override
	{
		// Don't return the buffer if we have a texture to force the media player to use the texture if available. 
		if (Texture)
		{
			return nullptr;
		}
		return Buffer.GetData();

	}

	void* GetMutableBuffer()
	{
		return Buffer.GetData();
	}

#if WITH_ENGINE
	virtual FRHITexture* GetTexture() const override
	{
		if (Texture)
		{
			return Texture.GetReference();
		}
		return nullptr;
	}
#endif //WITH_ENGINE

private:
	/** Hold a texture to be used for gpu texture transfers. */
	TRefCountPtr<FRHITexture2D> Texture;

	/** Called when the sample is destroyed by its pool. */
	TFunction<void(TRefCountPtr<FRHITexture2D>)> DestructionCallback;
};

/*
 * Implements a pool for AJA texture sample objects.
 */
class FAjaMediaTextureSamplePool : public TMediaObjectPool<FAjaMediaTextureSample> { };
