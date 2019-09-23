// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SingleLayerWaterRendering.cpp: Water pass rendering implementation.
=============================================================================*/

#include "SingleLayerWaterRendering.h"
#include "BasePassRendering.h"
#include "DeferredShadingRenderer.h"
#include "MeshPassProcessor.inl"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcessTemporalAA.h"
#include "RenderGraph.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "SceneTextureParameters.h"



DECLARE_GPU_STAT(SingleLayerWater);



static TAutoConsoleVariable<int32> CVarWaterSingleLayer(
	TEXT("r.Water.SingleLayer"), 1,
	TEXT("Enable the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarWaterSingleLayerReflection(
	TEXT("r.Water.SingleLayer.Reflection"), 1,
	TEXT("Enable reflection rendering on water."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarWaterSingleLayerTiledComposite(
	TEXT("r.Water.SingleLayer.TiledComposite"), 1,
	TEXT("Enable tiled optimisation of the water reflection rendering."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSR(
	TEXT("r.Water.SingleLayer.SSR"), 1,
	TEXT("Enable SSR for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSRTAA(
	TEXT("r.Water.SingleLayer.SSRTAA"), 1,
	TEXT("Enable SSR denoising using TAA for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);



//////////////////////////////////////////////////////////////////////////



static bool ShouldRenderSingleLayerWater(const FViewInfo& View)
{
	return View.bHasSingleLayerWaterMaterial;
}

bool ShouldRenderSingleLayerWater(const TArray<FViewInfo>& Views, const FEngineShowFlags& EngineShowFlags)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() > 0) // && EngineShowFlags.Water)
	{
		for (const FViewInfo& View : Views)
		{
			if (ShouldRenderSingleLayerWater(View))
			{
				return true;
			}
		}
	}
	return false;
}

bool ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(const TArray<FViewInfo>& Views)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() <= 0)
	{
		for (const FViewInfo& View : Views)
		{
			if (ShouldRenderSingleLayerWater(View))
			{
				return true;
			}
		}
	}
	return false;
}



//////////////////////////////////////////////////////////////////////////



FSingleLayerWaterPassMeshProcessor::FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
	// Not needed for now
	//FRHIBlendState* WaterBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
	//PassDrawRenderState.SetBlendState(WaterBlendState);
}

void FSingleLayerWaterPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	if (Material.GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
	{
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}
}

void FSingleLayerWaterPassMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	FUniformLightMapPolicy NoLightmapPolicy(LMP_NO_LIGHTMAP);
	typedef FUniformLightMapPolicy LightMapPolicyType;
	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> WaterPassShaders;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	const bool bRenderSkylight = true;
	const bool bRenderAtmosphericFog = false;
	GetBasePassShaders<LightMapPolicyType>(
		MaterialResource,
		VertexFactory->GetType(),
		NoLightmapPolicy,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		WaterPassShaders.HullShader,
		WaterPassShaders.DomainShader,
		WaterPassShaders.VertexShader,
		WaterPassShaders.PixelShader
		);

	TBasePassShaderElementData<LightMapPolicyType> ShaderElementData(nullptr);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(WaterPassShaders.VertexShader, WaterPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		WaterPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

FMeshPassProcessor* CreateSingleLayerWaterPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
	DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	// Make sure depth write is enabled.
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess_DepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess | FExclusiveDepthStencil::DepthWrite);
	SetupBasePassState(BasePassDepthStencilAccess_DepthWrite, false, DrawRenderState);

	return new(FMemStack::Get()) FSingleLayerWaterPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterSingleLayerWaterPass(&CreateSingleLayerWaterPassProcessor, EShadingPath::Deferred, EMeshPass::SingleLayerWaterPass, EMeshPassFlags::MainView);



//////////////////////////////////////////////////////////////////////////



BEGIN_SHADER_PARAMETER_STRUCT(FSingleLayerWaterCommonShaderParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceReflectionsTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceReflectionsSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneNoWaterDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneNoWaterDepthSampler)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)	// Water scene texture
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
END_SHADER_PARAMETER_STRUCT()

class FSingleLayerWaterScreenSpaceReflections : SHADER_PERMUTATION_BOOL("SCREEN_SPACE_REFLECTION");

class FSingleLayerWaterCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSingleLayerWaterCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSingleLayerWaterCompositePS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<FSingleLayerWaterScreenSpaceReflections>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
		{
			return false;
		}
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};

IMPLEMENT_GLOBAL_SHADER(FSingleLayerWaterCompositePS, "/Engine/Private/SingleLayerWaterComposite.usf", "SingleLayerWaterCompositePS", SF_Pixel);

class FWaterTileCategorisationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTileCategorisationCS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTileCategorisationCS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<>;

	static int32 GetTileSize()
	{
		return 8;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
		SHADER_PARAMETER(uint32, TiledViewWidth)
		SHADER_PARAMETER(uint32, TiledViewHeight)
		SHADER_PARAMETER(float, TiledViewWidthInv)
		SHADER_PARAMETER(float, TiledViewHeightInv)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchIndirectDataUAV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, WaterTileListDataUAV)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || !RHISupportsDrawIndirect(Parameters.Platform))
		{
			return false;
		}
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_CATERGORISATION_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WATER_TILE_SIZE"), GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};

