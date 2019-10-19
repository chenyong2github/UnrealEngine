// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsUtils.h: Hair strands utils.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "SceneTypes.h"

struct FHairStrandsLightDesc
{
	FMatrix WorldToLightClipTransform;
	FVector LightDirection;
	FVector LightPosition;
	FMinHairRadiusAtDepth1 MinStrandRadiusAtDepth1;
	bool bIsOrtho;
};

FIntRect ComputeProjectedScreenRect(const FBox& B, const FViewInfo& View);

void ComputeWorldToLightClip(
	FMatrix& WorldToClipTransform,
	FMinHairRadiusAtDepth1& MinStrandRadiusAtDepth1,
	const FBoxSphereBounds& PrimitivesBounds,
	const class FLightSceneProxy& LightProxy,
	const ELightComponentType LightType,
	const FIntPoint& ShadowResolution);

FVector4 GetHairComponents();

float SampleCountToSubPixelSize(uint32 SamplePerPixelCount);

FIntRect ComputeVisibleHairStrandsClustersRect(const FIntRect& ViewRect, const struct FHairStrandsClusterDatas& ClusterDatas);

bool IsHairStrandsViewRectOptimEnable();

enum EHairVisibilityVendor
{
	HairVisibilityVendor_AMD,
	HairVisibilityVendor_NVIDIA,
	HairVisibilityVendor_INTEL,
	HairVisibilityVendorCount
};

EHairVisibilityVendor GetVendor();
uint32 GetVendorOptimalGroupSize1D();
FIntPoint GetVendorOptimalGroupSize2D();

RENDERER_API bool IsHairStrandsSupported(const EShaderPlatform Platform);

