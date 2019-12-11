// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

static void RenderDeepShadowFrontDepth(
	FRHICommandList& RHICmdList,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterData::TPrimitiveInfos& PrimitiveSceneInfo,
	const FIntRect& AtlasRect,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformShaderParameters,
	const TUniformBufferRef<FDeepShadowPassUniformParameters>& DeepShadowPassUniformParameters,
	const bool bClearOutput,
	TRefCountPtr<IPooledRenderTarget>& outShadowDepthRT)
{
	DECLARE_GPU_STAT(HairStrandsDeepShadowFrontDepth);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsDeepShadowFrontDepth);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsDeepShadowFrontDepth);

	EDepthStencilTargetActions DepthStencilAction = MakeDepthStencilTargetActions(
		MakeRenderTargetActions(bClearOutput ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore),
		MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction));
	FRHIRenderPassInfo RPInfo(outShadowDepthRT->GetRenderTargetItem().TargetableTexture, DepthStencilAction, nullptr, FExclusiveDepthStencil::DepthWrite_StencilNop);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DeepShadowDepth"));
	RasterHairStrands(
		RHICmdList,
		Scene,
		ViewInfo,
		PrimitiveSceneInfo,
		EHairStrandsRasterPassType::FrontDepth,
		AtlasRect,
		ViewUniformShaderParameters,
		DeepShadowPassUniformParameters);
	RHICmdList.EndRenderPass();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void RenderDeepShadowLayers(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterData::TPrimitiveInfos& PrimitiveSceneInfo,
	const FIntRect& AtlasRect,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformShaderParameters,
	const TUniformBufferRef<FDeepShadowPassUniformParameters>& DeepShadowPassUniformParameters,
	const bool bClearOutput,
	TRefCountPtr<IPooledRenderTarget>& outDeepShadowLayersRT)
{
	DECLARE_GPU_STAT(HairStrandsDeepShadowLayers);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsDeepShadowLayers);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsDeepShadowLayers);

	FRHIRenderPassInfo RPInfo(outDeepShadowLayersRT->GetRenderTargetItem().TargetableTexture, bClearOutput ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("DeepShadowLayers"));
	RasterHairStrands(
		RHICmdList,
		Scene,
		ViewInfo,
		PrimitiveSceneInfo,
		EHairStrandsRasterPassType::DeepOpacityMap,
		AtlasRect,
		ViewUniformShaderParameters,
		DeepShadowPassUniformParameters);
	RHICmdList.EndRenderPass();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeepShadowViews RenderHairStrandsDeepShadows(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views,
	const FHairStrandsClusterViews& DeepShadowClusterViews)
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
			const FHairStrandsClusterDatas& DeepShadowClusterDatas = DeepShadowClusterViews.Views[ViewIndex];
			DeepShadowsPerView.Views.AddDefaulted();

			if (DeepShadowClusterDatas.Datas.Num() == 0 || 
				VisibleLightsPerView[ViewIndex].Num() == 0 ||
				IsHairStrandsForVoxelTransmittanceAndShadowEnable()) 
			{
				continue; 
			}

			for (int32 ClusterIt = 0; ClusterIt < DeepShadowClusterDatas.Datas.Num(); ++ClusterIt)
			{
				const FHairStrandsClusterData& Cluster = DeepShadowClusterDatas.Datas[ClusterIt];
				const FBoxSphereBounds ClusterBounds = Cluster.Bounds;

				for (const FLightSceneInfo* LightInfo : VisibleLightsPerView[ViewIndex])
				{
					const FLightSceneProxy* LightProxy = LightInfo->Proxy;
					if (!LightProxy->AffectsBounds(ClusterBounds))
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
	TRefCountPtr<IPooledRenderTarget> FrontDepthAtlasTexture;
	TRefCountPtr<IPooledRenderTarget> DeepShadowLayersAtlasTexture;

	// Create Atlas resources for DOM
	bool bClearFrontDepthAtlasTexture = true;
	bool bClearLayerAtlasTexture = true;
	{
		{
			FPooledRenderTargetDesc ShadowDesc(FPooledRenderTargetDesc::Create2DDesc(AtlasResolution, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, ShadowDesc, FrontDepthAtlasTexture, TEXT("ShadowDepth"));
		}

		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(AtlasResolution, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DeepShadowLayersAtlasTexture, TEXT("DeepShadowLayers"));
		}
	}

	// @todo_hair: could we share the rendering of the deep shadow across all view? Is it needed?
	// @todo_hair: need to clarify what how view are/should be managed. At the moment this is 
	// confusing as it seems there are many views but in the renderLight code, only one shadow mask 
	// texture is created for all lights.
	uint32 AtlasSlotIndex = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& ViewInfo = Views[ViewIndex];
		if (ViewInfo.Family)
		{
			FHairStrandsDeepShadowDatas& DeepShadowDatas = DeepShadowsPerView.Views[ViewIndex];

			const FHairStrandsClusterDatas& DeepShadowClusterDatas = DeepShadowClusterViews.Views[ViewIndex];
			for (int32 ClusterIt = 0; ClusterIt < DeepShadowClusterDatas.Datas.Num(); ++ClusterIt)
			{
				// List of all the light in the scene.
				uint32 LightLocalIndex = 0;
				for (const FLightSceneInfo* LightInfo : VisibleLightsPerView[ViewIndex])
				{
					const FHairStrandsClusterData& Cluster = DeepShadowClusterDatas.Datas[ClusterIt];
					FBoxSphereBounds ClusterBounds = Cluster.Bounds;

					const FLightSceneProxy* LightProxy = LightInfo->Proxy;
					if (!LightProxy->AffectsBounds(ClusterBounds))
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
					ComputeWorldToLightClip(DomData.WorldToLightTransform, MinStrandRadiusAtDepth1, ClusterBounds, *LightProxy, LightType, AtlasSlotResolution);
					DomData.LightDirection = LightProxy->GetDirection();
					DomData.LightPosition = FVector4(LightProxy->GetPosition(), LightType == ELightComponentType::LightType_Directional ? 0 : 1);
					DomData.LightLuminance = LightProxy->GetColor();
					DomData.LightType = LightType;
					DomData.LightId = LightInfo->Id;
					DomData.ShadowResolution = AtlasSlotResolution;
					DomData.Bounds = ClusterBounds;
					DomData.AtlasRect = FIntRect(AtlasRectOffset, AtlasRectOffset + AtlasSlotResolution);
					DomData.ClusterId = Cluster.ClusterId;
					DomData.DepthTexture = FrontDepthAtlasTexture;
					DomData.LayersTexture = DeepShadowLayersAtlasTexture;
					AtlasSlotIndex++;

					TUniformBufferRef<FDeepShadowPassUniformParameters> DeepShadowPassUniformParameters;
					{
						FDeepShadowPassUniformParameters PassParameters;
						PassParameters.WorldToClipMatrix = DomData.WorldToLightTransform;;
						PassParameters.SliceValue = FVector4(1, 1, 1, 1);
						PassParameters.FrontDepthTexture = DomData.DepthTexture->GetRenderTargetItem().TargetableTexture.GetReference();
						PassParameters.AtlasRect = DomData.AtlasRect;
						PassParameters.VoxelMinAABB = FVector::ZeroVector;
						PassParameters.VoxelMaxAABB = FVector::ZeroVector;
						PassParameters.VoxelResolution = 0;
						DeepShadowPassUniformParameters = CreateUniformBufferImmediate(PassParameters, UniformBuffer_SingleFrame, EUniformBufferValidation::None);
					}

					TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
					{
						const float bIsOrtho = LightType == ELightComponentType::LightType_Directional;

						// Save some status we need to restore
						const FVector SavedViewForward = ViewInfo.CachedViewUniformShaderParameters->ViewForward;
						// Update our view parameters
						ViewInfo.CachedViewUniformShaderParameters->HairRenderInfo.X = MinStrandRadiusAtDepth1.Primary;
						ViewInfo.CachedViewUniformShaderParameters->HairRenderInfo.Y = MinStrandRadiusAtDepth1.Primary;
						ViewInfo.CachedViewUniformShaderParameters->HairRenderInfo.Z = bIsOrtho;
						ViewInfo.CachedViewUniformShaderParameters->ViewForward = DomData.LightDirection;
						// Create the uniform buffer
						ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
						// Restore the view cached parameters
						ViewInfo.CachedViewUniformShaderParameters->ViewForward = SavedViewForward;
					}

					RenderDeepShadowFrontDepth(
						RHICmdList,
						Scene,
						&ViewInfo,
						Cluster.PrimitivesInfos,
						DomData.AtlasRect,
						ViewUniformShaderParameters,
						DeepShadowPassUniformParameters,
						bClearFrontDepthAtlasTexture,
						DomData.DepthTexture);

					RenderDeepShadowLayers(
						RHICmdList,
						Scene,
						&ViewInfo,
						Cluster.PrimitivesInfos,
						DomData.AtlasRect,
						ViewUniformShaderParameters,
						DeepShadowPassUniformParameters,
						bClearLayerAtlasTexture,
						DomData.LayersTexture);

					bClearFrontDepthAtlasTexture = false;
					bClearLayerAtlasTexture = false;

					++LightLocalIndex;
				}
			}
		}
	}

	return DeepShadowsPerView;
}
