// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomDepthRendering.h"
#include "SceneUtils.h"
#include "DepthRendering.h"
#include "SceneRendering.h"
#include "SceneCore.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"

static TAutoConsoleVariable<int32> CVarCustomDepth(
	TEXT("r.CustomDepth"),
	1,
	TEXT("0: feature is disabled\n")
	TEXT("1: feature is enabled, texture is created on demand\n")
	TEXT("2: feature is enabled, texture is not released until required (should be the project setting if the feature should not stall)\n")
	TEXT("3: feature is enabled, stencil writes are enabled, texture is not released until required (should be the project setting if the feature should not stall)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCustomDepthOrder(
	TEXT("r.CustomDepth.Order"),
	2,
	TEXT("When CustomDepth (and CustomStencil) is getting rendered\n")
	TEXT("  0: Before Base Pass (Allows samping in DBuffer pass. Can be more efficient with AsyncCompute.)\n")
	TEXT("  1: After Base Pass\n")
	TEXT("  2: Default (Before Base Pass if DBuffer enabled.)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileCustomDepthDownSample(
	TEXT("r.Mobile.CustomDepthDownSample"),
	0,
	TEXT("Perform Mobile CustomDepth at HalfRes \n ")
	TEXT("0: Off (default)\n ")
	TEXT("1: On \n "),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCustomDepthTemporalAAJitter(
	TEXT("r.CustomDepthTemporalAAJitter"),
	1,
	TEXT("If disabled the Engine will remove the TemporalAA Jitter from the Custom Depth Pass. Only has effect when TemporalAA is used."),
	ECVF_RenderThreadSafe);

DECLARE_GPU_STAT_NAMED(CustomDepth, TEXT("Custom Depth"));

ECustomDepthPassLocation GetCustomDepthPassLocation(EShaderPlatform Platform)
{
	const int32 CustomDepthOrder = CVarCustomDepthOrder.GetValueOnRenderThread();
	const bool bCustomDepthBeforeBasePase = CustomDepthOrder == 0 || (CustomDepthOrder == 2 && IsUsingDBuffers(Platform));
	return bCustomDepthBeforeBasePase ? ECustomDepthPassLocation::BeforeBasePass : ECustomDepthPassLocation::AfterBasePass;
}

ECustomDepthMode GetCustomDepthMode()
{
	switch (CVarCustomDepth.GetValueOnRenderThread())
	{
	case 1: // Fallthrough.
	case 2: return ECustomDepthMode::Enabled;
	case 3: return ECustomDepthMode::EnabledWithStencil;
	}
	return ECustomDepthMode::Disabled;
}

bool IsCustomDepthPassWritingStencil(ERHIFeatureLevel::Type FeatureLevel)
{
	switch (GetCustomDepthMode())
	{
	case ECustomDepthMode::Disabled:
		return false;
	case ECustomDepthMode::Enabled:
		return FeatureLevel <= ERHIFeatureLevel::ES3_1;
	}
	return true;
}

uint32 GetCustomDepthDownsampleFactor(ERHIFeatureLevel::Type FeatureLevel)
{
	return FeatureLevel <= ERHIFeatureLevel::ES3_1 && CVarMobileCustomDepthDownSample.GetValueOnRenderThread() > 0 ? 2 : 1;
}

FCustomDepthTextures FCustomDepthTextures::Create(FRDGBuilder& GraphBuilder, FIntPoint Extent, ERHIFeatureLevel::Type FeatureLevel, uint32 DownsampleFactor)
{
	const ECustomDepthMode CustomDepthMode = GetCustomDepthMode();

	if (!IsCustomDepthPassEnabled())
	{
		return {};
	}

	const bool bWritesCustomStencil = IsCustomDepthPassWritingStencil(FeatureLevel);
	const FIntPoint CustomDepthExtent = FIntPoint::DivideAndRoundUp(Extent, DownsampleFactor);

	FCustomDepthTextures CustomDepthTextures;

	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		const float DepthFar = (float)ERHIZBuffer::FarPlane;
		const FClearValueBinding DepthFarColor = FClearValueBinding(FLinearColor(DepthFar, DepthFar, DepthFar, DepthFar));

		ETextureCreateFlags MobileCustomDepthFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		ETextureCreateFlags MobileCustomStencilFlags = MobileCustomDepthFlags;

		if (!bWritesCustomStencil)
		{
			MobileCustomStencilFlags |= TexCreate_Memoryless;
		}

		const FRDGTextureDesc MobileCustomDepthDesc = FRDGTextureDesc::Create2D(CustomDepthExtent, PF_R16F, DepthFarColor, MobileCustomDepthFlags);
		const FRDGTextureDesc MobileCustomStencilDesc = FRDGTextureDesc::Create2D(CustomDepthExtent, PF_G8, FClearValueBinding::Transparent, MobileCustomStencilFlags);

		CustomDepthTextures.MobileDepth = GraphBuilder.CreateTexture(MobileCustomDepthDesc, TEXT("MobileCustomDepth"));
		CustomDepthTextures.MobileStencil = GraphBuilder.CreateTexture(MobileCustomStencilDesc, TEXT("MobileCustomStencil"));
	}

	const FRDGTextureDesc CustomDepthDesc = FRDGTextureDesc::Create2D(CustomDepthExtent, PF_DepthStencil, FClearValueBinding::DepthFar, GFastVRamConfig.CustomDepth | TexCreate_NoFastClear | TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);

	CustomDepthTextures.Depth = GraphBuilder.CreateTexture(CustomDepthDesc, TEXT("CustomDepth"));

	CustomDepthTextures.Stencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepthTextures.Depth, PF_X24_G8));
	CustomDepthTextures.DepthAction = ERenderTargetLoadAction::EClear;
	CustomDepthTextures.StencilAction = bWritesCustomStencil ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
	CustomDepthTextures.DownsampleFactor = DownsampleFactor;

	return CustomDepthTextures;
}

