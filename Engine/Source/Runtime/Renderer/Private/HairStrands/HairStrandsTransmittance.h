// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsTransmittance.h: Hair strands transmittance evaluation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

struct FHairStrandsTransmittanceMaskData
{
	TRefCountPtr<FPooledRDGBuffer>	TransmittanceMask;
	FShaderResourceViewRHIRef		TransmittanceMaskSRV;
};

/// Write opaque hair shadow onto screen shadow mask to have fine hair details cast onto opaque geometries
void RenderHairStrandsShadowMask(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const class FLightSceneInfo* LightSceneInfo,
	IPooledRenderTarget* ScreenShadowMaskTexture,
	const struct FHairStrandsDatas* Hairdatas);

/// Write hair transmittance onto screen shadow mask
FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const class FLightSceneInfo* LightSceneInfo,
	const struct FHairStrandsDatas* Hairdatas,
	TRefCountPtr<IPooledRenderTarget>& ScreenShadowMaskSubPixelTexture);