// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PixelFormat.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RenderResource.h"
#include "UnrealClient.h"


class FViewport;
class FRHITexture2D;


///////////////////////////////////////////////////////////////////
struct FDisplayClusterViewportResourceSettings
{
public:
	FDisplayClusterViewportResourceSettings(FViewport* InViewport);
	FDisplayClusterViewportResourceSettings(FRHITexture2D* InTexture);

public:
	bool operator==(const FDisplayClusterViewportResourceSettings& In) const
	{
		return (In.Size == Size) &&
			(In.Format == Format) &&
			(In.bShouldUseSRGB == bShouldUseSRGB) &&
			(In.DisplayGamma == DisplayGamma) &&
			(In.bIsRenderTargetable == bIsRenderTargetable) &&
			(In.NumMips== NumMips);
	}

public:
	FIntPoint    Size;
	EPixelFormat Format = EPixelFormat(0);
	bool         bShouldUseSRGB = false;

	// Render target only params
	float DisplayGamma = 1;

	// Texture target only params
	bool bIsRenderTargetable = false;
	int NumMips = 1;
};


///////////////////////////////////////////////////////////////////
class FDisplayClusterTextureResource
{
public:
	FDisplayClusterTextureResource(const FDisplayClusterViewportResourceSettings& InSettings)
		: InitSettings(InSettings)
		, BackbufferFrameOffset(EForceInit::ForceInitToZero)
	{ }

public:
	void InitDynamicRHI();
	void ReleaseRHI();

	bool IsResourceSettingsEqual(const FDisplayClusterViewportResourceSettings& InResourceSettings) const
	{
		return InitSettings == InResourceSettings;
	}

	const FTexture2DRHIRef& GetTextureResource() const
	{
		return (const FTexture2DRHIRef&)TextureRHI;
	}

public:
	const FDisplayClusterViewportResourceSettings InitSettings;

	// OutputFrameTargetableResources frame offset on backbuffer (special for 'side_by_side' and 'top_bottom' DCRenderDevices)
	FIntPoint BackbufferFrameOffset;

private:
	FTexture2DRHIRef TextureRHI;
};


///////////////////////////////////////////////////////////////////
class FDisplayClusterRenderTargetResource
	: public FTexture
	, public FRenderTarget
{
public:
	FDisplayClusterRenderTargetResource(const FDisplayClusterViewportResourceSettings& InResourceSettings)
		: ResourceSettings(InResourceSettings)
	{ }

public:
	virtual void InitDynamicRHI() override;

public:
	virtual const FTexture2DRHIRef& GetRenderTargetTexture() const
	{
		return (const FTexture2DRHIRef&)TextureRHI;
	}

	virtual FIntPoint GetSizeXY() const
	{
		return ResourceSettings.Size;
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const
	{
		return ResourceSettings.Size.X;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const
	{
		return ResourceSettings.Size.Y;
	}

	virtual float GetDisplayGamma() const 
	{
		return ResourceSettings.DisplayGamma;
	}

	virtual FString GetFriendlyName() const override 
	{
		return TEXT("FDisplayClusterRenderTargetResource");
	}

	bool IsResourceSettingsEqual(const FDisplayClusterViewportResourceSettings& InResourceSettings) const
	{
		return ResourceSettings == InResourceSettings;
	}

private:
	FDisplayClusterViewportResourceSettings ResourceSettings;
};
