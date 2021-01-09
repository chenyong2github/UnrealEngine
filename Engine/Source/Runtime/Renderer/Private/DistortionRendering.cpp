// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistortionRendering.h"
#include "RHIStaticStates.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "MeshMaterialShader.h"
#include "DeferredShadingRenderer.h"
#include "TranslucentRendering.h"
#include "Materials/Material.h"
#include "PipelineStateCache.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"
#include "MeshPassProcessor.inl"

DECLARE_GPU_STAT(Distortion);

const uint8 kStencilMaskBit = STENCIL_SANDBOX_MASK;

static TAutoConsoleVariable<int32> CVarDisableDistortion(
	TEXT("r.DisableDistortion"),
	0,
	TEXT("Prevents distortion effects from rendering.  Saves a full-screen framebuffer's worth of memory."),
	ECVF_Default);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FDistortionPassUniformParameters, "DistortionPass", SceneTextures);

int32 FSceneRenderer::GetRefractionQuality(const FSceneViewFamily& ViewFamily)
{
	static const auto ICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.RefractionQuality"));

	int32 Value = 0;

	if (ViewFamily.EngineShowFlags.Refraction)
	{
		Value = ICVar->GetValueOnRenderThread();
	}

	return Value;
}

void SetupDistortionParams(FVector4& DistortionParams, const FViewInfo& View)
{
	float Ratio = View.UnscaledViewRect.Width() / (float)View.UnscaledViewRect.Height();
	DistortionParams.X = View.ViewMatrices.GetProjectionMatrix().M[0][0];
	DistortionParams.Y = Ratio;
	DistortionParams.Z = (float)View.UnscaledViewRect.Width();
	DistortionParams.W = (float)View.UnscaledViewRect.Height();

	// When ISR is enabled we store two FOVs in the distortion parameters and compute the aspect ratio in the shader instead.
	if ((View.IsInstancedStereoPass() || View.bIsMobileMultiViewEnabled) && View.Family->Views.Num() > 0)
	{
		// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
		const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

		const FViewInfo& InstancedView = static_cast<const FViewInfo&>(View.Family->GetStereoEyeView(StereoPassIndex));
		DistortionParams.Y = InstancedView.ViewMatrices.GetProjectionMatrix().M[0][0];
	}
}

TRDGUniformBufferRef<FDistortionPassUniformParameters> CreateDistortionPassUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	auto* Parameters = GraphBuilder.AllocParameters<FDistortionPassUniformParameters>();
	SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::All, Parameters->SceneTextures);
	SetupDistortionParams(Parameters->DistortionParams, View);
	return GraphBuilder.CreateUniformBuffer(Parameters);
}

class FDistortionScreenPS : public FGlobalShader
{
public:
	class FUseMSAADim : SHADER_PERMUTATION_BOOL("USE_MSAA");
	using FPermutationDomain = TShaderPermutationDomain<FUseMSAADim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DMS<float4>, DistortionMSAATexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DMS<float4>, SceneColorMSAATexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DistortionTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DistortionTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		return !PermutationVector.Get<FUseMSAADim>() || IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FDistortionScreenPS() = default;
	FDistortionScreenPS(const FGlobalShaderType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

/** A pixel shader for rendering the full screen refraction pass */
class FDistortionApplyScreenPS : public FDistortionScreenPS
{
public:
	DECLARE_GLOBAL_SHADER(FDistortionApplyScreenPS);
	SHADER_USE_PARAMETER_STRUCT(FDistortionApplyScreenPS, FDistortionScreenPS);
};

IMPLEMENT_GLOBAL_SHADER(FDistortionApplyScreenPS, "/Engine/Private/DistortApplyScreenPS.usf", "Main", SF_Pixel);

/** A pixel shader that applies the distorted image to the scene */
class FDistortionMergeScreenPS : public FDistortionScreenPS
{
public:
	DECLARE_GLOBAL_SHADER(FDistortionMergeScreenPS);
	SHADER_USE_PARAMETER_STRUCT(FDistortionMergeScreenPS, FDistortionScreenPS);
};

IMPLEMENT_GLOBAL_SHADER(FDistortionMergeScreenPS, "/Engine/Private/DistortApplyScreenPS.usf", "Merge", SF_Pixel);

class FDistortionMeshVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FDistortionMeshVS,MeshMaterial);

