// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_WINDOWS

#include "Player/WmfMediaTextureSample.h"
#include "RHI.h"

#include "IMediaTextureSampleConverter.h"
#include "WmfMediaHardwareVideoDecodingRendering.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "d3d11.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "RenderUtils.h"

#include "Microsoft/COMPointer.h"

/**
 * Texture sample for hardware video decoding.
 */
class WMFMEDIA_API FWmfMediaHardwareVideoDecodingTextureSample :
	public FWmfMediaTextureSample, 
	public IMediaTextureSampleConverter
{
public:

	/** Default constructor. */
	FWmfMediaHardwareVideoDecodingTextureSample()
		: FWmfMediaTextureSample(),
		Format(PF_Unknown)
	{ }

public:

	/**
	 * Initialize shared texture from Wmf device
	 *
	 * @param InD3D11Device WMF device to create shared texture from
	 * @param InTime The sample time (in the player's local clock).
	 * @param InDuration The sample duration
	 * @param InDim texture dimension to create
	 * @param InFormat texture format to create
	 * @param InMediaTextureSampleFormat Media texture sample format
	 * @param InCreateFlags texture create flag
	 * @return The texture resource object that will hold the sample data.
	 */
	ID3D11Texture2D* InitializeSourceTexture(const TRefCountPtr<ID3D11Device>& InD3D11Device, FTimespan InTime, FTimespan InDuration, const FIntPoint& InDim, EPixelFormat InFormat, EMediaTextureSampleFormat InMediaTextureSampleFormat);

	/**
	 * Get media texture sample converter if sample implements it
	 *
	 * @return texture sample converter
	 */
	virtual IMediaTextureSampleConverter* GetMediaTextureSampleConverter() override
	{
		// Only use sample converter for Win8+
		return FPlatformMisc::VerifyWindowsVersion(6, 2) ? this : nullptr;
	}

	/**
	 * Texture sample convert using hardware video decoding.
	 */
	virtual bool Convert(FTexture2DRHIRef & InDstTexture, const FConversionHints & Hints) override
	{
		FWmfMediaHardwareVideoDecodingParameters::ConvertTextureFormat_RenderThread(this, InDstTexture);
		return true;
	}

	/**
	 * Get source texture from WMF device
	 *
	 * @return Source texture
	 */
	TComPtr<ID3D11Texture2D> GetSourceTexture() const
	{
		return SourceTexture;
	}

	/**
	 * Get Destination Texture of render thread device
	 *
	 * @return Destination texture 
	 */
	FTexture2DRHIRef GetOrCreateDestinationTexture()
	{
		if (DestinationTexture.IsValid() && DestinationTexture->GetSizeX() == Dim.X && DestinationTexture->GetSizeY() == Dim.Y)
		{
			return DestinationTexture;
		}

		FRHIResourceCreateInfo CreateInfo;
		const ETextureCreateFlags CreateFlags = TexCreate_Dynamic | TexCreate_DisableSRVCreation;
		DestinationTexture = RHICreateTexture2D(
			Dim.X,
			Dim.Y,
			Format,
			1,
			1,
			CreateFlags,
			CreateInfo);

		return DestinationTexture;
	}

	/**
	 * Called the the sample is returned to the pool for cleanup purposes
	 */
#if !UE_SERVER
	virtual void ShutdownPoolable() override;
#endif

private:

	/** Source Texture resource (from Wmf device). */
	TComPtr<ID3D11Texture2D> SourceTexture;

	/** D3D11 Device which create the texture, used to release the keyed mutex when the sampled is returned to the pool */
	TRefCountPtr<ID3D11Device> D3D11Device;

	/** Destination Texture resource (from Rendering device) */
	FTexture2DRHIRef DestinationTexture;

	/** Texture format */
	EPixelFormat Format;
};

/** Implements a pool for WMF texture samples. */
class FWmfMediaHardwareVideoDecodingTextureSamplePool : public TMediaObjectPool<FWmfMediaHardwareVideoDecodingTextureSample> { };

#endif