IMPLEMENT_GLOBAL_SHADER(FWaterTileCategorisationCS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileCatergorisationCS", SF_Compute);

// Disabled water composition due to non 32bits UAV operations
/*class FWaterTiledCompositeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTiledCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTiledCompositeCS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<FSingleLayerWaterScreenSpaceReflections>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
		SHADER_PARAMETER(uint32, TiledViewWidth)
		SHADER_PARAMETER(uint32, TiledViewHeight)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, WaterTileListData)
		SHADER_PARAMETER_UAV(RWTexture2D<float3>, SceneColorUAV)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDispatchParameters)	// Not used in shader but need to be reference in the parameter list
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || !RHISupportsDrawIndirect(Parameters.Platform))
		{
			return false;
		}
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILED_COMPOSITE_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WATER_TILE_SIZE"), FWaterTileCategorisationCS::GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};

IMPLEMENT_GLOBAL_SHADER(FWaterTiledCompositeCS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTiledCategorisationCS", SF_Compute);*/



//////////////////////////////////////////////////////////////////////////



void FDeferredShadingSceneRenderer::CopySingleLayerWaterTextures(FRHICommandList& RHICmdList, FSingleLayerWaterPassData& PassData)
{
	check(RHICmdList.IsOutsideRenderPass());
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	const ERHIFeatureLevel::Type CurrentFeatureLevel = SceneContext.GetCurrentFeatureLevel();

	// Allocate required buffers
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneContext.GetBufferSizeXY(), PF_DepthStencil, SceneContext.GetDefaultDepthClear(), TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead, false));
		Desc.NumSamples = SceneContext.GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		Desc.Flags |= GFastVRamConfig.SceneDepth;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PassData.SceneDepthZWithoutSingleLayerWater, TEXT("SceneDepthZWithoutSingleLayerWater"), true);
	}
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SceneContext.GetBufferSizeXY(), SceneContext.GetSceneColorFormat(), SceneContext.GetDefaultColorClear(), TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.NumSamples = SceneContext.GetNumSceneColorMSAASamples(CurrentFeatureLevel);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, PassData.SceneColorWithoutSingleLayerWater, TEXT("SceneColorWithoutSingleLayerWater"));
	}

	// Copy and save textures without water informations for later composition operations
	{
		FRHICopyTextureInfo DepthCopyInfo;
		RHICmdList.CopyTexture(SceneContext.SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture, PassData.SceneDepthZWithoutSingleLayerWater->GetRenderTargetItem().ShaderResourceTexture, DepthCopyInfo);
		RHICmdList.CopyTexture(SceneContext.GetSceneColorSurface(), PassData.SceneColorWithoutSingleLayerWater->GetRenderTargetItem().ShaderResourceTexture, DepthCopyInfo);
	}
}

