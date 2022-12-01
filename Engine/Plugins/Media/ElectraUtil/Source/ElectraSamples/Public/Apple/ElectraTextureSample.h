// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#include "MediaSamples.h"
#include "IMediaTextureSampleConverter.h"
#include "Misc/Timespan.h"
#include "MediaObjectPool.h"
#include "MediaVideoDecoderOutputApple.h"

#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>

class FElectraMediaTexConvApple;


class ELECTRASAMPLES_API FElectraTextureSample final
	: public IMediaTextureSample
    , public IMediaTextureSampleConverter
	, public IMediaPoolable
{
public:
	FElectraTextureSample(const TWeakPtr<FElectraMediaTexConvApple, ESPMode::ThreadSafe> & InTexConv)
		: Texture(nullptr)
		, TexConv(InTexConv)
	{
	}

	virtual ~FElectraTextureSample()
	{
	}

public:
	void Initialize(FVideoDecoderOutput* InVideoDecoderOutput);

	//~ IMediaTextureSample interface

	virtual const void* GetBuffer() override
	{
		return nullptr;
	}

	virtual FIntPoint GetDim() const override;

	virtual FTimespan GetDuration() const override;

	virtual double GetAspectRatio() const override
	{
		return VideoDecoderOutput->GetAspectRatio();
	}

	virtual EMediaOrientation GetOrientation() const override
	{
		return (EMediaOrientation)VideoDecoderOutput->GetOrientation();
	}

	virtual EMediaTextureSampleFormat GetFormat() const override;

	virtual FIntPoint GetOutputDim() const override;
	virtual uint32 GetStride() const override;

	virtual FRHITexture* GetTexture() const override
	{
		return Texture;
	}

	virtual FMediaTimeStamp GetTime() const override;

	virtual bool IsCacheable() const override
	{
		return true;
	}

    virtual IMediaTextureSampleConverter* GetMediaTextureSampleConverter() override
    {
        return this;
    }

	bool IsOutputSrgb() const override;
	const FMatrix& GetYUVToRGBMatrix() const override;
	bool GetFullRange() const override;
	FMatrix44f GetSampleToRGBMatrix() const override;
	FMatrix44f GetGamutToXYZMatrix() const override;
	FVector2f GetWhitePoint() const override;
	UE::Color::EEncoding GetEncodingType() const override;

	virtual void InitializePoolable() override;
	virtual void ShutdownPoolable() override;

private:
	/** The sample's texture resource. */
	TRefCountPtr<FRHITexture2D> Texture;

	/** Output data from video decoder. */
	TSharedPtr<FVideoDecoderOutputApple, ESPMode::ThreadSafe> VideoDecoderOutput;

	TWeakPtr<FElectraMediaTexConvApple, ESPMode::ThreadSafe> TexConv;

	/** Quick access for some HDR related info */
	TWeakPtr<const IVideoDecoderHDRInformation, ESPMode::ThreadSafe> HDRInfo;
	TWeakPtr<const IVideoDecoderColorimetry, ESPMode::ThreadSafe> Colorimetry;

	/** YUV matrix, adjusted to compensate for decoder output specific scale */
	FMatrix44f YuvToRgbMtx;

	virtual uint32 GetConverterInfoFlags() const override;
    virtual bool Convert(FTexture2DRHIRef & InDstTexture, const FConversionHints & Hints) override;
};


using FElectraTextureSamplePtr  = TSharedPtr<FElectraTextureSample, ESPMode::ThreadSafe>;
using FElectraTextureSampleRef  = TSharedRef<FElectraTextureSample, ESPMode::ThreadSafe>;


class ELECTRASAMPLES_API FElectraMediaTexConvApple
{
public:
    FElectraMediaTexConvApple();
    ~FElectraMediaTexConvApple();

#if WITH_ENGINE
    void ConvertTexture(FTexture2DRHIRef & InDstTexture, CVImageBufferRef InImageBufferRef, bool bFullRange, EMediaTextureSampleFormat Format, const FMatrix44f& YUVMtx, const FMatrix44f& GamutToXYZMtx, UE::Color::EEncoding EncodingType);
#endif

private:
#if WITH_ENGINE
    /** The Metal texture cache for unbuffered texture uploads. */
    CVMetalTextureCacheRef MetalTextureCache;
#endif
};


class ELECTRASAMPLES_API FElectraTextureSamplePool : public TMediaObjectPool<FElectraTextureSample, FElectraTextureSamplePool>
{
	using ParentClass = TMediaObjectPool<FElectraTextureSample, FElectraTextureSamplePool>;
	using TextureSample = FElectraTextureSample;

public:
	FElectraTextureSamplePool()
		: TMediaObjectPool<TextureSample, FElectraTextureSamplePool>(this)
		, TexConv(new FElectraMediaTexConvApple())
	{}

	TextureSample *Alloc() const
	{
		return new TextureSample(TexConv);
	}

	void PrepareForDecoderShutdown()
	{
	}

private:
	TSharedPtr<FElectraMediaTexConvApple, ESPMode::ThreadSafe> TexConv;
};

