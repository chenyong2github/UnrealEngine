// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"

#include "RHI.h"
#include "RenderResource.h"
#include "UnrealClient.h"


///////////////////////////////////////////////////////////////////
void FDisplayClusterTextureResource::InitDynamicRHI()
{
	ETextureCreateFlags CreateFlags = TexCreate_Dynamic;

	// reflect srgb from settings
	if (InitSettings.bShouldUseSRGB)
	{
		CreateFlags |= TexCreate_SRGB;
	}

	if (InitSettings.NumMips > 1)
	{
		// Create nummips texture!
		CreateFlags |= TexCreate_GenerateMipCapable | TexCreate_UAV;

		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

		FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterTextureResource"), FClearValueBinding::None);

		RHICreateTargetableShaderResource2D(
			InitSettings.Size.X, 
			InitSettings.Size.Y, 
			InitSettings.Format, 
			InitSettings.NumMips,
			CreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			TextureRHI,
			DummyTexture2DRHI
		);

		TextureRHI->SetName(TEXT("DisplayClusterTextureMipsResource"));
	}
	else
	{
		if (InitSettings.bIsRenderTargetable)
		{
			CreateFlags |= TexCreate_RenderTargetable | TexCreate_ShaderResource;
		}
		else
		{
			// Shader resource usage only
			CreateFlags |= TexCreate_ResolveTargetable | TexCreate_ShaderResource;
		}

		FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterTextureResource"), FClearValueBinding::None);
		TextureRHI = RHICreateTexture2D(InitSettings.Size.X, InitSettings.Size.Y, InitSettings.Format, InitSettings.NumMips, 1, CreateFlags, ERHIAccess::SRVMask, CreateInfo);
	}
}

void FDisplayClusterTextureResource::ReleaseRHI()
{
	TextureRHI.SafeRelease();
}


///////////////////////////////////////////////////////////////////
void FDisplayClusterRenderTargetResource::InitDynamicRHI()
{
	// create output render target if necessary
	ETextureCreateFlags CreateFlags = (ResourceSettings.bShouldUseSRGB ? TexCreate_SRGB : TexCreate_None);

	// Create the sampler state RHI resource.
	FSamplerStateInitializerRHI SamplerStateInitializer
	(
		SF_Bilinear,
		AM_Clamp,
		AM_Clamp,
		AM_Clamp
	);
	SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

	FTexture2DRHIRef Texture2DRHI;
	FRHIResourceCreateInfo CreateInfo(TEXT("DisplayClusterRenderTargetResource"), FClearValueBinding(FLinearColor::Black));

	RHICreateTargetableShaderResource2D(
		GetSizeX(),
		GetSizeY(),
		ResourceSettings.Format,
		1,
		CreateFlags,
		TexCreate_RenderTargetable,
		false,
		CreateInfo,
		RenderTargetTextureRHI,
		Texture2DRHI
	);

	TextureRHI = (FTextureRHIRef&)Texture2DRHI;
}
