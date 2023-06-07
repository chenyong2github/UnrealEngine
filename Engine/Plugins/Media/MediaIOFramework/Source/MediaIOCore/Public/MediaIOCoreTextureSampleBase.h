// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaTextureSample.h"
#include "MediaObjectPool.h"


#include "ColorSpace.h"
#include "Misc/FrameRate.h"
#include "Templates/RefCounting.h"

class FRHITexture;

namespace UE::MediaIOCore
{
	struct FColorFormatArgs
	{
		FColorFormatArgs() = default;
		
		/** Bool constructor to allow backwards compatibility with previous method definitions of FMediaIOCoreTextureSampleBase::Initialize. */
		FColorFormatArgs(bool bIsSRGBInput)
		{
			if (bIsSRGBInput)
			{
				Encoding = UE::Color::EEncoding::sRGB;
				ColorSpace = UE::Color::EColorSpace::sRGB;
			}
			else
			{
				Encoding = UE::Color::EEncoding::Linear;
				ColorSpace = UE::Color::EColorSpace::sRGB;
			}
		}

		FColorFormatArgs(UE::Color::EEncoding InEncoding, UE::Color::EColorSpace InColorSpace)
			: Encoding(InEncoding)
			, ColorSpace(InColorSpace)
		{
		}
		

		/** Encoding of the texture. */
		UE::Color::EEncoding Encoding = UE::Color::EEncoding::Linear;

		/** Color space of the texture. */
		UE::Color::EColorSpace ColorSpace = UE::Color::EColorSpace::sRGB;
	};
}


/**
 * Implements the IMediaTextureSample/IMediaPoolable interface.
 */
