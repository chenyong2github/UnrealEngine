// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Implements an experimental ray tracing reflection rendering algorithm based on ray and material sorting.
 * The algorithm consists of several separate stages:
 * - Generate reflection rays based on GBuffer (sorted in tiles by direction). Sorting may be optional in the future, based on performance measurements.
 * - Trace screen space reflection rays and output validity mask to avoid tracing/shading full rays [TODO; currently always tracing full rays]
 * - Trace reflection rays using lightweight RayGen shader and output material IDs
 * - Sort material IDs
 * - Execute material shaders and produce "Reflection GBuffer" [TODO; all lighting currently done in material eval RGS]
 * - Apply lighting to produce the final reflection buffer [TODO; all lighting currently done in material eval RGS]
 * 
 * Other features that are currently not implemented, but may be in the future:
 * - Shadow maps instead of ray traced shadows
 * 
 * Features that will never be supported due to performance:
 * - Multi-bounce
 * - Multi-SPP
 * - Clearcoat (only approximation will be supported)
 * - Translucency
 **/

#include "RayTracing/RayTracingLighting.h"
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RayTracingReflections.h"
#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "SceneTextureParameters.h"
#include "ReflectionEnvironment.h"

#if RHI_RAYTRACING

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsGenerateRaysWithRGS(
	TEXT("r.RayTracing.Reflections.ExperimentalDeferred.GenerateRaysWithRGS"),
	1,
	TEXT("Whether to generate reflection rays directly in RGS or in a separate compute shader (default: 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsGlossy(
	TEXT("r.RayTracing.Reflections.ExperimentalDeferred.Glossy"),
	1,
	TEXT("Whether to use glossy reflections with GGX sampling or to force mirror-like reflections for performance (default: 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarRayTracingReflectionsAnyHitMaxRoughness(
	TEXT("r.RayTracing.Reflections.ExperimentalDeferred.AnyHitMaxRoughness"),
	0.1,
	TEXT("Allows skipping AnyHit shader execution for rough reflection rays (default: 0.1)"),
	ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<float> CVarRayTracingReflectionsSmoothBias(
	TEXT("r.RayTracing.Reflections.ExperimentalDeferred.SmoothBias"),
	0.0,
	TEXT("Whether to bias reflections towards smooth / mirror-like directions. Improves performance, but is not physically based. (default: 0)\n")
	TEXT("The bias is implemented as a non-linear function, affecting low roughness values more than high roughness ones.\n")
	TEXT("Roughness values higher than this CVar value remain entirely unaffected.\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarRayTracingReflectionsMipBias(
	TEXT("r.RayTracing.Reflections.ExperimentalDeferred.MipBias"),
	0.0,
	TEXT("Global texture mip bias applied during ray tracing material evaluation. (default: 0)\n")
	TEXT("Improves ray tracing reflection performance at the cost of lower resolution textures in reflections. Values are clamped to range [0..15].\n"),
	ECVF_RenderThreadSafe
);

namespace 
{
	struct FSortedReflectionRay
	{
		float  Origin[3];
		uint32 PixelCoordinates; // X in low 16 bits, Y in high 16 bits
		float  Direction[3];
		float  Roughness; // Only technically need 8 bits, the rest could be repurposed
	};

	struct FRayIntersectionBookmark
	{
		uint32 Data[2];
	};
} // anon namespace

class FGenerateReflectionRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateReflectionRaysCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateReflectionRaysCS, FGlobalShader);

	class FWaveOps : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	SHADER_PARAMETER(FIntPoint, RayTracingResolution)
	SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
	SHADER_PARAMETER(float, ReflectionMaxNormalBias)
	SHADER_PARAMETER(float, ReflectionMaxRoughness)
	SHADER_PARAMETER(float, ReflectionSmoothBias)
	SHADER_PARAMETER(int, UpscaleFactor)
	SHADER_PARAMETER(int, GlossyReflections)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSortedReflectionRay>, RayBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 1024; // this shader generates rays and sorts them in 32x32 tiles using LDS
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}

};
IMPLEMENT_GLOBAL_SHADER(FGenerateReflectionRaysCS, "/Engine/Private/RayTracing/RayTracingReflectionsGenerateRaysCS.usf", "GenerateReflectionRaysCS", SF_Compute);

class FRayTracingDeferredReflectionsRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingDeferredReflectionsRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingDeferredReflectionsRGS, FGlobalShader)

	class FDeferredMaterialMode : SHADER_PERMUTATION_ENUM_CLASS("DIM_DEFERRED_MATERIAL_MODE", EDeferredMaterialMode);
	class FMissShaderLighting : SHADER_PERMUTATION_BOOL("DIM_MISS_SHADER_LIGHTING");
	class FGenerateRays : SHADER_PERMUTATION_BOOL("DIM_GENERATE_RAYS"); // Whether to generate rays in the RGS or in a separate CS
	class FAMDHitToken : SHADER_PERMUTATION_BOOL("DIM_AMD_HIT_TOKEN");
	using FPermutationDomain = TShaderPermutationDomain<FDeferredMaterialMode, FMissShaderLighting, FGenerateRays, FAMDHitToken>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, RayTracingResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionSmoothBias)
		SHADER_PARAMETER(float, AnyHitMaxRoughness)
		SHADER_PARAMETER(float, TextureMipBias)
		SHADER_PARAMETER(int, UpscaleFactor)
		SHADER_PARAMETER(int, GlossyReflections)
		SHADER_PARAMETER(int, ShouldDoDirectLighting)
		SHADER_PARAMETER(int, ShouldDoEmissiveAndIndirectLighting)
		SHADER_PARAMETER(int, ShouldDoReflectionCaptures)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSortedReflectionRay>, RayBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRayIntersectionBookmark>, BookmarkBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayHitDistanceOutput)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, Forward)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!ShouldCompileRayTracingShadersForProject(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDeferredMaterialMode>() == EDeferredMaterialMode::None)
		{
			return false;
		}

		if (PermutationVector.Get<FDeferredMaterialMode>() != EDeferredMaterialMode::Gather
			&& PermutationVector.Get<FGenerateRays>())
		{
			// DIM_GENERATE_RAYS only makes sense for "gather" mode
			return false;
		}

		if (PermutationVector.Get<FDeferredMaterialMode>() != EDeferredMaterialMode::Shade
			&& PermutationVector.Get<FMissShaderLighting>())
		{
			// DIM_MISS_SHADER_LIGHTING only makes sense for "shade" mode
			return false;
		}

		if (PermutationVector.Get<FAMDHitToken>() && !IsD3DPlatform(Parameters.Platform, false))
		{
			return false;
		}

		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1); // Always using 1D dispatches
		OutEnvironment.SetDefine(TEXT("ENABLE_TWO_SIDED_GEOMETRY"), 1); // Always using double-sided ray tracing for shadow rays
	}
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingDeferredReflectionsRGS, "/Engine/Private/RayTracing/RayTracingDeferredReflections.usf", "RayTracingDeferredReflectionsRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingDeferredReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FRayTracingDeferredReflectionsRGS::FPermutationDomain PermutationVector;

	const bool bGenerateRaysWithRGS = CVarRayTracingReflectionsGenerateRaysWithRGS.GetValueOnRenderThread() == 1;
	const bool bMissShaderLighting = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());
	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FAMDHitToken>(bHitTokenEnabled);

	{
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FMissShaderLighting>(false);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FGenerateRays>(bGenerateRaysWithRGS);
		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}

	{
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FMissShaderLighting>(bMissShaderLighting);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FGenerateRays>(false); // shading is independent of how rays are generated
		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareRayTracingDeferredReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	FRayTracingDeferredReflectionsRGS::FPermutationDomain PermutationVector;

	const bool bGenerateRaysWithRGS = CVarRayTracingReflectionsGenerateRaysWithRGS.GetValueOnRenderThread() == 1;
	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FAMDHitToken>(bHitTokenEnabled);
	PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
	PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FGenerateRays>(bGenerateRaysWithRGS);
	auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
	OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());

}

static void AddGenerateReflectionRaysPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGBufferRef RayBuffer,
	const FRayTracingDeferredReflectionsRGS::FParameters& CommonParameters)
{
	FGenerateReflectionRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateReflectionRaysCS::FParameters>();
	PassParameters->RayTracingResolution    = CommonParameters.RayTracingResolution;
	PassParameters->TileAlignedResolution   = CommonParameters.TileAlignedResolution;
	PassParameters->ReflectionMaxNormalBias = CommonParameters.ReflectionMaxNormalBias;
	PassParameters->ReflectionMaxRoughness  = CommonParameters.ReflectionMaxRoughness;
	PassParameters->ReflectionSmoothBias    = CommonParameters.ReflectionSmoothBias;
	PassParameters->UpscaleFactor           = CommonParameters.UpscaleFactor;
	PassParameters->GlossyReflections       = CommonParameters.GlossyReflections;
	PassParameters->ViewUniformBuffer       = CommonParameters.ViewUniformBuffer;
	PassParameters->SceneTextures           = CommonParameters.SceneTextures;
	PassParameters->RayBuffer               = GraphBuilder.CreateUAV(RayBuffer);

	FGenerateReflectionRaysCS::FPermutationDomain PermutationVector;
	const bool bUseWaveOps = GRHISupportsWaveOperations && GRHIMinimumWaveSize >= 32 && RHISupportsWaveOperations(View.GetShaderPlatform());
	PermutationVector.Set<FGenerateReflectionRaysCS::FWaveOps>(bUseWaveOps);

	auto ComputeShader = View.ShaderMap->GetShader<FGenerateReflectionRaysCS>(PermutationVector);
	ClearUnusedGraphResources(ComputeShader, PassParameters);

	const uint32 NumRays = CommonParameters.TileAlignedResolution.X * CommonParameters.TileAlignedResolution.Y;
	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp(NumRays, FGenerateReflectionRaysCS::GetGroupSize());
	GroupCount.Y = 1;
	GroupCount.Z = 1;
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GenerateReflectionRays"), ComputeShader, PassParameters, GroupCount);
}

