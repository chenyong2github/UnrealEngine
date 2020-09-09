// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"

class FViewInfo;

struct FSceneWithoutWaterTextures
{
	struct FView
	{
		FIntRect ViewRect;
		FVector4 MinMaxUV;
	};

	FRDGTextureRef ColorTexture = nullptr;
	FRDGTextureRef DepthTexture = nullptr;
	TArray<FView> Views;
	float RefractionDownsampleFactor = 1.0f;
};

bool ShouldRenderSingleLayerWater(TArrayView<const FViewInfo> Views);
bool ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(TArrayView<const FViewInfo> Views);