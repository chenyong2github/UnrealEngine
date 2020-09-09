// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsEnvironment.h: Hair strands environment lighting.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FViewInfo;
struct FHairStrandsRenderingData;

void RenderHairStrandsAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FHairStrandsRenderingData* HairDatas,
	const TRefCountPtr<IPooledRenderTarget>& InAOTexture);

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const uint32 ViewIndex,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas);

void RenderHairStrandsSceneColorScattering(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas);