	FDistortionMeshVS() = default;

	FDistortionMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsDistorted;
	}
};

class FDistortionMeshHS : public FBaseHS
{
public:
	DECLARE_SHADER_TYPE(FDistortionMeshHS,MeshMaterial);

	FDistortionMeshHS() = default;

	FDistortionMeshHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseHS(Initializer)
	{}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters)
			&& IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsDistorted;
	}
};

class FDistortionMeshDS : public FBaseDS
{
public:
	DECLARE_SHADER_TYPE(FDistortionMeshDS,MeshMaterial);

	FDistortionMeshDS() = default;

	FDistortionMeshDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseDS(Initializer)
	{}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsDistorted;
	}
};

class FDistortionMeshPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FDistortionMeshPS,MeshMaterial);

	FDistortionMeshPS() = default;

	FDistortionMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsDistorted;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		if (IsMobilePlatform(Parameters.Platform))
		{
			// use same path for scene textures as post-process material
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_MOBILE"), 1);
		}
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDistortionMeshVS, TEXT("/Engine/Private/DistortAccumulateVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDistortionMeshHS, TEXT("/Engine/Private/DistortAccumulateVS.usf"), TEXT("MainHull"), SF_Hull);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDistortionMeshDS, TEXT("/Engine/Private/DistortAccumulateVS.usf"), TEXT("MainDomain"), SF_Domain);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDistortionMeshPS,TEXT("/Engine/Private/DistortAccumulatePS.usf"),TEXT("Main"),SF_Pixel);

bool FDeferredShadingSceneRenderer::ShouldRenderDistortion() const
{
	static const auto DisableDistortionCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DisableDistortion"));
	const bool bAllowDistortion = DisableDistortionCVar->GetValueOnAnyThread() != 1;

	if (GetRefractionQuality(ViewFamily) <= 0 || !bAllowDistortion)
	{
		return false;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.bHasDistortionPrimitives && View.ShouldRenderView() && View.ParallelMeshDrawCommandPasses[EMeshPass::Distortion].HasAnyDraw())
		{
			return true;
		}
	}
	return false;
}

BEGIN_SHADER_PARAMETER_STRUCT(FDistortionPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDistortionPassUniformParameters, Pass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderDistortion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture)
{
	check(SceneDepthTexture);
	check(SceneColorTexture);

	if (!ShouldRenderDistortion())
	{
		return;
	}

	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderer_RenderDistortion);
	RDG_EVENT_SCOPE(GraphBuilder, "Distortion");
	RDG_GPU_STAT_SCOPE(GraphBuilder, Distortion);

	const FDepthStencilBinding StencilReadBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilRead);
	FDepthStencilBinding StencilWriteBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthRead_StencilWrite);

	FRDGTextureRef DistortionTexture = nullptr;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// Use stencil mask to optimize cases with lower screen coverage.
	// Note: This adds an extra pass which is actually slower as distortion tends towards full-screen.
	//       It could be worth testing object screen bounds then reverting to a target flip and single pass.

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderer_RenderDistortion_Accumulate);
		RDG_EVENT_SCOPE(GraphBuilder, "Accumulate");

		// Use RGBA8 light target for accumulating distortion offsets.
		// R = positive X offset
		// G = positive Y offset
		// B = negative X offset
		// A = negative Y offset

		DistortionTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				SceneDepthTexture->Desc.Extent,
				PF_B8G8R8A8,
				FClearValueBinding::Transparent,
				GFastVRamConfig.Distortion | TexCreate_RenderTargetable | TexCreate_ShaderResource,
				1,
				SceneDepthTexture->Desc.NumSamples),
			TEXT("Distortion"));

		ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			const ETranslucencyView TranslucencyView = GetTranslucencyView(View);

			if (!View.ShouldRenderView() && !EnumHasAnyFlags(TranslucencyView, ETranslucencyView::RayTracing))
			{
				continue;
			}

			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			auto* PassParameters = GraphBuilder.AllocParameters<FDistortionPassParameters>();
			PassParameters->Pass = CreateDistortionPassUniformBuffer(GraphBuilder, View);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(DistortionTexture, LoadAction);
			PassParameters->RenderTargets.DepthStencil = StencilWriteBinding;

			GraphBuilder.AddPass(
				{},
				PassParameters,
				ERDGPassFlags::Raster,
				[this, &View](FRHICommandListImmediate& RHICmdList)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRender_RenderDistortion_Accumulate_Meshes);
				Scene->UniformBuffers.UpdateViewUniformBuffer(View);

				SetStereoViewport(RHICmdList, View);
				View.ParallelMeshDrawCommandPasses[EMeshPass::Distortion].DispatchDraw(nullptr, RHICmdList);
			});

			LoadAction = ERenderTargetLoadAction::ELoad;
			StencilWriteBinding.SetStencilLoadAction(ERenderTargetLoadAction::ELoad);
		}
		StencilWriteBinding.SetStencilLoadAction(ERenderTargetLoadAction::ELoad);
	}

	FRDGTextureDesc DistortedSceneColorDesc = SceneColorTexture->Desc;
	EnumRemoveFlags(DistortedSceneColorDesc.Flags, TexCreate_FastVRAM | TexCreate_Transient);

	FRDGTextureRef DistortionSceneColorTexture = GraphBuilder.CreateTexture(DistortedSceneColorDesc, TEXT("DistortedSceneColor"));

	FDistortionScreenPS::FParameters CommonParameters;
	CommonParameters.DistortionMSAATexture = DistortionTexture;
	CommonParameters.DistortionTexture = DistortionTexture;
	CommonParameters.SceneColorTextureSampler = TStaticSamplerState<>::GetRHI();
	CommonParameters.DistortionTextureSampler = TStaticSamplerState<>::GetRHI();

	FDistortionScreenPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDistortionScreenPS::FUseMSAADim>(SceneColorTexture->Desc.NumSamples > 1);

	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FDistortionApplyScreenPS> ApplyPixelShader(ShaderMap, PermutationVector);
	TShaderMapRef<FDistortionMergeScreenPS> MergePixelShader(ShaderMap, PermutationVector);

	FScreenPassPipelineState PipelineState(VertexShader, {});
	FScreenPassTextureViewport Viewport(SceneColorTexture);

	// Apply distortion and store off-screen.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderer_RenderDistortion_Apply);
		RDG_EVENT_SCOPE(GraphBuilder, "Apply");
		CommonParameters.SceneColorMSAATexture = SceneColorTexture;
		CommonParameters.SceneColorTexture = SceneColorTexture;
		CommonParameters.RenderTargets.DepthStencil = StencilReadBinding;
		PipelineState.PixelShader = ApplyPixelShader;

		// Test against stencil mask but don't clear.
		PipelineState.DepthStencilState = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			kStencilMaskBit, kStencilMaskBit>::GetRHI();

		ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;

		for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			auto* PassParameters = GraphBuilder.AllocParameters<FDistortionScreenPS::FParameters>();
			*PassParameters = CommonParameters;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(DistortionSceneColorTexture, LoadAction);

			Viewport.Rect = View.ViewRect;

			ClearUnusedGraphResources(ApplyPixelShader, PassParameters);
			AddDrawScreenPass(GraphBuilder, {}, View, Viewport, Viewport, PipelineState, PassParameters,
				[ApplyPixelShader, PassParameters](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetStencilRef(kStencilMaskBit);
				SetShaderParameters(RHICmdList, ApplyPixelShader, ApplyPixelShader.GetPixelShader(), *PassParameters);
			});

			LoadAction = ERenderTargetLoadAction::ELoad;
		}
	}

	// Merge distortion back to scene color.
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderer_RenderDistortion_Merge);
		RDG_EVENT_SCOPE(GraphBuilder, "Merge");
		CommonParameters.SceneColorMSAATexture = DistortionSceneColorTexture;
		CommonParameters.SceneColorTexture = DistortionSceneColorTexture;
		CommonParameters.RenderTargets.DepthStencil = StencilWriteBinding;
		PipelineState.PixelShader = MergePixelShader;

		// Test against stencil mask and clear it.
		PipelineState.DepthStencilState = TStaticDepthStencilState<
			false, CF_Always,
			true, CF_Equal, SO_Keep, SO_Keep, SO_Zero,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			kStencilMaskBit, kStencilMaskBit>::GetRHI();

		for (int32 ViewIndex = 0, Num = Views.Num(); ViewIndex < Num; ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

			auto* PassParameters = GraphBuilder.AllocParameters<FDistortionScreenPS::FParameters>();
			*PassParameters = CommonParameters;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

			Viewport.Rect = View.ViewRect;

			ClearUnusedGraphResources(MergePixelShader, PassParameters);
			AddDrawScreenPass(GraphBuilder, {}, View, Viewport, Viewport, PipelineState, PassParameters,
				[MergePixelShader, PassParameters](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetStencilRef(kStencilMaskBit);
				SetShaderParameters(RHICmdList, MergePixelShader, MergePixelShader.GetPixelShader(), *PassParameters);
			});
		}
	}
}

void FDistortionMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);

		if (bIsTranslucent
			&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
			&& Material.IsDistorted())
		{
			Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		}
	}
}

void GetDistortionPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FDistortionMeshHS>& HullShader,
	TShaderRef<FDistortionMeshDS>& DomainShader,
	TShaderRef<FDistortionMeshVS>& VertexShader,
	TShaderRef<FDistortionMeshPS>& PixelShader)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	if (bNeedsHSDS)
	{
		DomainShader = Material.GetShader<FDistortionMeshDS>(VertexFactoryType);
		HullShader = Material.GetShader<FDistortionMeshHS>(VertexFactoryType);
	}

	VertexShader = Material.GetShader<FDistortionMeshVS>(VertexFactoryType);
	PixelShader = Material.GetShader<FDistortionMeshPS>(VertexFactoryType);
}

void FDistortionMeshProcessor::Process(
	const FMeshBatch& MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FDistortionMeshVS,
		FDistortionMeshHS,
		FDistortionMeshDS,
		FDistortionMeshPS> DistortionPassShaders;

	GetDistortionPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		DistortionPassShaders.HullShader,
		DistortionPassShaders.DomainShader,
		DistortionPassShaders.VertexShader,
		DistortionPassShaders.PixelShader
		);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(DistortionPassShaders.VertexShader, DistortionPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		DistortionPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

FDistortionMeshProcessor::FDistortionMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{}

FMeshPassProcessor* CreateDistortionPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DistortionPassState;
	DistortionPassState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	DistortionPassState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	
	// test against depth and write stencil mask
	DistortionPassState.SetDepthStencilState(TStaticDepthStencilState<
		false, CF_DepthNearOrEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		kStencilMaskBit, kStencilMaskBit>::GetRHI());

	DistortionPassState.SetStencilRef(kStencilMaskBit);

	// additive blending of offsets (or complexity if the shader complexity viewmode is enabled)
	DistortionPassState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

	return new(FMemStack::Get()) FDistortionMeshProcessor(Scene, InViewIfDynamicMeshCommand, DistortionPassState, InDrawListContext);
}

FMeshPassProcessor* CreateMobileDistortionPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DistortionPassState;
	DistortionPassState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	DistortionPassState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	// We don't have depth, render all pixels, pixel shader will sample SceneDepth from SceneColor.A and discard if occluded
	DistortionPassState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	// additive blending of offsets
	DistortionPassState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

	return new(FMemStack::Get()) FDistortionMeshProcessor(Scene, InViewIfDynamicMeshCommand, DistortionPassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterDistortionPass(&CreateDistortionPassProcessor, EShadingPath::Deferred, EMeshPass::Distortion, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileDistortionPass(&CreateMobileDistortionPassProcessor, EShadingPath::Mobile, EMeshPass::Distortion, EMeshPassFlags::MainView);