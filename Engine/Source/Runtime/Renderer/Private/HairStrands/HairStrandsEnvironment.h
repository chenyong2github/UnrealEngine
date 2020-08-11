// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsEnvironment.h: Hair strands environment lighting.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

void RenderHairStrandsAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const struct FHairStrandsRenderingData* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture);

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsRenderingData* HairDatas);

void RenderHairStrandsSceneColorScattering(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsRenderingData* HairDatas);
