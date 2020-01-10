// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Queue.h"
#include "Math/Color.h"
#include "MediaSampleSource.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"
#include "TextureResource.h"
#include "UnrealClient.h"

class FMediaPlayerFacade;
class IMediaPlayer;
class IMediaTextureSample;
class UMediaTexture;
struct FGenerateMipsStruct;

enum class EMediaTextureSinkFormat;
enum class EMediaTextureSinkMode;


/**
 * Texture resource type for media textures.
 */
class FMediaTextureResource
	: public FRenderTarget
	, public FTextureResource
{
public:

	/** 
	 * Creates and initializes a new instance.
	 *
	 * @param InOwner The Movie texture object to create a resource for (must not be nullptr).
	 * @param InOwnerDim Reference to the width and height of the texture that owns this resource (will be updated by resource).
	 * @param InOWnerSize Reference to the size in bytes of the texture that owns this resource (will be updated by resource).
	 * @param InClearColor The initial clear color.
	 * @param InTextureGuid The initial external texture GUID.
	 * @param bEnableGenMips If true mips generation will be enabled (possibly optimizing for NumMips == 1 case)
	 * @param InNumMips The initial number of mips to be generated for the output texture
	 */
	FMediaTextureResource(UMediaTexture& InOwner, FIntPoint& InOwnerDim, SIZE_T& InOwnerSize, FLinearColor InClearColor, FGuid InTextureGuid, bool bEnableGenMips, uint8 InNumMips);

	/** Virtual destructor. */
	virtual ~FMediaTextureResource() { }

public:

	/** Parameters for the Render method. */
	struct FRenderParams
	{
		/** Whether the texture can be cleared. */
		bool CanClear;

		/** The clear color to use when clearing the texture. */
		FLinearColor ClearColor;

		/** The texture's current external texture GUID. */
		FGuid CurrentGuid;

		/** The texture's previously used external texture GUID. */
		FGuid PreviousGuid;

		/** The player's play rate. */
		float Rate;

		/** The player facade that provides the video samples to render. */
		TWeakPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe> SampleSource;

		/** Whether output should be in sRGB color space. */
		bool SrgbOutput;

		/** Number of mips wanted */
		uint8 NumMips;

		/** The time of the video frame to render (in player's clock). */
		FTimespan Time;
	};

	/**
	 * Render the texture resource.
	 *
	 * This method is called on the render thread by the MediaTexture that owns this
	 * texture resource to clear or redraw the resource using the given parameters.
	 *
	 * @param Params Render parameters.
	 */
	void Render(const FRenderParams& Params);

public:

	//~ FRenderTarget interface

	virtual FIntPoint GetSizeXY() const override;

public:

	//~ FTextureResource interface

	virtual FString GetFriendlyName() const override;
	virtual uint32 GetSizeX() const override;
	virtual uint32 GetSizeY() const override;
	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;

protected:

	/**
	 * Clear the texture using the given clear color.
	 *
	 * @param ClearColor The clear color to use.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 */
	void ClearTexture(const FLinearColor& ClearColor, bool SrgbOutput);

	/**
	 * Render the given texture sample by converting it on the GPU.
	 *
	 * @param Sample The texture sample to convert.
	 * @param ClearColor The clear color to use for the output texture.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 * @param Number of mips
	 * @see CopySample
	 */
	void ConvertSample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips);

	/**
	 * Render the given texture sample by using it as or copying it to the render target.
	 *
	 * @param Sample The texture sample to copy.
	 * @param ClearColor The clear color to use for the output texture.
	 * @param SrgbOutput Whether the output texture is in sRGB color space.
	 * @param Number of mips
	 * @see ConvertSample
	 */
	void CopySample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips, const FGuid & TextureGUID);

	/** Calculates the current resource size and notifies the owner texture. */
	void UpdateResourceSize();

	/**
	 * Set the owner's texture reference to the given texture.
	 *
	 * @param NewTexture The texture to set.
	 */
	void UpdateTextureReference(FRHITexture2D* NewTexture);

	/**
	 * Create/update output render target as needed
	 */
	void CreateOutputRenderTarget(const FIntPoint & InDim, EPixelFormat InPixelFormat, bool bInSRGB, const FLinearColor & InClearColor, uint8 InNumMips);

	/**
	 * Caches next available sample from queue in MediaTexture owner to keep single consumer access
	 *
	 * @param InSampleQueue SampleQueue to query sample information from
	 */
	void CacheNextAvailableSampleTime(const TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe>& InSampleQueue) const;

	/** Setup sampler state from owner's settings as needed */
	void SetupSampler();

	/** Copy to local buffer from external texture */
	void CopyFromExternalTexture(const TSharedPtr <IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FGuid & TextureGUID);

private:

	// Class to maintain a reference to a IMediaTextureSample instance as long  as needed by RHI
	// (needed not to keep Texture for GPU - that is safe already - but to avoid reusing the buffer too early)
	class FTextureSampleKeeper : public FRHIResource
	{
	public:
		FTextureSampleKeeper(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> & InMediaSample)
			: MediaSample(InMediaSample)
		{}

		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> MediaSample;
	};

	/** Platform uses GL/ES ImageExternal */
	bool bUsesImageExternal;

	/** Whether the texture has been cleared. */
	bool Cleared;

	/** Tracks the current clear color. */
	FLinearColor CurrentClearColor;

	/** The external texture GUID to use when initializing this resource. */
	FGuid InitialTextureGuid;

	/** Input render target if the texture samples don't provide one (for conversions). */
	TRefCountPtr<FRHITexture2D> InputTarget;

	/** Output render target if the texture samples don't provide one. */
	TRefCountPtr<FRHITexture2D> OutputTarget;

	/** The media texture that owns this resource. */
	UMediaTexture& Owner;

	/** Reference to the owner's texture dimensions field. */
	FIntPoint& OwnerDim;

	/** Reference to the owner's texture size field. */
	SIZE_T& OwnerSize;

	/** Enable mips generation */
	bool bEnableGenMips;

	/** Current number of mips to be generated as output */
	uint8 CurrentNumMips;

	/** Current texture sampler filter value */
	ESamplerFilter CurrentSamplerFilter;

	/** The current media player facade to get video samples from. */
	TWeakPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> PlayerFacadePtr;

	/** cached media sample to postpone releasing it until the next sample rendering as it can get overwritten due to asynchronous rendering */
	TRefCountPtr<FTextureSampleKeeper> CurrentSample;

	/** cached params etc. for use with mip generator */
	TSharedPtr<FGenerateMipsStruct> CachedMipsGenParams;
};
