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
	const FSceneTextures& SceneTextures,
	TArray<FViewInfo>& Views,
	FHairStrandsRenderingData& OutHairDatas)
{
	// #hair_todo: Add multi-view
	const bool bIsViewCompatible = Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Views[0].GetShaderPlatform());
	if (bIsViewCompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();

		// Hair visibility pass
		OutHairDatas.HairVisibilityViews = RenderHairStrandsVisibilityBuffer(
			GraphBuilder, 
			Scene, 
			Views, 
			SceneTextures.GBufferA,
			SceneTextures.GBufferB,
			SceneTextures.GBufferC,
			SceneTextures.GBufferD,
			SceneTextures.GBufferE,
			SceneTextures.Color.Resolve,
			SceneTextures.Depth.Resolve,
			SceneTextures.Velocity,
			OutHairDatas.MacroGroupsPerViews);
	}
}
