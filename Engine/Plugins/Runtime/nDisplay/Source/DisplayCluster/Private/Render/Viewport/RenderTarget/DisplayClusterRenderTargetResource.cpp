// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"

#include "RHI.h"
#include "RenderResource.h"
#include "UnrealClient.h"

//------------------------------------------------------------------------------------------------
// FDisplayClusterViewportResource
//------------------------------------------------------------------------------------------------
FDisplayClusterViewportResource::FDisplayClusterViewportResource(const FDisplayClusterViewportResourceSettings& InResourceSettings)
	: ViewportResourceSettings(InResourceSettings)
{
	bSRGB = ViewportResourceSettings.bShouldUseSRGB;
	bGreyScaleFormat = false;
	bIgnoreGammaConversions = true;
}

void FDisplayClusterViewportResource::ImplInitDynamicRHI_RenderTargetResource2D(FTexture2DRHIRef& OutRenderTargetTextureRHI, FTexture2DRHIRef& OutTextureRHI)
{
	ETextureCreateFlags CreateFlags = TexCreate_Dynamic;

	// -- we will be manually copying this cross GPU, tell render graph not to
	CreateFlags |= TexCreate_MultiGPUGraphIgnore;

	// reflect srgb from settings
	if (bSRGB)
	{
		CreateFlags |= TexCreate_SRGB;
	}

	uint32 NumMips = 1;
	if (ViewportResourceSettings.NumMips > 1)
	{
		// Create nummips texture!
		CreateFlags |= TexCreate_GenerateMipCapable | TexCreate_UAV;
		NumMips = ViewportResourceSettings.NumMips;
	}

	FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterViewportRenderTargetResource"), FClearValueBinding::Black);

	RHICreateTargetableShaderResource2D(
		GetSizeX(),
		GetSizeY(),
		ViewportResourceSettings.Format,
		NumMips,
		CreateFlags,
		TexCreate_RenderTargetable,
		false,
		CreateInfo,
		OutRenderTargetTextureRHI,
		OutTextureRHI
	);
}

void FDisplayClusterViewportResource::ImplInitDynamicRHI_TextureResource2D(FTexture2DRHIRef& OutTextureRHI)
{
	ETextureCreateFlags CreateFlags = TexCreate_Dynamic | TexCreate_ShaderResource;

	// -- we will be manually copying this cross GPU, tell render graph not to
	CreateFlags |= TexCreate_MultiGPUGraphIgnore;

	// reflect srgb from settings
	if (bSRGB)
	{
		CreateFlags |= TexCreate_SRGB;
	}

	uint32 NumMips = 1;
	uint32 NumSamples = 1;

	CreateFlags |= ViewportResourceSettings.bIsRenderTargetable ? TexCreate_RenderTargetable : TexCreate_ResolveTargetable;

	FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterViewportTextureResource"), FClearValueBinding::Black);

	OutTextureRHI = RHICreateTexture2D(
		GetSizeX(),
		GetSizeY(),
		ViewportResourceSettings.Format,
		NumMips,
		NumSamples,
		CreateFlags,
		ERHIAccess::SRVMask,
		CreateInfo
	);
}

//------------------------------------------------------------------------------------------------
// FDisplayClusterViewportTextureResource
//------------------------------------------------------------------------------------------------
void FDisplayClusterViewportTextureResource::InitDynamicRHI()
{
	FTexture2DRHIRef NewTextureRHI;

	if (GetResourceSettingsConstRef().NumMips > 1)
	{
		FTexture2DRHIRef DummyTextureRHI;
		ImplInitDynamicRHI_RenderTargetResource2D(NewTextureRHI, DummyTextureRHI);
	}
	else
	{
		ImplInitDynamicRHI_TextureResource2D(NewTextureRHI);
	}

	TextureRHI = (FTextureRHIRef&)NewTextureRHI;
}

//------------------------------------------------------------------------------------------------
// FDisplayClusterViewportRenderTargetResource
//------------------------------------------------------------------------------------------------
void FDisplayClusterViewportRenderTargetResource::InitDynamicRHI()
{
	// Create RTT and shader resources
	FTexture2DRHIRef NewTextureRHI;
	ImplInitDynamicRHI_RenderTargetResource2D(RenderTargetTextureRHI, NewTextureRHI);
	TextureRHI = (FTextureRHIRef&)NewTextureRHI;

	// Create the sampler state RHI resource.
	FSamplerStateInitializerRHI SamplerStateInitializer
	(
		SF_Bilinear,
		AM_Clamp,
		AM_Clamp,
		AM_Clamp
	);
	SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);
}