class MEDIAIOCORE_API FMediaIOCoreTextureSampleBase
	: public IMediaTextureSample
	, public IMediaPoolable
{

public:
	FMediaIOCoreTextureSampleBase();

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoBuffer The video frame data.
	 * @param InBufferSize The size of the video buffer.
	 * @param InStride The number of channel of the video buffer.
	 * @param InWidth The sample rate of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 * @param InSampleFormat The sample format of the video buffer.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param InColorFormatArgs Information about the texture color encoding and color space.
	 */
	bool Initialize(const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs);

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoBuffer The video frame data.
	 * @param InStride The number of channel of the video buffer.
	 * @param InWidth The sample rate of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 * @param InSampleFormat The sample format of the video buffer.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param InColorFormatArgs Information about the texture color encoding and color space.
	 */
	bool Initialize(const TArray<uint8>& InVideoBuffer, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs);

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoBuffer The video frame data.
	 * @param InStride The number of channel of the video buffer.
	 * @param InWidth The sample rate of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 * @param InSampleFormat The sample format of the video buffer.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param InColorFormatArgs Information about the texture color encoding and color space.
	 */
	bool Initialize(TArray<uint8>&& InVideoBuffer, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs);

	/**
	 * Initialize the sample.
	 *
	 * @param InVideoBuffer The video frame data.
	 * @param InBufferSize The size of the video buffer.
	 */
	bool SetBuffer(const void* InVideoBuffer, uint32 InBufferSize);

	/**
	 * Set the sample buffer.
	 *
	 * @param InVideoBuffer The video frame data.
	 */
	bool SetBuffer(const TArray<uint8>& InVideoBuffer);

	/**
	 * Set the sample buffer.
	 *
	 * @param InVideoBuffer The video frame data.
	 */
	bool SetBuffer(TArray<uint8>&& InVideoBuffer);

	/**
	 * Set the sample properties.
	 *
	 * @param InStride The number of channel of the video buffer.
	 * @param InWidth The sample rate of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 * @param InSampleFormat The sample format of the video buffer.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InFrameRate The framerate of the media that produce the sample.
	 * @param InTimecode The sample timecode if available.
	 * @param InColorFormatArgs Information about the texture color encoding and color space.
	 */
	bool SetProperties(uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs);

	/**
	 * Initialize the sample with half it's original height and take only the odd or even line.
	 *
	 * @param bUseEvenLine Should use the Even or the Odd line from the video buffer.
	 * @param InVideoBuffer The video frame data.
	 * @param InStride The number of channel of the video buffer.
	 * @param InWidth The sample rate of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 * @param InSampleFormat The sample format of the video buffer.
	 * @param InTime The sample time (in the player's own clock).
	 * @param InTimecode The sample timecode if available.
	 * @param InColorFormatArgs Information about the texture color encoding and color space.
	 */
	bool InitializeWithEvenOddLine(bool bUseEvenLine, const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs);

	/**
	 * Set the sample buffer with half it's original height and take only the odd or even line.
	 *
	 * @param bUseEvenLine Should use the Even or the Odd line from the video buffer.
	 * @param InVideoBuffer The video frame data.
	 * @param InBufferSize The size of the video buffer.
	 * @param InStride The number of channel of the video buffer.
	 * @param InHeight The sample rate of the video buffer.
	 */
	bool SetBufferWithEvenOddLine(bool bUseEvenLine, const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InHeight);



	/**
	 * Request an uninitialized sample buffer.
	 * Should be used when the buffer could be filled by something else.
	 * SetProperties should still be called after.
	 *
	 * @param InBufferSize The size of the video buffer.
	 */
	virtual void* RequestBuffer(uint32 InBufferSize);

public:
	//~ IMediaTextureSample interface

	virtual FIntPoint GetDim() const override
	{
		switch (GetFormat())
		{
		case EMediaTextureSampleFormat::CharAYUV:
		case EMediaTextureSampleFormat::CharNV12:
		case EMediaTextureSampleFormat::CharNV21:
		case EMediaTextureSampleFormat::CharUYVY:
		case EMediaTextureSampleFormat::CharYUY2:
		case EMediaTextureSampleFormat::CharYVYU:
			return FIntPoint(Width / 2, Height);
		case EMediaTextureSampleFormat::YUVv210:
			// Data for 6 output pixels is contained in 4 actual texture pixels
			// Padding aligned on 48 (16 and 6 at the same time)
			return FIntPoint(4 * ((((Width + 47) / 48) * 48) / 6), Height);
		default:
			return FIntPoint(Width, Height);
		}
	}

	virtual FTimespan GetDuration() const override
	{
		return Duration;
	}

	virtual EMediaTextureSampleFormat GetFormat() const override
	{
		return SampleFormat;
	}

	virtual FIntPoint GetOutputDim() const override
	{
		return FIntPoint(Width, Height);
	}

	virtual uint32 GetStride() const override
	{
		return Stride;
	}

	virtual FMediaTimeStamp GetTime() const override
	{
		return FMediaTimeStamp(Time);
	}

	virtual TOptional<FTimecode> GetTimecode() const override
	{
		return Timecode;
	}

	virtual bool IsCacheable() const override
	{
		return true;
	}

	virtual bool IsOutputSrgb() const override;
	
	virtual FMatrix44f GetGamutToXYZMatrix() const override;
	virtual FVector2f GetWhitePoint() const override;
	virtual FVector2f GetDisplayPrimaryRed() const override;
	virtual FVector2f GetDisplayPrimaryGreen() const override;
	virtual FVector2f GetDisplayPrimaryBlue() const override;
	virtual UE::Color::EEncoding GetEncodingType() const override;
	virtual float GetHDRNitsNormalizationFactor() const override;

	virtual const void* GetBuffer() override
	{
		// Don't return the buffer if we have a texture to force the media player to use the texture if available. 
		if (Texture)
		{
			return nullptr;
		}

		if (ExternalBuffer)
		{
			return ExternalBuffer;
		}

		return Buffer.GetData();

	}

	void* GetMutableBuffer()
	{
		if (ExternalBuffer)
		{
			return ExternalBuffer;
		}

		return Buffer.GetData();
	}

#if WITH_ENGINE
	virtual FRHITexture* GetTexture() const override;
#endif //WITH_ENGINE

	void SetBuffer(void* InBuffer)
	{
		ExternalBuffer = InBuffer;
	}

	void SetTexture(TRefCountPtr<FRHITexture> InRHITexture);
	void SetDestructionCallback(TFunction<void(TRefCountPtr<FRHITexture>)> InDestructionCallback);

private:
	/** Hold a texture to be used for gpu texture transfers. */
	TRefCountPtr<FRHITexture> Texture;

	/** Called when the sample is destroyed by its pool. */
	TFunction<void(TRefCountPtr<FRHITexture>)> DestructionCallback;

public:
	//~ IMediaPoolable interface

	virtual void ShutdownPoolable() override;

protected:
	virtual void FreeSample()
	{
		Buffer.Reset();
	}

	/**
	 * Get YUV to RGB conversion matrix
	 *
	 * @return MediaIOCore Yuv To Rgb matrix
	 */
	virtual const FMatrix& GetYUVToRGBMatrix() const override;

protected:
	/** Duration for which the sample is valid. */
	FTimespan Duration;

	/** Sample format. */
	EMediaTextureSampleFormat SampleFormat;

	/** Sample time. */
	FTimespan Time;

	/** Sample timecode. */
	TOptional<FTimecode> Timecode;

	/** Image dimensions */
	uint32 Stride;
	uint32 Width;
	uint32 Height;

	/** Pointer to raw pixels */
	TArray<uint8, TAlignedHeapAllocator<4096>> Buffer;
	void* ExternalBuffer = nullptr;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.3, "Please use Encoding instead.")
	/** Whether the sample is in sRGB space and requires an explicit conversion to linear */
	bool bIsSRGBInput = false;
#endif

	/** Color encoding of the incoming texture. */
	UE::Color::EEncoding Encoding = UE::Color::EEncoding::Linear;

	/** Color space enum of the incoming texture. */
	UE::Color::EColorSpace ColorSpace = UE::Color::EColorSpace::sRGB;

	/** Color space structure of the incoming texture. Used for retrieving chromaticities. */
	UE::Color::FColorSpace ColorSpaceStruct = UE::Color::FColorSpace(UE::Color::EColorSpace::sRGB);
};
