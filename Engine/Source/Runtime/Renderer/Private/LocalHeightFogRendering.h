// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineDefines.h"
#include "RenderGraph.h"
#include "RenderResource.h"
#include "RenderGraphResources.h"
#include "SceneView.h"

class FScene;
class FViewInfo;
struct FMinimalSceneTextures;

bool ShouldRenderLocalHeightFog(const FScene* Scene, const FSceneViewFamily& Family);

void RenderLocalHeightFog(
	const FScene* Scene,
	TArray<FViewInfo>& Views,
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef LightShaftOcclusionTexture);