void FDeferredShadingSceneRenderer::BeginRenderingWaterGBuffer(FRHICommandList& RHICmdList, FSingleLayerWaterPassData& PassData, FExclusiveDepthStencil::Type DepthStencilAccess, bool bBindQuadOverdrawBuffers)
{
	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingWaterGBuffer);

	check(RHICmdList.IsOutsideRenderPass());
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const ERHIFeatureLevel::Type CurrentFeatureLevel = SceneContext.GetCurrentFeatureLevel();
	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	check(CurrentFeatureLevel >= ERHIFeatureLevel::SM5);

	// Create MRT
	int32 VelocityRTIndex = -1;
	FRHIRenderPassInfo RPInfo;
	SceneContext.FillGBufferRenderPassInfo(ERenderTargetLoadAction::ELoad, RPInfo, VelocityRTIndex);
	// Set a dummy Scene color RT to avoid gbuffer to stomp HDR scene color we want to blend over
	RPInfo.ColorRenderTargets[0].Action = MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction);
	RPInfo.ColorRenderTargets[0].RenderTarget = SceneContext.GetSceneColorSurface();

	// Stencil always has to be store or certain VK drivers will leave the attachment in an undefined state.
	RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore), MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = (const FTexture2DRHIRef&)SceneContext.SceneDepthZ->GetRenderTargetItem().TargetableTexture;
	RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = DepthStencilAccess;

	// Set other UAVs
	const bool bClearQuadOverdrawBuffers = false;
	SceneContext.SetQuadOverdrawUAV(RHICmdList, bBindQuadOverdrawBuffers, bClearQuadOverdrawBuffers, RPInfo);
	if (UseVirtualTexturing(CurrentFeatureLevel) && !bBindQuadOverdrawBuffers)
	{
		SceneContext.BindVirtualTextureFeedbackUAV(RPInfo);
	}

	// Begin the pass
	RHICmdList.BeginRenderPass(RPInfo, TEXT("WaterGBuffer"));

	// Needs to be called after we start a renderpass in order for the color/depth decompress/expand to be executed on the next write-to-read barrier/transition.
	RHICmdList.BindClearMRTValues(true, true, false);
}

void FDeferredShadingSceneRenderer::FinishWaterGBufferPassAndResolve(FRHICommandListImmediate& RHICmdList)
{
	// Same as the GBuffer for now (also same resolves)
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.FinishGBufferPassAndResolve(RHICmdList);
}


