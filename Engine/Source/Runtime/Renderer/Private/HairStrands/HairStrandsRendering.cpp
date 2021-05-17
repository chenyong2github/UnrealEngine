// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

void AddServiceLocalQueuePass(FRDGBuilder& GraphBuilder);
bool IsHairStrandsAdaptiveVoxelAllocationEnable();

void RenderHairPrePass(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	TArray<FViewInfo>& Views,
	FHairStrandsRenderingData& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Views[0].GetShaderPlatform());
	if (bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		// Allocate voxel page allocation readback buffers
		const bool bAdaptiveAllocationEnable = IsHairStrandsAdaptiveVoxelAllocationEnable();
		for (FViewInfo& View : Views)
		{
			if (View.ViewState)
			{
				const bool bIsInit = View.ViewState->HairStrandsViewData.IsInit();
				// Init resources if the adaptive allocation is enabled, but the resources are not initialized yet.
				if (bAdaptiveAllocationEnable && !bIsInit)
				{
					View.ViewState->HairStrandsViewData.Init();
				}
				// Release resources if the adaptive allocation is disabled, but the resources are initialized.
				else if (!bAdaptiveAllocationEnable && bIsInit)
				{
					View.ViewState->HairStrandsViewData.Release();
				}
			}
		}

		//SCOPED_GPU_STAT(RHICmdList, HairRendering);
		OutHairDatas.MacroGroupsPerViews = CreateHairStrandsMacroGroups(GraphBuilder, Scene, Views);
		AddServiceLocalQueuePass(GraphBuilder);

		// Voxelization and Deep Opacity Maps
		VoxelizeHairStrands(GraphBuilder, Scene, Views, OutHairDatas.MacroGroupsPerViews);
		RenderHairStrandsDeepShadows(GraphBuilder, Scene, Views, OutHairDatas.MacroGroupsPerViews);

		AddServiceLocalQueuePass(GraphBuilder);
	}
}

void RenderHairBasePass(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	FSceneRenderTargets& SceneContext,
	TArray<FViewInfo>& Views,
	FHairStrandsRenderingData& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Views[0].GetShaderPlatform());
	if (bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		// Hair visibility pass
		TRefCountPtr<IPooledRenderTarget> SceneColor = SceneContext.IsSceneColorAllocated() ? SceneContext.GetSceneColor() : nullptr;
		OutHairDatas.HairVisibilityViews = RenderHairStrandsVisibilityBuffer(
			GraphBuilder, 
			Scene, 
			Views, 
			SceneContext.GBufferA, 
			SceneContext.GBufferB,
			SceneContext.GBufferC,
			SceneContext.GBufferD,
			SceneContext.GBufferE,
			SceneColor, 
			SceneContext.SceneDepthZ, 
			SceneContext.SceneVelocity, 
			OutHairDatas.MacroGroupsPerViews);
	}
}
