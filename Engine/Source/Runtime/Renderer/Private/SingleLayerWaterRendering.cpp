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

int32 GSingleLayerWaterRefractionDownsampleFactor = 2;
FAutoConsoleVariableRef CVarWaterSingleLayerRefractionDownsampleFactor(
	TEXT("r.Water.SingleLayer.RefractionDownsampleFactor"),
	GSingleLayerWaterRefractionDownsampleFactor,
	TEXT("Resolution divider for the water refraction buffer."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GSingleLayerWaterRefractionFullPrecision = 0;
FAutoConsoleVariableRef CVarWaterSingleLayerRefractionFullPrecision(
	TEXT("r.Water.SingleLayer.RefractionFullPrecision"),
	GSingleLayerWaterRefractionFullPrecision,
	TEXT("Whether to pack refraction depth in a Float32 (instead of Float16). To be used as a debug option to find issues with refraction depth precision."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSR(
	TEXT("r.Water.SingleLayer.SSR"), 1,
	TEXT("Enable SSR for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarWaterSingleLayerSSRTAA(
	TEXT("r.Water.SingleLayer.SSRTAA"), 1,
	TEXT("Enable SSR denoising using TAA for the single water renderring system."),
	ECVF_RenderThreadSafe | ECVF_Scalability);


static TAutoConsoleVariable<int32> CVarRHICmdSingleLayerWaterDeferredContexts(
	TEXT("r.RHICmdSingleLayerWaterDeferredContexts"),
	1,
	TEXT("True to use deferred contexts to parallelize single layer water command list execution."));


static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksSingleLayerWater(
	TEXT("r.RHICmdFlushRenderThreadTasksSingleLayerWater"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of Single layer water. A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksSingleLayerWater is > 0 we will flush."));




//////////////////////////////////////////////////////////////////////////



static bool ShouldRenderSingleLayerWater(const FViewInfo& View)
{
	return View.bHasSingleLayerWaterMaterial;
}

// This is to have switch use the simple single layer water shading similar to mobile: no dynamic lights, only sun and sky, no distortion, no colored transmittance on background, no custom depth read.
bool SingleLayerWaterUsesSimpleShading(EShaderPlatform ShaderPlatform)
{
	return  IsSwitchPlatform(ShaderPlatform) && IsForwardShadingEnabled(ShaderPlatform);
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

bool UseSingleLayerWaterIndirectDraw(EShaderPlatform ShaderPlatform)
{
	return IsFeatureLevelSupported(ShaderPlatform, ERHIFeatureLevel::SM5)
		&& !IsSwitchPlatform(ShaderPlatform) && !IsVulkanPlatform(ShaderPlatform) && !IsMetalPlatform(ShaderPlatform); // Switch does not use tiling, Vulkan gives error with WaterTileCatergorisationCS usage of atomic, and Metal does not play nice, either.
}



//////////////////////////////////////////////////////////////////////////



FSingleLayerWaterPassMeshProcessor::FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
	const bool bSingleLayerWaterUsesSimpleShading = SingleLayerWaterUsesSimpleShading(Scene->GetShaderPlatform());
	if (bSingleLayerWaterUsesSimpleShading)
	{
		// Force non opaque, pre multiplied alpha, transparent blend mode because water is going to be blended against scene color (no distortion from texture scene color).
		FRHIBlendState* ForwardSimpleWaterBlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		PassDrawRenderState.SetBlendState(ForwardSimpleWaterBlendState);
	}
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
	SHADER_PARAMETER(FVector2D, SceneNoWaterMaxUV)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)	// Water scene texture
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCaptureData)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionsParameters)
END_SHADER_PARAMETER_STRUCT()

class FSingleLayerWaterCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSingleLayerWaterCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSingleLayerWaterCompositePS, FGlobalShader)

	class FScreenSpaceReflections : SHADER_PERMUTATION_BOOL("SCREEN_SPACE_REFLECTION");
	class FHasBoxCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_BOX_CAPTURES");
	class FHasSphereCaptures : SHADER_PERMUTATION_BOOL("REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES");
	using FPermutationDomain = TShaderPermutationDomain<FScreenSpaceReflections, FHasBoxCaptures, FHasSphereCaptures>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCommonShaderParameters, CommonParameters)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
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
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchIndirectDataUAV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, WaterTileListDataUAV)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_CATERGORISATION_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

};

IMPLEMENT_GLOBAL_SHADER(FWaterTileCategorisationCS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileCatergorisationCS", SF_Compute);

class FWaterTileVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterTileVS);
	SHADER_USE_PARAMETER_STRUCT(FWaterTileVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileListData)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_VERTEX_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), FWaterTileCategorisationCS::GetTileSize());
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterTileVS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileVS", SF_Vertex);

class FWaterRefractionCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWaterRefractionCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FWaterRefractionCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorCopyDownsampleSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthCopyDownsampleTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthCopyDownsampleSampler)
		SHADER_PARAMETER(FVector2D, SVPositionToSourceTextureUV)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FDownsampleRefraction : SHADER_PERMUTATION_BOOL("DOWNSAMPLE_REFRACTION");
	class FDownsampleColor : SHADER_PERMUTATION_BOOL("DOWNSAMPLE_COLOR");

	using FPermutationDomain = TShaderPermutationDomain<FDownsampleRefraction, FDownsampleColor>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterRefractionCopyPS, "/Engine/Private/SingleLayerWaterComposite.usf", "WaterRefractionCopyPS", SF_Pixel);



//////////////////////////////////////////////////////////////////////////

DECLARE_CYCLE_STAT(TEXT("WaterSingleLayer"), STAT_CLP_WaterSingleLayerPass, STATGROUP_ParallelCommandListMarkers); 

class FWaterSingleLayerPassParallelCommandListSet : public FParallelCommandListSet
{
public:
	FExclusiveDepthStencil::Type PassDepthStencilAccess;

	FWaterSingleLayerPassParallelCommandListSet(
		const FViewInfo& InView,
		FRHICommandListImmediate& InParentCmdList,
		bool bInParallelExecute,
		bool bInCreateSceneContext,
		const FSceneRenderer* InSceneRenderer,
		FExclusiveDepthStencil::Type InPassDepthStencilAccess,
		const FMeshPassProcessorRenderState& InDrawRenderState)
		: FParallelCommandListSet(GET_STATID(STAT_CLP_WaterSingleLayerPass), InView, InSceneRenderer, InParentCmdList, bInParallelExecute, bInCreateSceneContext, InDrawRenderState)
		, PassDepthStencilAccess(InPassDepthStencilAccess)
	{
	}

	virtual ~FWaterSingleLayerPassParallelCommandListSet()
	{
		Dispatch();
	}

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) override
	{
		FParallelCommandListSet::SetStateOnCommandList(CmdList);

		FDeferredShadingSceneRenderer::BeginRenderingWaterGBuffer(CmdList, PassDepthStencilAccess, SceneRenderer->ViewFamily.EngineShowFlags.ShaderComplexity, SceneRenderer->ShaderPlatform);
		SetupBasePassView(CmdList, View, SceneRenderer);
	}
};

//////////////////////////////////////////////////////////////////////////




