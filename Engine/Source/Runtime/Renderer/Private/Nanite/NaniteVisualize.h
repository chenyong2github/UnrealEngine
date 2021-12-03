// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteCullRaster.h"

struct FNaniteMaterialPassCommand;

namespace Nanite
{

void AddVisualizationPasses(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextures& SceneTextures,
	const FEngineShowFlags& EngineShowFlags,
	TArrayView<const FViewInfo> Views,
	TArrayView<Nanite::FRasterResults> Results
);

#if WITH_DEBUG_VIEW_MODES

void RenderDebugViewMode(
	FRDGBuilder& GraphBuilder,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& NaniteMaterialPassCommands,
	const FSceneRenderer& SceneRenderer,
	const FSceneTextures& SceneTextures,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const FSceneViewFamily& ViewFamily,
	const FRasterResults& RasterResults,
	FRDGTextureRef QuadOverdrawTexture,
	const FRenderTargetBindingSlots& RenderTargets
);

#endif

} // namespace Nanite