void FDeferredShadingSceneRenderer::RenderRayTracingDeferredReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FRayTracingReflectionOptions& Options,
	IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs)
{
	const bool bGenerateRaysWithRGS = CVarRayTracingReflectionsGenerateRaysWithRGS.GetValueOnRenderThread()==1;
	const bool bMissShaderLighting = CanUseRayTracingLightingMissShader(View.GetShaderPlatform());

	int32 UpscaleFactor = int32(1.0f / Options.ResolutionFraction);
	ensure(Options.ResolutionFraction == 1.0 / UpscaleFactor);
	FIntPoint RayTracingResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);
	FIntPoint RayTracingBufferSize = SceneTextures.SceneDepthTexture->Desc.Extent / UpscaleFactor;

	FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
		RayTracingBufferSize,
		PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
		TexCreate_ShaderResource | TexCreate_UAV);

	OutDenoiserInputs->Color          = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflections"));
	OutputDesc.Format                 = PF_R16F;
	OutDenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflectionsHitDistance"));

	const uint32 SortTileSize             = 64; // Ray sort tile is 32x32, material sort tile is 64x64, so we use 64 here (tile size is not configurable).
	const FIntPoint TileAlignedResolution = FIntPoint::DivideAndRoundUp(RayTracingResolution, SortTileSize) * SortTileSize;

	FRayTracingDeferredReflectionsRGS::FParameters CommonParameters;
	CommonParameters.UpscaleFactor           = UpscaleFactor;
	CommonParameters.RayTracingResolution    = RayTracingResolution;
	CommonParameters.TileAlignedResolution   = TileAlignedResolution;
	CommonParameters.GlossyReflections       = CVarRayTracingReflectionsGlossy.GetValueOnRenderThread();
	CommonParameters.ReflectionMaxRoughness  = Options.MaxRoughness;
	CommonParameters.ReflectionSmoothBias    = CVarRayTracingReflectionsSmoothBias.GetValueOnRenderThread();
	CommonParameters.AnyHitMaxRoughness      = CVarRayTracingReflectionsAnyHitMaxRoughness.GetValueOnRenderThread();
	CommonParameters.GlossyReflections       = CVarRayTracingReflectionsGlossy.GetValueOnRenderThread();
	CommonParameters.TextureMipBias          = FMath::Clamp(CVarRayTracingReflectionsMipBias.GetValueOnRenderThread(), 0.0f, 15.0f);

	CommonParameters.ShouldDoDirectLighting              = Options.bDirectLighting;
	CommonParameters.ShouldDoEmissiveAndIndirectLighting = Options.bEmissiveAndIndirectLighting;
	CommonParameters.ShouldDoReflectionCaptures          = Options.bReflectionCaptures;

	CommonParameters.TLAS                    = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	CommonParameters.SceneTextures           = SceneTextures;
	CommonParameters.ViewUniformBuffer       = View.ViewUniformBuffer;
	CommonParameters.LightDataPacked         = View.RayTracingLightData.UniformBuffer;
	CommonParameters.LightDataBuffer         = View.RayTracingLightData.LightBufferSRV;
	CommonParameters.SSProfilesTexture       = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
	CommonParameters.ReflectionStruct        = CreateReflectionUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	CommonParameters.ReflectionCapture       = View.ReflectionCaptureUniformBuffer;
	CommonParameters.Forward                 = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
	CommonParameters.ReflectionMaxNormalBias = GetRaytracingMaxNormalBias();

	const bool bHitTokenEnabled = CanUseRayTracingAMDHitToken();

	// Generate sorted reflection rays

	const uint32 TileAlignedNumRays          = TileAlignedResolution.X * TileAlignedResolution.Y;
	const FRDGBufferDesc SortedRayBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSortedReflectionRay), TileAlignedNumRays);
	FRDGBufferRef SortedRayBuffer            = GraphBuilder.CreateBuffer(SortedRayBufferDesc, TEXT("ReflectionRayBuffer"));

	const FRDGBufferDesc DeferredMaterialBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), TileAlignedNumRays);
	FRDGBufferRef DeferredMaterialBuffer            = GraphBuilder.CreateBuffer(DeferredMaterialBufferDesc, TEXT("RayTracingReflectionsMaterialBuffer"));

	const FRDGBufferDesc BookmarkBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FRayIntersectionBookmark), TileAlignedNumRays);
	FRDGBufferRef BookmarkBuffer            = GraphBuilder.CreateBuffer(BookmarkBufferDesc, TEXT("RayTracingReflectionsBookmarkBuffer"));

	if (!bGenerateRaysWithRGS)
	{
		AddGenerateReflectionRaysPass(GraphBuilder, View, SortedRayBuffer, CommonParameters);
	}

	// Trace reflection material gather rays

	{
		FRayTracingDeferredReflectionsRGS::FParameters& PassParameters = *GraphBuilder.AllocParameters<FRayTracingDeferredReflectionsRGS::FParameters>();
		PassParameters                      = CommonParameters;
		PassParameters.MaterialBuffer       = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
		PassParameters.RayBuffer            = GraphBuilder.CreateUAV(SortedRayBuffer);
		PassParameters.BookmarkBuffer       = GraphBuilder.CreateUAV(BookmarkBuffer);
		PassParameters.ColorOutput          = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
		PassParameters.RayHitDistanceOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->RayHitDistance);

		FRayTracingDeferredReflectionsRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FAMDHitToken>(bHitTokenEnabled);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FGenerateRays>(bGenerateRaysWithRGS);

		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenShader, &PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracingDeferredReflectionsGather %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			&PassParameters,
			ERDGPassFlags::Compute,
		[&PassParameters, this, &View, TileAlignedNumRays, RayGenShader](FRHICommandList& RHICmdList)
		{
			FRayTracingPipelineState* Pipeline = View.RayTracingMaterialGatherPipeline;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, PassParameters);
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedNumRays, 1);
		});
	}

	// Sort hit points by material within 64x64 (4096 element) tiles

	SortDeferredMaterials(GraphBuilder, View, 5, TileAlignedNumRays, DeferredMaterialBuffer);

	// Shade reflection points

	{
		FRayTracingDeferredReflectionsRGS::FParameters& PassParameters = *GraphBuilder.AllocParameters<FRayTracingDeferredReflectionsRGS::FParameters>();
		PassParameters                      = CommonParameters;
		PassParameters.MaterialBuffer       = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
		PassParameters.RayBuffer            = GraphBuilder.CreateUAV(SortedRayBuffer);
		PassParameters.BookmarkBuffer       = GraphBuilder.CreateUAV(BookmarkBuffer);
		PassParameters.ColorOutput          = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
		PassParameters.RayHitDistanceOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->RayHitDistance);

		FRayTracingDeferredReflectionsRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FAMDHitToken>(bHitTokenEnabled);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FMissShaderLighting>(bMissShaderLighting);
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FGenerateRays>(false);

		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenShader, &PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracingDeferredReflectionsShade %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			&PassParameters,
			ERDGPassFlags::Compute,
		[&PassParameters, &View, TileAlignedNumRays, RayGenShader](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, PassParameters);
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedNumRays, 1);
		});
	}
}
#else // RHI_RAYTRACING
void FDeferredShadingSceneRenderer::RenderRayTracingDeferredReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FRayTracingReflectionOptions& Options,
	IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs)
{
	checkNoEntry();
}
#endif // RHI_RAYTRACING
