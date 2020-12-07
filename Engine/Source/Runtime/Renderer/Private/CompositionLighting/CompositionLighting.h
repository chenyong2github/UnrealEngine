// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CompositionLighting.h: The center for all deferred lighting activities.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "SceneRendering.h"
#include "PostProcessDeferredDecals.h"

class FPersistentUniformBuffers;

/**
 * The center for all screen space processing activities (e.g. G-buffer manipulation, lighting).
 */
namespace CompositionLighting
{
	struct FAsyncResults
	{
		FRDGTextureRef HorizonsTexture = nullptr;
	};

	bool CanProcessAsync(TArrayView<const FViewInfo> Views);

	void ProcessBeforeBasePass(
		FRDGBuilder& GraphBuilder,
		TArrayView<const FViewInfo> Views,
		const FSceneTextures& SceneTextures,
		FDBufferTextures& DBufferTextures);

	void ProcessAfterBasePass(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FSceneTextures& SceneTextures,
		const FAsyncResults& AsyncResults,
		bool bEnableSSAO);

	FAsyncResults ProcessAsync(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, const FMinimalSceneTextures& SceneTextures);
};

extern bool ShouldRenderScreenSpaceAmbientOcclusion(const FViewInfo& View);