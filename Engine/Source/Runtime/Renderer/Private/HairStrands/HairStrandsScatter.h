// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

void AddHairDiffusionPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const struct FHairStrandsVisibilityData& VisibilityData,
	const struct FVirtualVoxelResources& VoxelResources,
	const FRDGTextureRef SceneColorDepth,
	FRDGTextureRef OutSceneColorSubPixelTexture,
	FRDGTextureRef OutSceneColorTexture);