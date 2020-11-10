// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

void AddServiceLocalQueuePass(FRDGBuilder& GraphBuilder);

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
