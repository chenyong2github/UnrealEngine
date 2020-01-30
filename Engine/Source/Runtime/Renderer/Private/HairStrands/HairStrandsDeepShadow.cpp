// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDeepShadow.h"
#include "HairStrandsRasterCommon.h"
#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "LightSceneInfo.h"
#include "ScenePrivate.h"

// this is temporary until we split the voxelize and DOM path
static int32 GDeepShadowResolution = 2048;
static FAutoConsoleVariableRef CVarDeepShadowResolution(TEXT("r.HairStrands.DeepShadow.Resolution"), GDeepShadowResolution, TEXT("Shadow resolution for Deep Opacity Map rendering. (default = 2048)"));

///////////////////////////////////////////////////////////////////////////////////////////////////

typedef TArray<const FLightSceneInfo*, SceneRenderingAllocator> FLightSceneInfos;
typedef TArray<FLightSceneInfos, SceneRenderingAllocator> FLightSceneInfosArray;

static FLightSceneInfosArray GetVisibleDeepShadowLights(const FScene* Scene, const TArray<FViewInfo>& Views)
{
	// Collect all visible lights per view
	FLightSceneInfosArray VisibleLightsPerView;
	VisibleLightsPerView.SetNum(Views.Num());
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		if (!LightSceneInfo->ShouldRenderLightViewIndependent())
			continue;

		// Check if the light is visible in any of the views.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const bool bIsCompatible = LightSceneInfo->ShouldRenderLight(Views[ViewIndex]) && LightSceneInfo->Proxy->CastsHairStrandsDeepShadow();
			if (!bIsCompatible)
				continue;

			VisibleLightsPerView[ViewIndex].Add(LightSceneInfo);
		}
	}

	return VisibleLightsPerView;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeepShadowViews RenderHairStrandsDeepShadows(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views,
	const FHairStrandsMacroGroupViews& MacroGroupsViews)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CLM_RenderDeepShadow);
	DECLARE_GPU_STAT(HairStrandsDeepShadow);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsDeepShadow);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsDeepShadow);

	FLightSceneInfosArray VisibleLightsPerView = GetVisibleDeepShadowLights(Scene, Views);

	// Compute the number of DOM which need to be created and insert default value
	FHairStrandsDeepShadowViews DeepShadowsPerView;
	uint32 DOMSlotCount = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupsViews.Views[ViewIndex];
			DeepShadowsPerView.Views.AddDefaulted();

			if (MacroGroupDatas.Datas.Num() == 0 || 
				VisibleLightsPerView[ViewIndex].Num() == 0 ||
				IsHairStrandsForVoxelTransmittanceAndShadowEnable()) 
			{
				continue; 
			}

			for (int32 MacroGroupIt = 0; MacroGroupIt < MacroGroupDatas.Datas.Num(); ++MacroGroupIt)
			{
				const FHairStrandsMacroGroupData& MacroGroup = MacroGroupDatas.Datas[MacroGroupIt];
				const FBoxSphereBounds MacroGroupBounds = MacroGroup.Bounds;

				for (const FLightSceneInfo* LightInfo : VisibleLightsPerView[ViewIndex])
				{
					const FLightSceneProxy* LightProxy = LightInfo->Proxy;
					if (!LightProxy->AffectsBounds(MacroGroupBounds))
					{
						continue;
					}

					DOMSlotCount++;
				}
			}
		}
	}

	if (DOMSlotCount == 0)
		return DeepShadowsPerView;

	const uint32 AtlasSlotX = FGenericPlatformMath::CeilToInt(FMath::Sqrt(DOMSlotCount));
	const FIntPoint AtlasSlotDimension(AtlasSlotX, AtlasSlotX == DOMSlotCount ? 1 : AtlasSlotX);
	const FIntPoint AtlasSlotResolution(GDeepShadowResolution, GDeepShadowResolution);
	const FIntPoint AtlasResolution(AtlasSlotResolution.X * AtlasSlotDimension.X, AtlasSlotResolution.Y * AtlasSlotDimension.Y);

	FRDGBuilder GraphBuilder(RHICmdList);

	// Create Atlas resources for DOM
	bool bClearFrontDepthAtlasTexture = true;
	bool bClearLayerAtlasTexture = true;
	FRDGTextureRef FrontDepthAtlasTexture = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(AtlasResolution, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false), TEXT("ShadowDepth"));
	FRDGTextureRef DeepShadowLayersAtlasTexture = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(AtlasResolution, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false), TEXT("DeepShadowLayers"));

	uint32 AtlasSlotIndex = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& ViewInfo = Views[ViewIndex];
		if (ViewInfo.Family)
		{
			FHairStrandsDeepShadowDatas& DeepShadowDatas = DeepShadowsPerView.Views[ViewIndex];

			const FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupsViews.Views[ViewIndex];
			for (int32 MacroGroupIt = 0; MacroGroupIt < MacroGroupDatas.Datas.Num(); ++MacroGroupIt)
			{
				// List of all the light in the scene.
				uint32 LightLocalIndex = 0;
				for (const FLightSceneInfo* LightInfo : VisibleLightsPerView[ViewIndex])
				{
					const FHairStrandsMacroGroupData& MacroGroup = MacroGroupDatas.Datas[MacroGroupIt];
					FBoxSphereBounds MacroGroupBounds = MacroGroup.Bounds;

					const FLightSceneProxy* LightProxy = LightInfo->Proxy;
					if (!LightProxy->AffectsBounds(MacroGroupBounds))
					{
						continue;
					}

					const ELightComponentType LightType = (ELightComponentType)LightProxy->GetLightType();

					FMinHairRadiusAtDepth1 MinStrandRadiusAtDepth1;
					const FIntPoint AtlasRectOffset(
						(AtlasSlotIndex % AtlasSlotDimension.X) * AtlasSlotResolution.X,
						(AtlasSlotIndex / AtlasSlotDimension.X) * AtlasSlotResolution.Y);

					// Note: LightPosition.W is used in the transmittance mask shader to differentiate between directional and local lights.
					FHairStrandsDeepShadowData& DomData = DeepShadowDatas.Datas.AddZeroed_GetRef();
					ComputeWorldToLightClip(DomData.WorldToLightTransform, MinStrandRadiusAtDepth1, MacroGroupBounds, *LightProxy, LightType, AtlasSlotResolution);
					DomData.LightDirection = LightProxy->GetDirection();
					DomData.LightPosition = FVector4(LightProxy->GetPosition(), LightType == ELightComponentType::LightType_Directional ? 0 : 1);
					DomData.LightLuminance = LightProxy->GetColor();
					DomData.LightType = LightType;
					DomData.LightId = LightInfo->Id;
					DomData.ShadowResolution = AtlasSlotResolution;
					DomData.Bounds = MacroGroupBounds;
					DomData.AtlasRect = FIntRect(AtlasRectOffset, AtlasRectOffset + AtlasSlotResolution);
					DomData.MacroGroupId = MacroGroup.MacroGroupId;					
					AtlasSlotIndex++;

					const bool bIsOrtho = LightType == ELightComponentType::LightType_Directional;
					const FVector4 HairRenderInfo = PackHairRenderInfo(MinStrandRadiusAtDepth1.Primary, MinStrandRadiusAtDepth1.Primary, 1, bIsOrtho, false);
					
					// Front depth
					{
						DECLARE_GPU_STAT(HairStrandsDeepShadowFrontDepth);
						SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsDeepShadowFrontDepth);
						SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsDeepShadowFrontDepth);

						FHairDeepShadowRasterPassParameters* PassParameters = GraphBuilder.AllocParameters<FHairDeepShadowRasterPassParameters>();
						PassParameters->WorldToClipMatrix = DomData.WorldToLightTransform;;
						PassParameters->SliceValue = FVector4(1, 1, 1, 1);
						PassParameters->AtlasRect = DomData.AtlasRect;
						PassParameters->ViewportResolution = AtlasSlotResolution;
						PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(FrontDepthAtlasTexture, bClearFrontDepthAtlasTexture ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

						AddHairDeepShadowRasterPass(
							GraphBuilder,
							Scene,
							&ViewInfo,
							MacroGroup.PrimitivesInfos,
							EHairStrandsRasterPassType::FrontDepth,
							DomData.AtlasRect,
							HairRenderInfo,
							DomData.LightDirection,
							PassParameters);
					}

					// Deep layers
					{
						DECLARE_GPU_STAT(HairStrandsDeepShadowLayers);
						SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsDeepShadowLayers);
						SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsDeepShadowLayers);

						FHairDeepShadowRasterPassParameters* PassParameters = GraphBuilder.AllocParameters<FHairDeepShadowRasterPassParameters>();
						PassParameters->WorldToClipMatrix = DomData.WorldToLightTransform;;
						PassParameters->SliceValue = FVector4(1, 1, 1, 1);
						PassParameters->AtlasRect = DomData.AtlasRect;
						PassParameters->ViewportResolution = AtlasSlotResolution;
						PassParameters->FrontDepthTexture = FrontDepthAtlasTexture;
						PassParameters->RenderTargets[0] = FRenderTargetBinding(DeepShadowLayersAtlasTexture, bClearLayerAtlasTexture ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, 0);

						AddHairDeepShadowRasterPass(
							GraphBuilder,
							Scene,
							&ViewInfo,
							MacroGroup.PrimitivesInfos,
							EHairStrandsRasterPassType::DeepOpacityMap,
							DomData.AtlasRect,
							HairRenderInfo,
							DomData.LightDirection,
							PassParameters);						
					}

					GraphBuilder.QueueTextureExtraction(FrontDepthAtlasTexture, &DomData.DepthTexture);
					GraphBuilder.QueueTextureExtraction(DeepShadowLayersAtlasTexture, &DomData.LayersTexture);

					bClearFrontDepthAtlasTexture = false;
					bClearLayerAtlasTexture = false;

					++LightLocalIndex;
				}
			}

			GraphBuilder.Execute();
		}
	}

	return DeepShadowsPerView;
}