void FDeferredShadingSceneRenderer::CopySingleLayerWaterTextures(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData)
{
	const bool bSingleLayerWaterUsesSimpleShading = SingleLayerWaterUsesSimpleShading(Scene->GetShaderPlatform());
	bool bCopyColor = !bSingleLayerWaterUsesSimpleShading;

	check(RHICmdList.IsOutsideRenderPass());
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	const ERHIFeatureLevel::Type CurrentFeatureLevel = SceneContext.GetCurrentFeatureLevel();
	const int32 RefractionDownsampleFactor = FMath::Clamp(GSingleLayerWaterRefractionDownsampleFactor, 1, 8);
	const FIntPoint RefractionResolution = FIntPoint::DivideAndRoundDown(SceneContext.GetBufferSizeXY(), RefractionDownsampleFactor);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneColorWithoutSingleLayerWaterTexture = nullptr;

	if (bCopyColor)
	{
		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2DDesc(RefractionResolution, SceneContext.GetSceneColorFormat(), SceneContext.GetDefaultColorClear(), TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		SceneColorWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(ColorDesc, TEXT("SceneColorWithoutSingleLayerWater"));
	}

	const FRDGTextureDesc DepthDesc(FRDGTextureDesc::Create2DDesc(RefractionResolution, GSingleLayerWaterRefractionFullPrecision ? PF_R32_FLOAT : PF_R16F, SceneContext.GetDefaultColorClear(), TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false));
	FRDGTextureRef SceneDepthWithoutSingleLayerWaterTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("SceneDepthWithoutSingleLayerWater"));

	// For now support only the 1st view
	const FViewInfo& View = Views[0];

	FWaterRefractionCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterRefractionCopyPS::FParameters>();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->SceneColorCopyDownsampleTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());
	PassParameters->SceneColorCopyDownsampleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->SceneDepthCopyDownsampleTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ);
	PassParameters->SceneDepthCopyDownsampleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->SVPositionToSourceTextureUV = FVector2D(RefractionDownsampleFactor / float(SceneContext.GetBufferSizeXY().X), RefractionDownsampleFactor / float(SceneContext.GetBufferSizeXY().Y));

	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneDepthWithoutSingleLayerWaterTexture, ERenderTargetLoadAction::ENoAction);

	if (bCopyColor)
	{
		PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneColorWithoutSingleLayerWaterTexture, ERenderTargetLoadAction::ENoAction);
	}

	FWaterRefractionCopyPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FWaterRefractionCopyPS::FDownsampleRefraction>(RefractionDownsampleFactor > 1);
	PermutationVector.Set<FWaterRefractionCopyPS::FDownsampleColor>(bCopyColor);
	auto PixelShader = View.ShaderMap->GetShader<FWaterRefractionCopyPS>(PermutationVector);

	const FIntRect RefractionViewRect = FIntRect(FIntPoint(0, 0), FIntPoint::DivideAndRoundDown(View.ViewRect.Size(), RefractionDownsampleFactor));

	PassData.SceneWithoutSingleLayerWaterMaxUV.X = (RefractionViewRect.Max.X - 0.5f) / RefractionResolution.X;
	PassData.SceneWithoutSingleLayerWaterMaxUV.Y = (RefractionViewRect.Max.Y - 0.5f) / RefractionResolution.Y;

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("Water Refraction Copy"),
		PixelShader,
		PassParameters,
		RefractionViewRect);

	GraphBuilder.QueueTextureExtraction(SceneDepthWithoutSingleLayerWaterTexture, &PassData.SceneDepthWithoutSingleLayerWater, true);

	if (bCopyColor)
	{
		GraphBuilder.QueueTextureExtraction(SceneColorWithoutSingleLayerWaterTexture, &PassData.SceneColorWithoutSingleLayerWater, true);
	}

	GraphBuilder.Execute();
}

void FDeferredShadingSceneRenderer::BeginRenderingWaterGBuffer(FRHICommandList& RHICmdList, FExclusiveDepthStencil::Type DepthStencilAccess, bool bBindQuadOverdrawBuffers, EShaderPlatform InShaderPlatform)
{
	SCOPED_DRAW_EVENT(RHICmdList, BeginRenderingWaterGBuffer);

	check(RHICmdList.IsOutsideRenderPass());
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const ERHIFeatureLevel::Type CurrentFeatureLevel = SceneContext.GetCurrentFeatureLevel();
	const bool bUseGBuffer = IsUsingGBuffers(InShaderPlatform);
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

	// Make the render targets writable
	FRHITexture* TransitionRTs[MaxSimultaneousRenderTargets];
	int32 NumColorRenderTargets = 0;
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		if (RPInfo.ColorRenderTargets[Index].RenderTarget)
		{
			TransitionRTs[NumColorRenderTargets++] = RPInfo.ColorRenderTargets[Index].RenderTarget;
		}
	}

	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, TransitionRTs, NumColorRenderTargets);

	// Begin the pass
	RHICmdList.BeginRenderPass(RPInfo, TEXT("WaterGBuffer"));

	// Needs to be called after we start a renderpass in order for the color/depth decompress/expand to be executed on the next write-to-read barrier/transition.
	RHICmdList.BindClearMRTValues(true, true, false);
}

void FDeferredShadingSceneRenderer::FinishWaterGBufferPassAndResolve(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type DepthStencilAccess)
{
	// Same as the GBuffer for now (also same resolves)
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.FinishGBufferPassAndResolve(RHICmdList, DepthStencilAccess);
}

