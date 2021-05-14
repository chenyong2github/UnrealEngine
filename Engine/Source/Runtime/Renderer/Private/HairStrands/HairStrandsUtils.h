// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsUtils.h: Hair strands utils.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "SceneTypes.h"

class FViewInfo;

FIntRect ComputeProjectedScreenRect(const FBox& B, const FViewInfo& View);

void ComputeWorldToLightClip(
	FMatrix& WorldToClipTransform,
	FMinHairRadiusAtDepth1& MinStrandRadiusAtDepth1,
	const FBoxSphereBounds& PrimitivesBounds,
	const class FLightSceneProxy& LightProxy,
	const ELightComponentType LightType,
	const FIntPoint& ShadowResolution);

struct FHairComponent
{
	bool R = true;
	bool TT = true;
	bool TRT = true;
	bool GlobalScattering = true;
	bool LocalScattering = true;
	bool TTModel = false;
};
FHairComponent GetHairComponents();
uint32 ToBitfield(const FHairComponent& Component);

float GetHairDualScatteringRoughnessOverride();

float SampleCountToSubPixelSize(uint32 SamplePerPixelCount);

FIntRect ComputeVisibleHairStrandsMacroGroupsRect(const FIntRect& ViewRect, const struct FHairStrandsMacroGroupDatas& Datas);

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

enum class  EHairStrandsCompositionType : uint8
{
	BeforeTranslucent,
	AfterTranslucent,
	AfterSeparateTranslucent,
	AfterTranslucentTranslucentBeforeAfterDOF
};

EHairStrandsCompositionType GetHairStrandsComposition();

FVector4 PackHairRenderInfo(
	float PrimaryRadiusAtDepth1,
	float StableRadiusAtDepth1,
	float VelocityRadiusAtDepth1,
	float VelocityScale);

uint32 PackHairRenderInfoBits(
	bool bIsOrtho,
	bool bIsGPUDriven);