// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRendering.h"
#include "HairStrandsData.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

static TRDGUniformBufferRef<FHairStrandsViewUniformParameters> InternalCreateHairStrandsViewUniformBuffer(
	FRDGBuilder& GraphBuilder, 
	FRDGTextureRef HairCategorizationTexture, 
	FRDGTextureRef HairDepthOnlyTexture)
{
	FHairStrandsViewUniformParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsViewUniformParameters>();
	Parameters->HairCategorizationTexture = HairCategorizationTexture ? HairCategorizationTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
	Parameters->HairOnlyDepthTexture = HairDepthOnlyTexture ? HairDepthOnlyTexture : GSystemTextures.GetDepthDummy(GraphBuilder);
	return GraphBuilder.CreateUniformBuffer(Parameters);
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairStrandsViewUniformParameters, "HairStrands");

void AddServiceLocalQueuePass(FRDGBuilder& GraphBuilder);

void RenderHairPrePass(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	TArray<FViewInfo>& Views,
	FInstanceCullingManager& InstanceCullingManager,
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
		VoxelizeHairStrands(GraphBuilder, Scene, Views, InstanceCullingManager, OutHairDatas.MacroGroupsPerViews);
		RenderHairStrandsDeepShadows(GraphBuilder, Scene, Views, InstanceCullingManager, OutHairDatas.MacroGroupsPerViews);

		AddServiceLocalQueuePass(GraphBuilder);
	}
}

void RenderHairBasePass(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	const FSceneTextures& SceneTextures,
	TArray<FViewInfo>& Views,
	FInstanceCullingManager& InstanceCullingManager,
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
			InstanceCullingManager,
			OutHairDatas.MacroGroupsPerViews);

		// Create RDG uniform buffer for each view
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (View.Family)
			{
				FHairStrandsVisibilityData VisibilityData = OutHairDatas.HairVisibilityViews.HairDatas[ViewIndex];
				Views[ViewIndex].HairStrandsViewData.UniformBuffer = InternalCreateHairStrandsViewUniformBuffer(GraphBuilder, VisibilityData.CategorizationTexture, VisibilityData.HairOnlyDepthTexture);
				Views[ViewIndex].HairStrandsViewData.bIsValid = true;
			}
		}
	}
}

namespace HairStrands
{

TRDGUniformBufferRef<FHairStrandsViewUniformParameters> CreateDefaultHairStrandsViewUniformBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View)
{
	return InternalCreateHairStrandsViewUniformBuffer(GraphBuilder, nullptr, nullptr);
}

TRDGUniformBufferRef<FHairStrandsViewUniformParameters> BindHairStrandsViewUniformParameters(const FViewInfo& View)
{
	return View.HairStrandsViewData.UniformBuffer;
}

bool HasViewHairStrandsData(const FViewInfo& View)
{
	return View.HairStrandsViewData.bIsValid;
}

}