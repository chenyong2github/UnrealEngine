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
#include "RayTracingMaterialHitShaders.h"
#include "SceneTextureParameters.h"

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

static TAutoConsoleVariable<int32> CVarRayTracingAmbientOcclusionEnableMaterials(
	TEXT("r.RayTracing.AmbientOcclusion.EnableMaterials"),
	0,
	TEXT("Enables "),
	ECVF_RenderThreadSafe
);

bool ShouldRenderRayTracingAmbientOcclusion(const FViewInfo& View)
{
	const int32 ForceAllRayTracingEffects = GetForceRayTracingEffectsCVarValue();
	const bool bRTAOEnabled = (ForceAllRayTracingEffects > 0 || (GRayTracingAmbientOcclusion > 0 && ForceAllRayTracingEffects < 0));

	//#dxr_todo: add option to enable RTAO in View.FinalPostProcessSettings
	return IsRayTracingEnabled() && !ShouldRenderRayTracingGlobalIllumination(View) && bRTAOEnabled;
}

DECLARE_GPU_STAT_NAMED(RayTracingAmbientOcclusion, TEXT("Ray Tracing Ambient Occlusion"));

class FRayTracingAmbientOcclusionRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingAmbientOcclusionRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingAmbientOcclusionRGS, FGlobalShader)

	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMaterialsDim : SHADER_PERMUTATION_BOOL("ENABLE_MATERIALS");

	using FPermutationDomain = TShaderPermutationDomain<FEnableTwoSidedGeometryDim, FEnableMaterialsDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int, SamplesPerPixel)
		SHADER_PARAMETER(float, MaxRayDistance)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(float, MaxNormalBias)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWOcclusionMaskUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWHitDistanceUAV)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingAmbientOcclusionRGS, "/Engine/Private/RayTracing/RayTracingAmbientOcclusionRGS.usf", "AmbientOcclusionRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingAmbientOcclusion(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound
	FRayTracingAmbientOcclusionRGS::FPermutationDomain PermutationVector;
	for (uint32 TwoSidedGeometryIndex = 0; TwoSidedGeometryIndex < 2; ++TwoSidedGeometryIndex)
	{
		for (uint32 EnableMaterialsIndex = 0; EnableMaterialsIndex < 2; ++EnableMaterialsIndex)
		{
			PermutationVector.Set<FRayTracingAmbientOcclusionRGS::FEnableTwoSidedGeometryDim>(TwoSidedGeometryIndex != 0);
			PermutationVector.Set<FRayTracingAmbientOcclusionRGS::FEnableMaterialsDim>(EnableMaterialsIndex != 0);
			TShaderMapRef<FRayTracingAmbientOcclusionRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader->GetRayTracingShader());
		}
	}
}

void FDeferredShadingSceneRenderer::RenderRayTracingAmbientOcclusion(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& AmbientOcclusionRT)
{
	bool bAnyViewWithRTAO = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		bAnyViewWithRTAO = bAnyViewWithRTAO || ShouldRenderRayTracingAmbientOcclusion(View);
	}

	if (!bAnyViewWithRTAO)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Define RDG targets
	FRDGTextureRef AmbientOcclusionTexture;
	{
		FRDGTextureDesc Desc = SceneContext.GetSceneColor()->GetDesc();
		Desc.Format = PF_R16F;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		AmbientOcclusionTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingAmbientOcclusion"));
	}

	FRDGTextureRef RayDistanceTexture;
	{
		FRDGTextureDesc Desc = SceneContext.GetSceneColor()->GetDesc();
		Desc.Format = PF_R16F;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingAmbientOcclusionHitDistance"));
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{	
		FViewInfo& View = Views[ViewIndex];
		if (ShouldRenderRayTracingAmbientOcclusion(View))
		{
			RenderRayTracingAmbientOcclusion(RHICmdList, GraphBuilder, View, AmbientOcclusionTexture, RayDistanceTexture, AmbientOcclusionRT);
		}
	}

	GraphBuilder.Execute();
	SceneContext.bScreenSpaceAOIsValid = true;
	AmbientOcclusionRT->SetDebugName(TEXT("RayTracingAmbientOcclusion"));
	GVisualizeTexture.SetCheckPoint(RHICmdList, AmbientOcclusionRT);
}

