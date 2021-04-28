// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "RHIResources.h"
#include "PostProcess/PostProcessing.h"
#include "RayTracing/RaytracingOptions.h"
#include "Raytracing/RaytracingLighting.h"

DECLARE_GPU_STAT(RayTracingPrimaryRays);

class FRayTracingPrimaryRaysRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingPrimaryRaysRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingPrimaryRaysRGS, FGlobalShader)

	class FDenoiserOutput : SHADER_PERMUTATION_BOOL("DIM_DENOISER_OUTPUT");
	class FEnableTwoSidedGeometryForShadowDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FMissShaderLighting : SHADER_PERMUTATION_BOOL("DIM_MISS_SHADER_LIGHTING");

	using FPermutationDomain = TShaderPermutationDomain<FDenoiserOutput, FEnableTwoSidedGeometryForShadowDim, FMissShaderLighting>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, SamplesPerPixel)
		SHADER_PARAMETER(int32, MaxRefractionRays)
		SHADER_PARAMETER(int32, HeightFog)
		SHADER_PARAMETER(int32, ShouldDoDirectLighting)
		SHADER_PARAMETER(int32, ReflectedShadowsType)
		SHADER_PARAMETER(int32, ShouldDoEmissiveAndIndirectLighting)
		SHADER_PARAMETER(int32, UpscaleFactor)
		SHADER_PARAMETER(int32, ShouldUsePreExposure)
		SHADER_PARAMETER(uint32, PrimaryRayFlags)
		SHADER_PARAMETER(float, TranslucencyMinRayDistance)
		SHADER_PARAMETER(float, TranslucencyMaxRayDistance)
		SHADER_PARAMETER(float, TranslucencyMaxRoughness)
		SHADER_PARAMETER(int32, TranslucencyRefraction)
		SHADER_PARAMETER(float, MaxNormalBias)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FFogUniformParameters, FogUniformParameters)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayHitDistanceOutput)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingPrimaryRaysRGS, "/Engine/Private/RayTracing/RayTracingPrimaryRays.usf", "RayTracingPrimaryRaysRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingTranslucency(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound.
	// NOTE: Translucency shader may be used for primary ray debug view mode.
	if (GetRayTracingTranslucencyOptions(View).bEnabled || View.RayTracingRenderMode == ERayTracingRenderMode::RayTracingDebug)
	{
		FRayTracingPrimaryRaysRGS::FPermutationDomain PermutationVector;

		const bool bLightingMissShader = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());
		PermutationVector.Set<FRayTracingPrimaryRaysRGS::FMissShaderLighting>(bLightingMissShader);

		PermutationVector.Set<FRayTracingPrimaryRaysRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());

		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingPrimaryRaysRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::RenderRayTracingPrimaryRaysView(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef* InOutColorTexture,
	FRDGTextureRef* InOutRayHitDistanceTexture,
	int32 SamplePerPixel,
	int32 HeightFog,
	float ResolutionFraction,
	ERayTracingPrimaryRaysFlag Flags)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	int32 UpscaleFactor = int32(1.0f / ResolutionFraction);
	ensure(ResolutionFraction == 1.0 / UpscaleFactor);
	ensureMsgf(FComputeShaderUtils::kGolden2DGroupSize % UpscaleFactor == 0, TEXT("PrimaryRays ray tracing will have uv misalignement."));
	FIntPoint RayTracingResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);

	{
		FRDGTextureDesc Desc = Translate(SceneContext.GetSceneColor()->GetDesc());
		Desc.Format = PF_FloatRGBA;
		Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
		Desc.Flags |= TexCreate_UAV;
		Desc.Extent /= UpscaleFactor;

		if(*InOutColorTexture == nullptr) 
		{
			*InOutColorTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingPrimaryRays"));
			
		}

		Desc.Format = PF_R16F;
		if(*InOutRayHitDistanceTexture == nullptr) 
		{
			*InOutRayHitDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingPrimaryRaysHitDistance"));
		}
	}

	FRayTracingPrimaryRaysRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingPrimaryRaysRGS::FParameters>();

	FRayTracingPrimaryRaysOptions TranslucencyOptions = GetRayTracingTranslucencyOptions(View);
	PassParameters->SamplesPerPixel = SamplePerPixel;
	PassParameters->MaxRefractionRays = TranslucencyOptions.MaxRefractionRays > -1 ? TranslucencyOptions.MaxRefractionRays : View.FinalPostProcessSettings.RayTracingTranslucencyRefractionRays;
	PassParameters->HeightFog = HeightFog;
	PassParameters->ShouldDoDirectLighting = TranslucencyOptions.EnableDirectLighting;
	PassParameters->ReflectedShadowsType = TranslucencyOptions.EnableShadows > -1 ? TranslucencyOptions.EnableShadows : (int32)View.FinalPostProcessSettings.RayTracingTranslucencyShadows;
	PassParameters->ShouldDoEmissiveAndIndirectLighting = TranslucencyOptions.EnableEmmissiveAndIndirectLighting;
	PassParameters->UpscaleFactor = UpscaleFactor;
	PassParameters->TranslucencyMinRayDistance = FMath::Min(TranslucencyOptions.MinRayDistance, TranslucencyOptions.MaxRayDistance);
	PassParameters->TranslucencyMaxRayDistance = TranslucencyOptions.MaxRayDistance;
	PassParameters->TranslucencyMaxRoughness = FMath::Clamp(TranslucencyOptions.MaxRoughness >= 0 ? TranslucencyOptions.MaxRoughness : View.FinalPostProcessSettings.RayTracingTranslucencyMaxRoughness, 0.01f, 1.0f);
	PassParameters->TranslucencyRefraction = TranslucencyOptions.EnableRefraction >= 0 ? TranslucencyOptions.EnableRefraction : View.FinalPostProcessSettings.RayTracingTranslucencyRefraction;
	PassParameters->MaxNormalBias = GetRaytracingMaxNormalBias();
	PassParameters->ShouldUsePreExposure = View.Family->EngineShowFlags.Tonemapper;
	PassParameters->PrimaryRayFlags = (uint32)Flags;
	PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	PassParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;

	PassParameters->SceneTextures = SceneTextures;

	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
	PassParameters->SceneColorTexture = SceneColorTexture;

	PassParameters->ReflectionStruct = CreateReflectionUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	PassParameters->FogUniformParameters = CreateFogUniformBuffer(GraphBuilder, View);

	PassParameters->ColorOutput = GraphBuilder.CreateUAV(*InOutColorTexture);
	PassParameters->RayHitDistanceOutput = GraphBuilder.CreateUAV(*InOutRayHitDistanceTexture);

	// TODO: should be converted to RDG
	PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);

	const bool bMissShaderLighting = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());

	FRayTracingPrimaryRaysRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingPrimaryRaysRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
	PermutationVector.Set< FRayTracingPrimaryRaysRGS::FMissShaderLighting>(bMissShaderLighting);

	auto RayGenShader = View.ShaderMap->GetShader<FRayTracingPrimaryRaysRGS>(PermutationVector);

	ClearUnusedGraphResources(RayGenShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RayTracingPrimaryRays %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, this, &View, RayGenShader, RayTracingResolution](FRHICommandList& RHICmdList)
	{
		SCOPED_GPU_STAT(RHICmdList, RayTracingPrimaryRays);
	FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;

		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
		RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
	});
}

#endif // RHI_RAYTRACING
