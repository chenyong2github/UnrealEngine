// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsEnvironment.h: Hair strands environment lighting.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FScene;
class FViewInfo;
struct FHairStrandsRenderingData;

void RenderHairStrandsAmbientOcclusion(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FHairStrandsRenderingData* HairDatas,
	const FRDGTextureRef& InAOTexture);

void RenderHairStrandsEnvironmentLighting(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const uint32 ViewIndex,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas);

void RenderHairStrandsSceneColorScattering(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	const FScene* Scene,
	TArrayView<const FViewInfo> Views,
	FHairStrandsRenderingData* HairDatas);
