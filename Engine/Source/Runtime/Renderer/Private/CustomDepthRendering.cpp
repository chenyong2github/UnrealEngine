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

bool IsCustomDepthPassWritingStencil()
{
	return GetCustomDepthMode() == ECustomDepthMode::EnabledWithStencil;
}

FCustomDepthTextures FCustomDepthTextures::Create(FRDGBuilder& GraphBuilder, FIntPoint CustomDepthExtent)
{
	const ECustomDepthMode CustomDepthMode = GetCustomDepthMode();

	if (!IsCustomDepthPassEnabled())
	{
		return {};
	}

	const bool bWritesCustomStencil = IsCustomDepthPassWritingStencil();

	FCustomDepthTextures CustomDepthTextures;

	const FRDGTextureDesc CustomDepthDesc = FRDGTextureDesc::Create2D(CustomDepthExtent, PF_DepthStencil, FClearValueBinding::DepthFar, GFastVRamConfig.CustomDepth | TexCreate_NoFastClear | TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);

	CustomDepthTextures.Depth = GraphBuilder.CreateTexture(CustomDepthDesc, TEXT("CustomDepth"));

	CustomDepthTextures.DepthAction = ERenderTargetLoadAction::EClear;
	CustomDepthTextures.StencilAction = bWritesCustomStencil ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;

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

bool FSceneRenderer::RenderCustomDepthPass(FRDGBuilder& GraphBuilder, FCustomDepthTextures& CustomDepthTextures, const FSceneTextureShaderParameters& SceneTextures)
{
	if (!CustomDepthTextures.IsValid())
	{
		return false;
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderCustomDepthPass);
	RDG_GPU_STAT_SCOPE(GraphBuilder, CustomDepth);

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

			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
				CustomDepthTextures.Depth,
				DepthLoadAction,
				StencilLoadAction,
				FExclusiveDepthStencil::DepthWrite_StencilWrite);

			View.ParallelMeshDrawCommandPasses[EMeshPass::CustomDepth].BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CustomDepth"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View, PassParameters](FRHICommandList& RHICmdList)
			{
				SetStereoViewport(RHICmdList, View, 1.0f);
				View.ParallelMeshDrawCommandPasses[EMeshPass::CustomDepth].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			});

			bCustomDepthRendered = true;
		}
	}

	if (bCustomDepthRendered)
	{
		const FSceneTexturesConfig& Config = FSceneTexturesConfig::Get();
		FRDGTextureRef CustomDepth = CustomDepthTextures.Depth;

		// TextureView is not supported in GLES, so we can't lookup CustomDepth and CustomStencil from a single texture
		// Do a copy of the CustomDepthStencil texture if both CustomDepth and CustomStencil are sampled in a shader.
		if (IsOpenGLPlatform(ShaderPlatform) && Config.bSamplesCustomDepthAndStencil)
		{
			CustomDepth = GraphBuilder.CreateTexture(CustomDepthTextures.Depth->Desc, TEXT("CustomDepthCopy"));
			AddCopyTexturePass(GraphBuilder, CustomDepthTextures.Depth, CustomDepth);
		}

		CustomDepthTextures.Stencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepth, PF_X24_G8));
	}

	return bCustomDepthRendered;
}

class FCustomDepthPassMeshProcessor : public FSceneRenderingAllocatorObject<FCustomDepthPassMeshProcessor>, public FMeshPassProcessor
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

	template<bool bPositionOnly>
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

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
	const bool bWriteCustomStencilValues = IsCustomDepthPassWritingStencil();

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
	}
	else
	{
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	}

	bool bResult = true;
	if (BlendMode == BLEND_Opaque
		&& MeshBatch.VertexFactory->SupportsPositionOnlyStream()
		&& !Material.MaterialModifiesMeshPosition_RenderThread()
		&& Material.WritesEveryPixel())
	{
		const FMaterialRenderProxy& DefaultProxy = *UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		const FMaterial& DefaultMaterial = *DefaultProxy.GetMaterialNoFallback(FeatureLevel);
		bResult = Process<true>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, DefaultProxy, DefaultMaterial, MeshFillMode, MeshCullMode);
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

		bResult = Process<false>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *EffectiveMaterialRenderProxy, *EffectiveMaterial, MeshFillMode, MeshCullMode);
	}

	return bResult;
}

template<bool bPositionOnly>
bool FCustomDepthPassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		TDepthOnlyVS<bPositionOnly>,
		FDepthOnlyPS> DepthPassShaders;

	FShaderPipelineRef ShaderPipeline;
	if (!GetDepthPassShaders<bPositionOnly>(
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

	FMeshMaterialShaderElementData ShaderElementData;
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
	return new FCustomDepthPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Deferred, EMeshPass::CustomDepth, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileCustomDepthPass(&CreateCustomDepthPassProcessor, EShadingPath::Mobile, EMeshPass::CustomDepth, EMeshPassFlags::MainView);
