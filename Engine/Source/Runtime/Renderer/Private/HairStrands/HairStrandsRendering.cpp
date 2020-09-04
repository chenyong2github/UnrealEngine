// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

void ServiceLocalQueue();

void RenderHairPrePass(
	FRHICommandListImmediate& RHICmdList,
	FScene* Scene,
	TArray<FViewInfo>& Views,
	FHairStrandsRenderingData& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && Views[0].Family->ViewMode == VMI_Lit;
	if (bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		//SCOPED_GPU_STAT(RHICmdList, HairRendering);
		OutHairDatas.MacroGroupsPerViews = CreateHairStrandsMacroGroups(RHICmdList, Scene, Views);
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
	FHairStrandsRenderingData& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && Views[0].Family->ViewMode == VMI_Lit;
	if (bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		// Hair visibility pass
		TRefCountPtr<IPooledRenderTarget> SceneColor = SceneContext.IsSceneColorAllocated() ? SceneContext.GetSceneColor() : nullptr;
		OutHairDatas.HairVisibilityViews = RenderHairStrandsVisibilityBuffer(RHICmdList, Scene, Views, SceneContext.GBufferB, SceneColor, SceneContext.SceneDepthZ, SceneContext.SceneVelocity, OutHairDatas.MacroGroupsPerViews);

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
