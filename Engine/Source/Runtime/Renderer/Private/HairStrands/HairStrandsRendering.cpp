// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

void ServiceLocalQueue();

void RenderHairPrePass(
	FRHICommandListImmediate& RHICmdList,
	FScene* Scene,
	TArray<FViewInfo>& Views,
	FHairStrandClusterData HairClusterData,
	FHairStrandsDatas& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && Views[0].Family->ViewMode == VMI_Lit;
	if (IsHairStrandsEnable(Scene->GetShaderPlatform()) && bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		//SCOPED_GPU_STAT(RHICmdList, HairRendering);
		OutHairDatas.MacroGroupsPerViews = CreateHairStrandsMacroGroups(RHICmdList, Scene, Views);

		// Culling/LOD pass for DOM and Voxelisation altogether
		FHairCullingParams CullingParams;
		CullingParams.bShadowViewMode = true;
		CullingParams.bCullingProcessSkipped = false;
		ComputeHairStrandsClustersCulling(RHICmdList, *GetGlobalShaderMap(FeatureLevel), Views, CullingParams, HairClusterData);

		ServiceLocalQueue();

		// Voxelization and Deep Opacity Maps
		VoxelizeHairStrands(RHICmdList, Scene, Views, OutHairDatas.MacroGroupsPerViews);
		RenderHairStrandsDeepShadows(RHICmdList, Scene, Views, OutHairDatas.MacroGroupsPerViews);

		ServiceLocalQueue();
	}
}

void RenderHairBasePass(
	FRHICommandListImmediate& RHICmdList,
	FScene* Scene,
	FSceneRenderTargets& SceneContext,
	TArray<FViewInfo>& Views,
	FHairStrandClusterData HairClusterData,
	FHairStrandsDatas& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && Views[0].Family->ViewMode == VMI_Lit;
	if (IsHairStrandsEnable(Scene->GetShaderPlatform()) && bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		//SCOPED_GPU_STAT(RHICmdList, HairRendering);
		// Culling/LOD pass for visibility (must be done after HZB is generated)
		FHairCullingParams CullingParams;
		CullingParams.bCullingProcessSkipped = false;
		CullingParams.bShadowViewMode = false;
		ComputeHairStrandsClustersCulling(RHICmdList, *GetGlobalShaderMap(FeatureLevel), Views, CullingParams, HairClusterData);

		// Hair visibility pass
		TRefCountPtr<IPooledRenderTarget> SceneColor = SceneContext.IsSceneColorAllocated() ? SceneContext.GetSceneColor() : nullptr;
		OutHairDatas.HairVisibilityViews = RenderHairStrandsVisibilityBuffer(RHICmdList, Scene, Views, SceneContext.GBufferB, SceneColor, SceneContext.SceneDepthZ, SceneContext.SceneVelocity, OutHairDatas.MacroGroupsPerViews);

		// Reset indirect draw buffer
		ResetHairStrandsClusterToLOD0(RHICmdList, *GetGlobalShaderMap(FeatureLevel), HairClusterData);

		ServiceLocalQueue();

		if (SceneContext.bScreenSpaceAOIsValid && SceneContext.ScreenSpaceAO)
		{
			RenderHairStrandsAmbientOcclusion(
				RHICmdList,
				Views,
				&OutHairDatas,
				SceneContext.ScreenSpaceAO);
		}
	}
}