BEGIN_SHADER_PARAMETER_STRUCT(FWaterCompositeParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FWaterTileVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSingleLayerWaterCompositePS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

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
			Parameters.SceneNoWaterDepthTexture = GraphBuilder.RegisterExternalTexture(PassData.SceneDepthWithoutSingleLayerWater ? PassData.SceneDepthWithoutSingleLayerWater : GSystemTextures.BlackDummy);
			Parameters.SceneNoWaterDepthSampler = TStaticSamplerState<SF_Point>::GetRHI();
			Parameters.SceneNoWaterMaxUV = PassData.SceneWithoutSingleLayerWaterMaxUV;
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

		const bool bRunTiled = UseSingleLayerWaterIndirectDraw(View.GetShaderPlatform()) && CVarWaterSingleLayerTiledComposite.GetValueOnRenderThread();
		FTiledScreenSpaceReflection TiledScreenSpaceReflection = {nullptr, nullptr, nullptr, nullptr, nullptr, 8};
		FIntVector ViewRes(View.ViewRect.Width(), View.ViewRect.Height(), 1);
		FIntVector TiledViewRes = FIntVector::DivideAndRoundUp(ViewRes, TiledScreenSpaceReflection.TileSize);
		if (bRunTiled)
		{
			TiledScreenSpaceReflection.DispatchIndirectParametersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("WaterIndirectDrawParameters"));
			TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.DispatchIndirectParametersBuffer);
			TiledScreenSpaceReflection.TileListDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TiledViewRes.X * TiledViewRes.Y), TEXT("TileListDataBuffer"));
			TiledScreenSpaceReflection.TileListStructureBufferUAV = GraphBuilder.CreateUAV(TiledScreenSpaceReflection.TileListDataBuffer, PF_R32_UINT);
			TiledScreenSpaceReflection.TileListStructureBufferSRV = GraphBuilder.CreateSRV(TiledScreenSpaceReflection.TileListDataBuffer, PF_R32_UINT);

			// Clear DispatchIndirectParametersBuffer
			AddClearUAVPass(GraphBuilder, TiledScreenSpaceReflection.DispatchIndirectParametersBufferUAV, 0);

			// Categorization based on SHADING_MODEL_ID
			{
				FWaterTileCategorisationCS::FPermutationDomain PermutationVector;
				TShaderMapRef<FWaterTileCategorisationCS> ComputeShader(View.ShaderMap, PermutationVector);

				FWaterTileCategorisationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterTileCategorisationCS::FParameters>();
				SetCommonParameters(PassParameters->CommonParameters);
				PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;
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
		{
			const bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
			const bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);

			FSingleLayerWaterCompositePS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSingleLayerWaterCompositePS::FScreenSpaceReflections>(bEnableSSR);
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasBoxCaptures>(bHasBoxCaptures);
			PermutationVector.Set<FSingleLayerWaterCompositePS::FHasSphereCaptures>(bHasSphereCaptures);
			TShaderMapRef<FSingleLayerWaterCompositePS> PixelShader(View.ShaderMap, PermutationVector);

			FWaterCompositeParameters* PassParameters = GraphBuilder.AllocParameters<FWaterCompositeParameters>();

			PassParameters->VS.ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->VS.TileListData = TiledScreenSpaceReflection.TileListStructureBufferSRV;

			SetCommonParameters(PassParameters->PS.CommonParameters);

			PassParameters->IndirectDrawParameter = TiledScreenSpaceReflection.DispatchIndirectParametersBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

			ValidateShaderParameters(*PixelShader, PassParameters->PS);
			ClearUnusedGraphResources(*PixelShader, &PassParameters->PS);

			if (bRunTiled)
			{
				FWaterTileVS::FPermutationDomain VsPermutationVector;
				TShaderMapRef<FWaterTileVS> VertexShader(View.ShaderMap, VsPermutationVector);
				ValidateShaderParameters(*VertexShader, PassParameters->VS);
				ClearUnusedGraphResources(*VertexShader, &PassParameters->VS);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Water Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, &View, TiledScreenSpaceReflection, VertexShader, PixelShader, bRunTiled](FRHICommandList& InRHICmdList)
				{
					InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader->GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader->GetPixelShader();
					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

					SetShaderParameters(InRHICmdList, *VertexShader, VertexShader->GetVertexShader(), PassParameters->VS);
					SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), PassParameters->PS);

					InRHICmdList.DrawPrimitiveIndirect(PassParameters->IndirectDrawParameter->GetIndirectRHICallBuffer(), 0);
				});
			}
			else
			{
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Water Composite %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, &View, TiledScreenSpaceReflection, PixelShader, bRunTiled](FRHICommandList& InRHICmdList)
				{
					InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, *PixelShader, GraphicsPSOInit);

					// Premultiplied alpha where alpha is transmittance.
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

					SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
					SetShaderParameters(InRHICmdList, *PixelShader, PixelShader->GetPixelShader(), PassParameters->PS);
					FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
				});
			}
		}
	}

	TRefCountPtr<IPooledRenderTarget> OutSceneColor;
	GraphBuilder.QueueTextureExtraction(SceneColorTexture, &OutSceneColor);		// Should not be needed...

	GraphBuilder.Execute();

	ResolveSceneColor(RHICmdList);
}