void FDeferredShadingSceneRenderer::RenderRayTracingAmbientOcclusion(
	FRHICommandListImmediate& RHICmdList,
	FRDGBuilder& GraphBuilder,
	FViewInfo& View,
	FRDGTextureRef AmbientOcclusionTexture,
	FRDGTextureRef RayDistanceTexture,
	TRefCountPtr<IPooledRenderTarget>& AmbientOcclusionMaskRT
)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingAmbientOcclusion);
	RDG_EVENT_SCOPE(GraphBuilder, "Ray Tracing Ambient Occlusion");

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	// Build RTAO parameters
	FRayTracingAmbientOcclusionRGS::FParameters *PassParameters = GraphBuilder.AllocParameters<FRayTracingAmbientOcclusionRGS::FParameters>();
	PassParameters->SamplesPerPixel = GRayTracingAmbientOcclusionSamplesPerPixel >= 0 ? GRayTracingAmbientOcclusionSamplesPerPixel : View.FinalPostProcessSettings.RayTracingAOSamplesPerPixel;
	PassParameters->MaxRayDistance = View.FinalPostProcessSettings.AmbientOcclusionRadius;
	PassParameters->Intensity = View.FinalPostProcessSettings.AmbientOcclusionIntensity;
	PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->RWOcclusionMaskUAV = GraphBuilder.CreateUAV(AmbientOcclusionTexture);
	PassParameters->RWHitDistanceUAV = GraphBuilder.CreateUAV(RayDistanceTexture);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);
	PassParameters->SceneTextures = SceneTextures;

	FRayTracingAmbientOcclusionRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingAmbientOcclusionRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingAmbientOcclusionEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
	PermutationVector.Set<FRayTracingAmbientOcclusionRGS::FEnableMaterialsDim>(CVarRayTracingAmbientOcclusionEnableMaterials.GetValueOnRenderThread() != 0);
	TShaderMapRef<FRayTracingAmbientOcclusionRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
	ClearUnusedGraphResources(*RayGenerationShader, PassParameters);

	FIntPoint RayTracingResolution = View.ViewRect.Size();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("AmbientOcclusionRayTracing %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, this, &View, RayGenerationShader, RayTracingResolution](FRHICommandList& RHICmdList)
	{
		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, *RayGenerationShader, *PassParameters);

		// TODO: Provide material support for opacity mask
		FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;
		if (CVarRayTracingAmbientOcclusionEnableMaterials.GetValueOnRenderThread() == 0)
		{
			// Declare default pipeline
			FRayTracingPipelineStateInitializer Initializer;
			Initializer.MaxPayloadSizeInBytes = 52; // sizeof(FPackedMaterialClosestHitPayload)
			FRHIRayTracingShader* RayGenShaderTable[] = { RayGenerationShader->GetRayTracingShader() };
			Initializer.SetRayGenShaderTable(RayGenShaderTable);

			FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FOpaqueShadowHitGroup>()->GetRayTracingShader() };
			Initializer.SetHitGroupTable(HitGroupTable);
			Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

			Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);
		}

		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
		RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
	});

	int32 DenoiserMode = CVarUseAODenoiser.GetValueOnRenderThread();
	if (DenoiserMode != 0)
	{
		FSceneTextureParameters SceneTextureParams;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextureParams);

		const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
		const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig RayTracingConfig;

		IScreenSpaceDenoiser::FAmbientOcclusionInputs DenoiserInputs;
		DenoiserInputs.Mask = AmbientOcclusionTexture;
		DenoiserInputs.RayHitDistance = RayDistanceTexture;

		{
			RDG_EVENT_SCOPE(GraphBuilder, "%s%s(AmbientOcclusion) %dx%d",
				DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
				DenoiserToUse->GetDebugName(),
				View.ViewRect.Width(), View.ViewRect.Height());

			IScreenSpaceDenoiser::FAmbientOcclusionOutputs DenoiserOutputs = DenoiserToUse->DenoiseAmbientOcclusion(
				GraphBuilder,
				View,
				&View.PrevViewInfo,
				SceneTextureParams,
				DenoiserInputs,
				RayTracingConfig);

			GraphBuilder.QueueTextureExtraction(DenoiserOutputs.AmbientOcclusionMask, &AmbientOcclusionMaskRT);
		}
	}
	else
	{
		GraphBuilder.QueueTextureExtraction(AmbientOcclusionTexture, &AmbientOcclusionMaskRT);
	}
}

#endif // RHI_RAYTRACING