BEGIN_SHADER_PARAMETER_STRUCT(FCustomDepthPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static FViewShaderParameters CreateViewShaderParametersWithoutJitter(const FViewInfo& View)
{
	const auto SetupParameters = [](const FViewInfo& View, FViewUniformShaderParameters& Parameters)
	{
		FBox VolumeBounds[TVC_MAX];
		FViewMatrices ModifiedViewMatrices = View.ViewMatrices;
		ModifiedViewMatrices.HackRemoveTemporalAAProjectionJitter();

		Parameters = *View.CachedViewUniformShaderParameters;
		View.SetupUniformBufferParameters(ModifiedViewMatrices, ModifiedViewMatrices, VolumeBounds, TVC_MAX, Parameters);
	};

	FViewUniformShaderParameters ViewUniformParameters;
	SetupParameters(View, ViewUniformParameters);

	FViewShaderParameters Parameters;
	Parameters.View = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformParameters, UniformBuffer_SingleFrame);

	if (const FViewInfo* InstancedView = View.GetInstancedView())
	{
		SetupParameters(*InstancedView, ViewUniformParameters);
	}

	Parameters.InstancedView = TUniformBufferRef<FInstancedViewUniformShaderParameters>::CreateUniformBufferImmediate(
		reinterpret_cast<const FInstancedViewUniformShaderParameters&>(ViewUniformParameters),
		UniformBuffer_SingleFrame);

	return Parameters;
}

bool FSceneRenderer::RenderCustomDepthPass(FRDGBuilder& GraphBuilder, const FCustomDepthTextures& CustomDepthTextures, const FSceneTextureShaderParameters& SceneTextures)
{
	if (!CustomDepthTextures.IsValid())
	{
		return false;
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderCustomDepthPass);
	RDG_GPU_STAT_SCOPE(GraphBuilder, CustomDepth);

	const bool bMobilePath = (FeatureLevel <= ERHIFeatureLevel::ES3_1);
	const bool bWritesCustomStencilValues = IsCustomDepthPassWritingStencil(FeatureLevel);

	bool bCustomDepthRendered = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		FViewInfo& View = Views[ViewIndex];

		if (View.ShouldRenderView() && View.bHasCustomDepthPrimitives)
		{
			View.BeginRenderView();

			FCustomDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FCustomDepthPassParameters>();
			PassParameters->SceneTextures = SceneTextures;

			// User requested jitter-free custom depth.
			if (CVarCustomDepthTemporalAAJitter.GetValueOnRenderThread() == 0 && IsTemporalAccumulationBasedMethod(View.AntiAliasingMethod))
			{
				PassParameters->View = CreateViewShaderParametersWithoutJitter(View);
			}
			else
			{
				PassParameters->View = View.GetShaderParameters();
			}

			const ERenderTargetLoadAction DepthLoadAction = GetLoadActionIfProduced(CustomDepthTextures.Depth, CustomDepthTextures.DepthAction);
			const ERenderTargetLoadAction StencilLoadAction = GetLoadActionIfProduced(CustomDepthTextures.Depth, CustomDepthTextures.StencilAction);

			if (bMobilePath)
			{
				PassParameters->RenderTargets[0] = FRenderTargetBinding(CustomDepthTextures.MobileDepth, DepthLoadAction);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(CustomDepthTextures.MobileStencil, StencilLoadAction);

				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
					CustomDepthTextures.Depth,
					DepthLoadAction,
					DepthLoadAction,
					FExclusiveDepthStencil::DepthWrite_StencilWrite);
			}
			else
			{
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
					CustomDepthTextures.Depth,
					DepthLoadAction,
					StencilLoadAction,
					FExclusiveDepthStencil::DepthWrite_StencilWrite);
			}

			View.ParallelMeshDrawCommandPasses[EMeshPass::CustomDepth].BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CustomDepth"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View, PassParameters, DownsampleFactor = CustomDepthTextures.DownsampleFactor](FRHICommandList& RHICmdList)
			{
				SetStereoViewport(RHICmdList, View, 1.0f / static_cast<float>(DownsampleFactor));
				View.ParallelMeshDrawCommandPasses[EMeshPass::CustomDepth].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});

			bCustomDepthRendered = true;
		}
	}

	return bCustomDepthRendered;
}

class FCustomDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
	FCustomDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	template<bool bPositionOnly, bool bUsesMobileColorValue>
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		float MobileColorValue);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

FCustomDepthPassMeshProcessor::FCustomDepthPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
}

void FCustomDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (PrimitiveSceneProxy->ShouldRenderCustomDepth())
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material)
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool FCustomDepthPassMeshProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	// Determine the mesh's material and blend mode.
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const bool bWriteCustomStencilValues = IsCustomDepthPassWritingStencil(FeatureLevel);
	float MobileColorValue = 0.0f;

	if (bWriteCustomStencilValues)
	{
		const uint32 CustomDepthStencilValue = PrimitiveSceneProxy->GetCustomDepthStencilValue();
		static FRHIDepthStencilState* StencilStates[EStencilMask::SM_Count] =
		{
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 255>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 255>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 1>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 2>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 4>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 8>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 16>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 32>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 64>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Replace, SO_Replace, false, CF_Always, SO_Keep, SO_Keep, SO_Keep, 255, 128>::GetRHI()
		};
		checkSlow(EStencilMask::SM_Count == UE_ARRAY_COUNT(StencilStates));

		PassDrawRenderState.SetDepthStencilState(StencilStates[(int32)PrimitiveSceneProxy->GetStencilWriteMask()]);
		PassDrawRenderState.SetStencilRef(CustomDepthStencilValue);

		if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
		{
			// On mobile platforms write custom stencil value to color target
			MobileColorValue = CustomDepthStencilValue / 255.0f;
		}
	}
	else
	{
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	}

	const bool bUsesMobileColorValue = FeatureLevel <= ERHIFeatureLevel::ES3_1;

	bool bResult = true;
	if (BlendMode == BLEND_Opaque
		&& MeshBatch.VertexFactory->SupportsPositionOnlyStream()
		&& !Material.MaterialModifiesMeshPosition_RenderThread()
		&& Material.WritesEveryPixel()
		&& !bUsesMobileColorValue)
	{
		const FMaterialRenderProxy& DefaultProxy = *UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		const FMaterial& DefaultMaterial = *DefaultProxy.GetMaterialNoFallback(FeatureLevel);
		bResult = Process<true, false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, DefaultProxy, DefaultMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
	}
	else if (!IsTranslucentBlendMode(BlendMode) || Material.IsTranslucencyWritingCustomDepth())
	{
		const bool bMaterialMasked = !Material.WritesEveryPixel() || Material.IsTranslucencyWritingCustomDepth();

		const FMaterialRenderProxy* EffectiveMaterialRenderProxy = &MaterialRenderProxy;
		const FMaterial* EffectiveMaterial = &Material;

		if (!bMaterialMasked && !Material.MaterialModifiesMeshPosition_RenderThread())
		{
			// Override with the default material for opaque materials that are not two sided
			EffectiveMaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			EffectiveMaterial = EffectiveMaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			check(EffectiveMaterial);
		}

		if (bUsesMobileColorValue)
		{
			bResult = Process<false, true>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
		}
		else
		{
			bResult = Process<false, false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode, MobileColorValue);
		}
	}

	return bResult;
}

template<bool bPositionOnly, bool bUsesMobileColorValue>
bool FCustomDepthPassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode,
	float MobileColorValue)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		TDepthOnlyVS<bPositionOnly>,
		FDepthOnlyPS<bUsesMobileColorValue>> DepthPassShaders;

	FShaderPipelineRef ShaderPipeline;
	if (!GetDepthPassShaders<bPositionOnly, bUsesMobileColorValue>(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		DepthPassShaders.VertexShader,
		DepthPassShaders.PixelShader,
		ShaderPipeline
		))
	{
		return false;
	}

	FDepthOnlyShaderElementData ShaderElementData(MobileColorValue);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(DepthPassShaders.VertexShader, DepthPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		DepthPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		bPositionOnly ? EMeshPassFeatures::PositionOnly : EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

FMeshPassProcessor* CreateCustomDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	return new(FMemStack::Get()) FCustomDepthPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Deferred, EMeshPass::CustomDepth, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Mobile, EMeshPass::CustomDepth, EMeshPassFlags::MainView);