bool FDeferredShadingSceneRenderer::RenderSingleLayerWaterPass(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData, FExclusiveDepthStencil::Type WaterPassDepthStencilAccess, bool bParallel)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(WaterPassRenderSingleLayer);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderSingleLayerWaterPass, FColor::Emerald);

	bool bDirty = false;

	RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

	{
		SCOPED_DRAW_EVENT(RHICmdList, SingleLayerWater);
		SCOPE_CYCLE_COUNTER(STAT_WaterPassDrawTime);
		SCOPED_GPU_STAT(RHICmdList, SingleLayerWater);

		if (!bParallel)
		{
			// Must have an open renderpass before getting here in single threaded mode.
			check(RHICmdList.IsInsideRenderPass());
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);
			FViewInfo& View = Views[ViewIndex];
			SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

			TUniformBufferRef<FOpaqueBasePassUniformParameters> WaterPassUniformBuffer;
			IPooledRenderTarget* WhiteForwardScreenSpaceShadowMask = GSystemTextures.WhiteDummy;
			CreateOpaqueBasePassUniformBuffer(RHICmdList, 
				View, 
				WhiteForwardScreenSpaceShadowMask, 
				&PassData.SceneWithoutSingleLayerWaterMaxUV,
				PassData.SceneColorWithoutSingleLayerWater.IsValid() ? PassData.SceneColorWithoutSingleLayerWater : GSystemTextures.BlackDummy,
				PassData.SceneDepthWithoutSingleLayerWater,
				WaterPassUniformBuffer);

			FMeshPassProcessorRenderState DrawRenderState(View, WaterPassUniformBuffer);
			SetupBasePassState(WaterPassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity, DrawRenderState);

			const bool bShouldRenderView = View.ShouldRenderView();
			if (bShouldRenderView)
			{
				Scene->UniformBuffers.UpdateViewUniformBuffer(View);

				bDirty |= RenderSingleLayerWaterPassView(RHICmdList, View, PassData, DrawRenderState, bParallel);
			}
		}
	}

	RHICmdList.AutomaticCacheFlushAfterComputeShader(true);
	RHICmdList.FlushComputeShaderCache();

	return bDirty;
}

bool FDeferredShadingSceneRenderer::RenderSingleLayerWaterPassView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FSingleLayerWaterPassData& PassData, const FMeshPassProcessorRenderState& InDrawRenderState, bool bParallel)
{
	if (!bParallel)
	{
	    SetupBasePassView(RHICmdList, View, this);
		View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(nullptr, RHICmdList);
	}
	else
	{
		FWaterSingleLayerPassParallelCommandListSet ParallelSet
		(
			View,
			RHICmdList,
			CVarRHICmdSingleLayerWaterDeferredContexts.GetValueOnRenderThread() > 0,
			CVarRHICmdFlushRenderThreadTasksSingleLayerWater.GetValueOnRenderThread() == 0 && CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() == 0,
			this,
			FExclusiveDepthStencil::DepthWrite_StencilWrite,
			InDrawRenderState
		);

		View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].DispatchDraw(&ParallelSet, RHICmdList);
	}

	return View.ParallelMeshDrawCommandPasses[EMeshPass::SingleLayerWaterPass].HasAnyDraw();
}
