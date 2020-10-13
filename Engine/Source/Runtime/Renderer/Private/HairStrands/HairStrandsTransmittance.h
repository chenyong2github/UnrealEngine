// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsTransmittance.h: Hair strands transmittance evaluation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FLightSceneInfo;
class FViewInfo;

struct FHairStrandsTransmittanceMaskData
{
	FRDGBufferRef TransmittanceMask = nullptr;
	FRDGBufferSRVRef TransmittanceMaskSRV = nullptr;
};

/// Write opaque hair shadow onto screen shadow mask to have fine hair details cast onto opaque geometries
void RenderHairStrandsShadowMask(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	const struct FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef ScreenShadowMaskTexture); 

/// Write hair transmittance onto screen shadow mask
FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const class FLightSceneInfo* LightSceneInfo,
	const struct FHairStrandsRenderingData* Hairdatas,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture);