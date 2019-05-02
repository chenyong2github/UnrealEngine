// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DeferredShadingRenderer.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "SceneUtils.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "RHI/Public/PipelineStateCache.h"
#include "Raytracing/RaytracingOptions.h"
#include "SceneViewFamilyBlackboard.h"

#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"

static int32 GRayTracingAmbientOcclusion = 1;
static FAutoConsoleVariableRef CVarRayTracingAmbientOcclusion(
	TEXT("r.RayTracing.AmbientOcclusion"),
	GRayTracingAmbientOcclusion,
	TEXT("Enables ray tracing ambient occlusion (default = 1)")
);

static TAutoConsoleVariable<int32> CVarUseAODenoiser(
	TEXT("r.AmbientOcclusion.Denoiser"),
	2,
	TEXT("Choose the denoising algorithm.\n")
	TEXT(" 0: Disabled;\n")
	TEXT(" 1: Forces the default denoiser of the renderer;\n")
	TEXT(" 2: GScreenSpaceDenoiser witch may be overriden by a third party plugin (default)."),
	ECVF_RenderThreadSafe);

static int32 GRayTracingAmbientOcclusionSamplesPerPixel = -1;
static FAutoConsoleVariableRef CVarRayTracingAmbientOcclusionSamplesPerPixel(
	TEXT("r.RayTracing.AmbientOcclusion.SamplesPerPixel"),
	GRayTracingAmbientOcclusionSamplesPerPixel,
	TEXT("Sets the samples-per-pixel for ambient occlusion (default = -1 (driven by postprocesing volume))")
);

static TAutoConsoleVariable<int32> CVarRayTracingAmbientOcclusionEnableTwoSidedGeometry(
	TEXT("r.RayTracing.AmbientOcclusion.EnableTwoSidedGeometry"),
	0,
	TEXT("Enables two-sided geometry when tracing shadow rays (default = 0)"),
	ECVF_RenderThreadSafe
);


bool ShouldRenderRayTracingAmbientOcclusion(const FViewInfo& View)
{
	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	const bool bRTAOEnabled = (ForceAllRayTracingEffects > 0 || (GRayTracingAmbientOcclusion > 0 && ForceAllRayTracingEffects < 0));

	//#dxr_todo: add option to enable RTAO in View.FinalPostProcessSettings
	return IsRayTracingEnabled() && !ShouldRenderRayTracingGlobalIllumination(View) && bRTAOEnabled;
}


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAmbientOcclusionData, )
SHADER_PARAMETER(int, SamplesPerPixel)
SHADER_PARAMETER(float, MaxRayDistance)
SHADER_PARAMETER(float, Intensity)
SHADER_PARAMETER(float, MaxNormalBias)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FAmbientOcclusionData, "AmbientOcclusion");

DECLARE_GPU_STAT_NAMED(RayTracingAmbientOcclusion, TEXT("Ray Tracing Ambient Occlusion"));
DECLARE_GPU_STAT_NAMED(AmbientOcclusionDenoiser, TEXT("Ambient Occlusion Denoiser"));

template<uint32 EnableTwoSidedGeometry>
class TAmbientOcclusionRGS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TAmbientOcclusionRGS, Global)

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_TWO_SIDED_GEOMETRY"), EnableTwoSidedGeometry);
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	TAmbientOcclusionRGS() {}
	virtual ~TAmbientOcclusionRGS() {}

	TAmbientOcclusionRGS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ViewParameter.Bind(Initializer.ParameterMap, TEXT("View"));
		TLASParameter.Bind(Initializer.ParameterMap, TEXT("TLAS"));
		SceneTexturesParameter.Bind(Initializer.ParameterMap, TEXT("SceneTexturesStruct"));
		AmbientOcclusionParameter.Bind(Initializer.ParameterMap, TEXT("AmbientOcclusion"));

		OcclusionMaskUAVParameter.Bind(Initializer.ParameterMap, TEXT("RWOcclusionMaskUAV"));
		RayDistanceUAVParameter.Bind(Initializer.ParameterMap, TEXT("RWHitDistanceUAV"));
	}

	bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ViewParameter;
		Ar << TLASParameter;
		Ar << SceneTexturesParameter;
		Ar << AmbientOcclusionParameter;
		Ar << OcclusionMaskUAVParameter;
		Ar << RayDistanceUAVParameter;
		return bShaderHasOutdatedParameters;
	}

	void Dispatch(
		FRHICommandListImmediate& RHICmdList,
		const FRayTracingScene& RayTracingScene,
		FUniformBufferRHIParamRef ViewUniformBuffer,
		FUniformBufferRHIParamRef SceneTexturesUniformBuffer,
		FUniformBufferRHIParamRef AmbientOcclusionUniformBuffer,
		FUnorderedAccessViewRHIParamRef OcclusionMaskUAV,
		FUnorderedAccessViewRHIParamRef HitDistanceUAV,
		uint32 Width, uint32 Height
	)
	{
		FRayTracingPipelineStateInitializer Initializer;

		FRayTracingShaderRHIParamRef RayGenShaderTable[] = { GetRayTracingShader() };
		Initializer.SetRayGenShaderTable(RayGenShaderTable);

		FRHIRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(Initializer); 

		FRayTracingShaderBindingsWriter GlobalResources;
		GlobalResources.Set(TLASParameter, RayTracingScene.RayTracingSceneRHI->GetShaderResourceView());
		GlobalResources.Set(ViewParameter, ViewUniformBuffer);
		GlobalResources.Set(SceneTexturesParameter, SceneTexturesUniformBuffer);
		GlobalResources.Set(AmbientOcclusionParameter, AmbientOcclusionUniformBuffer);
		GlobalResources.Set(OcclusionMaskUAVParameter, OcclusionMaskUAV);
		GlobalResources.Set(RayDistanceUAVParameter, HitDistanceUAV);

		RHICmdList.RayTraceDispatch(Pipeline, GetRayTracingShader(), RayTracingScene.RayTracingSceneRHI, GlobalResources, Width, Height);
	}

private:
	// Input
	FShaderResourceParameter TLASParameter;
	FShaderUniformBufferParameter ViewParameter;
	FShaderUniformBufferParameter SceneTexturesParameter;
	FShaderUniformBufferParameter AmbientOcclusionParameter;

	// Output
	FShaderResourceParameter OcclusionMaskUAVParameter;
	FShaderResourceParameter RayDistanceUAVParameter;
};

IMPLEMENT_SHADER_TYPE(template<>, TAmbientOcclusionRGS<0>, TEXT("/Engine/Private/RayTracing/RayTracingAmbientOcclusionRGS.usf"), TEXT("AmbientOcclusionRGS"), SF_RayGen)
IMPLEMENT_SHADER_TYPE(template<>, TAmbientOcclusionRGS<1>, TEXT("/Engine/Private/RayTracing/RayTracingAmbientOcclusionRGS.usf"), TEXT("AmbientOcclusionRGS"), SF_RayGen)

void FDeferredShadingSceneRenderer::RenderRayTracingAmbientOcclusion(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& AmbientOcclusionMaskRT)
{
	SCOPED_DRAW_EVENT(RHICmdList, RayTracingAmbientOcclusion);
	SCOPED_GPU_STAT(RHICmdList, RayTracingAmbientOcclusion);

	bool bAnyViewWithRTAO = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		bAnyViewWithRTAO = bAnyViewWithRTAO | ShouldRenderRayTracingAmbientOcclusion(View);
	}

	if (!bAnyViewWithRTAO)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FPooledRenderTargetDesc Desc = SceneContext.GetSceneColor()->GetDesc();
	Desc.Format = PF_R16F;
	Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);

	TRefCountPtr<IPooledRenderTarget> HitDistanceRT;

	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, AmbientOcclusionMaskRT, TEXT("RayTracingAmbientOcclusion"));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, HitDistanceRT, TEXT("RayTracingAmbientOcclusionHitDistance"));

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{	
		FViewInfo& View = Views[ViewIndex];
		if (ShouldRenderRayTracingAmbientOcclusion(View))
		{
			RenderRayTracingAmbientOcclusion(RHICmdList, GraphBuilder, View, AmbientOcclusionMaskRT, HitDistanceRT);
		}
	}

	GraphBuilder.Execute();

	SceneContext.bScreenSpaceAOIsValid = true;
}

void FDeferredShadingSceneRenderer::RenderRayTracingAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList, 
	FRDGBuilder& GraphBuilder, 
	FViewInfo& View,
	TRefCountPtr<IPooledRenderTarget>& AmbientOcclusionMaskRT,
	TRefCountPtr<IPooledRenderTarget>& HitDistanceRT
)
{
	//#dxr_todo refactor to RDG
	
	FIntPoint ViewSize = View.ViewRect.Size();
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Add ambient occlusion parameters to uniform buffer
	FAmbientOcclusionData AmbientOcclusionData;

	FSceneTexturesUniformParameters SceneTextures;
	SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, ESceneTextureSetupMode::All, SceneTextures);
	FUniformBufferRHIRef SceneTexturesUniformBuffer = RHICreateUniformBuffer(&SceneTextures, FSceneTexturesUniformParameters::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);

	AmbientOcclusionData.SamplesPerPixel = GRayTracingAmbientOcclusionSamplesPerPixel >= 0 ? GRayTracingAmbientOcclusionSamplesPerPixel : View.FinalPostProcessSettings.RayTracingAOSamplesPerPixel;
	AmbientOcclusionData.MaxRayDistance = View.FinalPostProcessSettings.AmbientOcclusionRadius;
	AmbientOcclusionData.Intensity = View.FinalPostProcessSettings.AmbientOcclusionIntensity;
	AmbientOcclusionData.MaxNormalBias = GetRaytracingMaxNormalBias();
	FUniformBufferRHIRef AmbientOcclusionUniformBuffer = RHICreateUniformBuffer(&AmbientOcclusionData, FAmbientOcclusionData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);

	int32 EnableTwoSidedGeometry = CVarRayTracingAmbientOcclusionEnableTwoSidedGeometry.GetValueOnRenderThread();
	if (EnableTwoSidedGeometry)
	{
		TShaderMapRef<TAmbientOcclusionRGS<1>> AmbientOcclusionRayGenerationShader(GetGlobalShaderMap(FeatureLevel));
		AmbientOcclusionRayGenerationShader->Dispatch(
			RHICmdList,
			View.RayTracingScene,
			View.ViewUniformBuffer,
			SceneTexturesUniformBuffer,
			AmbientOcclusionUniformBuffer,
			AmbientOcclusionMaskRT->GetRenderTargetItem().UAV,
			HitDistanceRT->GetRenderTargetItem().UAV,
			ViewSize.X, ViewSize.Y
		);
	}
	else
	{
		TShaderMapRef<TAmbientOcclusionRGS<0>> AmbientOcclusionRayGenerationShader(GetGlobalShaderMap(FeatureLevel));
		AmbientOcclusionRayGenerationShader->Dispatch(
			RHICmdList,
			View.RayTracingScene,
			View.ViewUniformBuffer,
			SceneTexturesUniformBuffer,
			AmbientOcclusionUniformBuffer,
			AmbientOcclusionMaskRT->GetRenderTargetItem().UAV,
			HitDistanceRT->GetRenderTargetItem().UAV,
			ViewSize.X, ViewSize.Y
		);
	}

	int32 DenoiserMode = CVarUseAODenoiser.GetValueOnRenderThread();
	if (DenoiserMode != 0)
	{
		SCOPED_GPU_STAT(RHICmdList, AmbientOcclusionDenoiser);

		FSceneViewFamilyBlackboard SceneBlackboard;
		SetupSceneViewFamilyBlackboard(GraphBuilder, &SceneBlackboard);

		const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
		const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;

		IScreenSpaceDenoiser::FAmbientOcclusionInputs DenoiserInputs;
		DenoiserInputs.Mask = GraphBuilder.RegisterExternalTexture(SceneContext.ScreenSpaceAO, TEXT("AOMask"));
		DenoiserInputs.RayHitDistance = GraphBuilder.RegisterExternalTexture(HitDistanceRT, TEXT("AOHitDistance"));

		{
			RDG_EVENT_SCOPE(GraphBuilder, "%s%s(AmbientOcclusion) %dx%d",
				DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
				DenoiserToUse->GetDebugName(),
				View.ViewRect.Width(), View.ViewRect.Height());

			IScreenSpaceDenoiser::FAmbientOcclusionOutputs DenoiserOutputs = DenoiserToUse->DenoiseAmbientOcclusion(
				GraphBuilder,
				View,
				&View.PrevViewInfo,
				SceneBlackboard,
				DenoiserInputs,
				RayTracingConfig);

			GraphBuilder.QueueTextureExtraction(DenoiserOutputs.AmbientOcclusionMask, &SceneContext.ScreenSpaceAO);
		}
	}

	FUnorderedAccessViewRHIParamRef UAVs[]
	{
		AmbientOcclusionMaskRT->GetRenderTargetItem().UAV,
		HitDistanceRT->GetRenderTargetItem().UAV
	};
	FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("RayTracingAmbientOcclusion"));
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, UAVs, ARRAY_COUNT(UAVs), Fence);
}

#endif // RHI_RAYTRACING