void FDeferredShadingSceneRenderer::RenderSingleLayerWaterReflections(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData)
{
	if (CVarWaterSingleLayer.GetValueOnRenderThread() <= 0 || CVarWaterSingleLayerReflection.GetValueOnRenderThread() <= 0)
	{
		return;
	}

	bool AllViewAreForward = true;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		AllViewAreForward &= IsAnyForwardShadingEnabled(Views[ViewIndex].GetShaderPlatform());
	}
	if (AllViewAreForward)
	{
		return; // No SSR or composite needed in Forward for anyview so quick return. Reflections are applied in the WaterGBuffer pass.
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
		{
			continue; // No SSR or composite needed in forward views.
		}

		FRDGTextureRef ReflectionsColor = nullptr;

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		auto SetCommonParameters = [&](FSingleLayerWaterCommonShaderParameters& Parameters)
		{
			Parameters.ScreenSpaceReflectionsTexture = ReflectionsColor ? ReflectionsColor : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
			Parameters.ScreenSpaceReflectionsSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
			Parameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters.SceneNoWaterDepthTexture = GraphBuilder.RegisterExternalTexture(PassData.SceneDepthZWithoutSingleLayerWater ? PassData.SceneDepthZWithoutSingleLayerWater : GSystemTextures.BlackDummy);
			Parameters.SceneNoWaterDepthSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.SceneTextures = SceneTextures;
			SetupSceneTextureSamplers(&Parameters.SceneTextureSamplers);
			Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
			Parameters.ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
			{
				FReflectionUniformParameters ReflectionUniformParameters;
				SetupReflectionUniformParameters(View, ReflectionUniformParameters);
				Parameters.ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
			}
		};

		const bool bRunTiled = RHISupportsDrawIndirect(View.GetShaderPlatform()) && CVarWaterSingleLayerTiledComposite.GetValueOnRenderThread();
		FTiledScreenSpaceReflection TiledScreenSpaceReflection = {nullptr, nullptr, nullptr, nullptr, nullptr, 8};
		FIntVector ViewRes(View.ViewRect.Width(), View.ViewRect.Height(), 1);
		FIntVector TiledViewRes = FIntVector::DivideAndRoundUp(ViewRes, TiledScreenSpaceReflection.TileSize);
		if (bRunTiled)
		{
			TiledScreenSpaceReflection.DispatchIndirectParametersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("WaterIndirectDrawParameters"));
			TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.DispatchIndirectParametersBuffer);
			FRDGBufferDesc TileListStructuredBufferDesc = FRDGBufferDesc::CreateStructuredDesc(4, TiledViewRes.X * TiledViewRes.Y); // one uint32 element per tile
			TiledScreenSpaceReflection.TileListDataBuffer = GraphBuilder.CreateBuffer(TileListStructuredBufferDesc, TEXT("WaterTileList"));
			TiledScreenSpaceReflection.TileListStructureBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.TileListDataBuffer);
			TiledScreenSpaceReflection.TileListStructureBufferSRV = GraphBuilder.CreateSRV(TiledScreenSpaceReflection.TileListDataBuffer);

			// Clear DispatchIndirectParametersBuffer
			AddClearUAVPass(GraphBuilder, TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV, 0);

			// Categorization based on SHADING_MODEL_ID
			{
				FWaterTileCategorisationCS::FPermutationDomain PermutationVector;
				TShaderMapRef<FWaterTileCategorisationCS> ComputeShader(View.ShaderMap, PermutationVector);

				FWaterTileCategorisationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterTileCategorisationCS::FParameters>();
				SetCommonParameters(PassParameters->CommonParameters);
				PassParameters->TiledViewWidth = TiledViewRes.X;
				PassParameters->TiledViewHeight = TiledViewRes.Y;
				PassParameters->TiledViewWidthInv = 1.0f / float(TiledViewRes.X);
				PassParameters->TiledViewHeightInv = 1.0f / float(TiledViewRes.Y);
				PassParameters->DispatchIndirectDataUAV = TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV;
				PassParameters->WaterTileListDataUAV = TiledScreenSpaceReflection.TileListStructureBufferUAV;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("WaterTileCategorisation"), *ComputeShader, PassParameters, TiledViewRes);
			}
		}

		const bool bEnableSSR = CVarWaterSingleLayerSSR.GetValueOnRenderThread() != 0 && ShouldRenderScreenSpaceReflections(View);
		if (bEnableSSR)
		{
			// RUN SSR
			// Uses the water GBuffer (depth, ABCDEF) to know how to start tracing.
			// The water scene depth is used to know where to start tracing.
			// Then it uses the scene HZB for the ray casting process.

			FRDGTextureRef CurrentSceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());

			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfig;
			ESSRQuality SSRQuality;
			GetSSRQualityForView(View, &SSRQuality, &RayTracingConfig);

			RDG_EVENT_SCOPE(GraphBuilder, "Water ScreenSpaceReflections(Quality=%d)", int32(SSRQuality));

			const bool bDenoise = false;
			RenderScreenSpaceReflections(
				GraphBuilder, SceneTextures, CurrentSceneColor, View, SSRQuality, bDenoise, &DenoiserInputs, bRunTiled ? &TiledScreenSpaceReflection : nullptr);

			ReflectionsColor = DenoiserInputs.Color;

			if (CVarWaterSingleLayerSSRTAA.GetValueOnRenderThread() && IsSSRTemporalPassRequired(View)) // TAA pass is an option
			{
				check(View.ViewState);
				FTAAPassParameters TAASettings(View);
				TAASettings.Pass = ETAAPassConfig::ScreenSpaceReflections;
				TAASettings.SceneColorInput = DenoiserInputs.Color;

				FTAAOutputs TAAOutputs = AddTemporalAAPass(
					GraphBuilder,
					SceneTextures, 
					View,
					TAASettings,
					View.PrevViewInfo.SSRHistory,
					&View.ViewState->PrevFrameViewInfo.SSRHistory);

				ReflectionsColor = TAAOutputs.SceneColor;
			}
		}

		// Composite reflections on water
