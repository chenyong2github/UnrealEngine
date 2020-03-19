// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsDeepShadow.h: Hair strands deep shadow implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "RenderGraphResources.h"
#include "SceneTypes.h"

/// Hold deep shadow information for a given light.
struct FHairStrandsDeepShadowData
{
	static const uint32 MaxMacroGroupCount = 16u;

	FMatrix CPU_WorldToLightTransform;
	FMinHairRadiusAtDepth1 CPU_MinStrandRadiusAtDepth1;
	FIntRect AtlasRect;
	uint32 MacroGroupId = ~0;
	uint32 AtlasSlotIndex = 0;

	FIntPoint ShadowResolution = FIntPoint::ZeroValue;
	uint32 LightId = ~0;
	ELightComponentType LightType = LightType_MAX;
	FVector  LightDirection;
	FVector4 LightPosition;
	FLinearColor LightLuminance;

	FBoxSphereBounds Bounds;
};

struct FDeepShadowResources
{
	// Limit the number of atlas slot to 32, in order to create the view info per slot in single compute
	// This limitation can be alleviate, and is just here for convenience (see FDeepShadowCreateViewInfoCS)
	static const uint32 MaxAtlasSlotCount = 32u;

	uint32 TotalAtlasSlotCount = 0;
	FIntPoint AtlasSlotResolution;
	bool bIsGPUDriven = false;

	TRefCountPtr<IPooledRenderTarget> DepthAtlasTexture;
	TRefCountPtr<IPooledRenderTarget> LayersAtlasTexture;

	TRefCountPtr<FPooledRDGBuffer> DeepShadowWorldToLightTransforms;
	TRefCountPtr<FRHIShaderResourceView> DeepShadowWorldToLightTransformsSRV;
};

/// Store all deep shadows infos for a given view
struct FHairStrandsDeepShadowDatas
{
	TArray<FHairStrandsDeepShadowData, SceneRenderingAllocator> Datas;
};

void RenderHairStrandsDeepShadows(
	FRHICommandListImmediate& RHICmdList,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsMacroGroupViews& MacroGroupsViews);