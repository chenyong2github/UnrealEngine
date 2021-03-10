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
class FCompositionLighting
{
public:
	void Reset();

	void ProcessBeforeBasePass(
		FRDGBuilder& GraphBuilder,
		FPersistentUniformBuffers& UniformBuffers,
		const FViewInfo& View,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		bool bDBuffer,
		uint32 SSAOLevels);

	void ProcessAfterBasePass(
		FRDGBuilder& GraphBuilder,
		FPersistentUniformBuffers& UniformBuffers,
		const FViewInfo& View,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer);

	// only call if LPV is enabled
	void ProcessLpvIndirect(FRHICommandListImmediate& RHICmdList, FViewInfo& View);

	bool CanProcessAsyncSSAO(const TArray<FViewInfo>& Views);

	void ProcessAsyncSSAO(
		FRDGBuilder& GraphBuilder,
		const TArray<FViewInfo>& Views,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer);

private:
	FDeferredDecalPassTextures DecalPassTextures;
};

/** The global used for deferred lighting. */
extern FCompositionLighting GCompositionLighting;

extern bool ShouldRenderScreenSpaceAmbientOcclusion(const FViewInfo& View);