/*		if (bRunTiled)									// Disabled water composition due to non 32bits UAV operations
		{
			// Render Tiled composite CS shader
			{
				FWaterTiledCompositeCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSingleLayerWaterScreenSpaceReflections>(bEnableSSR);
				TShaderMapRef<FWaterTiledCompositeCS> ComputeShader(View.ShaderMap, PermutationVector);

				FWaterTiledCompositeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterTiledCompositeCS::FParameters>();
				SetCommonParameters(PassParameters->CommonParameters);
				PassParameters->TiledViewWidth = TiledViewRes.X;
				PassParameters->TiledViewHeight = TiledViewRes.Y;
				PassParameters->WaterTileListData = TiledScreenSpaceReflection.TileListStructureBufferSRV;
				PassParameters->SceneColorUAV = SceneContext.GetSceneColorTextureUAV();
				PassParameters->IndirectDispatchParameters = TiledScreenSpaceReflection.DispatchIndirectParametersBuffer;
				
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("WaterTiledComposite"), *ComputeShader, PassParameters, TiledScreenSpaceReflection.DispatchIndirectParametersBuffer, 0);
			}
		}
		else*/
		{
			FSingleLayerWaterCompositePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSingleLayerWaterScreenSpaceReflections>(bEnableSSR);
			TShaderMapRef<FSingleLayerWaterCompositePS> PixelShader(View.ShaderMap, PermutationVector);

			FSingleLayerWaterCompositePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSingleLayerWaterCompositePS::FParameters>();
			SetCommonParameters(PassParameters->CommonParameters);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
			ClearUnusedGraphResources(*PixelShader, PassParameters);


			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Water Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader](FRHICommandList& InRHICmdList)
			{
				InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, *PixelShader, GraphicsPSOInit);

				// Premultiplied alpha where alpha is transmittance.
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI(); 

				SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
				SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
				FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
			});
		}
	}

	TRefCountPtr<IPooledRenderTarget> OutSceneColor;
	GraphBuilder.QueueTextureExtraction(SceneColorTexture, &OutSceneColor);		// Should not be needed...

	GraphBuilder.Execute();

	ResolveSceneColor(RHICmdList);
}

bool FDeferredShadingSceneRenderer::RenderSingleLayerWaterPass(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData, FExclusiveDepthStencil::Type WaterPassDepthStencilAccess)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderSingleLayerWaterPass);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderSingleLayerWaterPass, FColor::Emerald);

	bool bDirty = false;

	RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

	{
		SCOPED_DRAW_EVENT(RHICmdList, SingleLayerWater);
		SCOPE_CYCLE_COUNTER(STAT_WaterPassDrawTime);
		SCOPED_GPU_STAT(RHICmdList, SingleLayerWater);

		// Must have an open renderpass before getting here in single threaded mode.
		check(RHICmdList.IsInsideRenderPass());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
			FViewInfo& View = Views[ViewIndex];
			SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

			TUniformBufferRef<FOpaqueBasePassUniformParameters> WaterPassUniformBuffer;
			IPooledRenderTarget* WhiteForwardScreenSpaceShadowMask = GSystemTextures.WhiteDummy;
			CreateOpaqueBasePassUniformBuffer(RHICmdList, View, WhiteForwardScreenSpaceShadowMask, PassData.SceneColorWithoutSingleLayerWater, PassData.SceneDepthZWithoutSingleLayerWater, WaterPassUniformBuffer);

			FMeshPassProcessorRenderState DrawRenderState(View, WaterPassUniformBuffer);
			SetupBasePassState(WaterPassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity, DrawRenderState);

			const bool bShouldRenderView = View.ShouldRenderView();
			if (bShouldRenderView)
			{
				Scene->UniformBuffers.UpdateViewUniformBuffer(View);

				bDirty |= RenderSingleLayerWaterPassView(RHICmdList, View, PassData, DrawRenderState);
			}
		}
	}

	RHICmdList.AutomaticCacheFlushAfterComputeShader(true);
	RHICmdList.FlushComputeShaderCache();

	return bDirty;
}

bool FDeferredShadingSceneRenderer::RenderSingleLayerWaterPassView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FSingleLayerWaterPassData& PassData, const FMeshPassProcessorRenderState& InDrawRenderState)
{
	bool bDirty = false;
	FMeshPassProcessorRenderState DrawRenderState(InDrawRenderState);
	SetupBasePassView(RHICmdList, View, this);

	View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(nullptr, RHICmdList);

	return bDirty;
}


