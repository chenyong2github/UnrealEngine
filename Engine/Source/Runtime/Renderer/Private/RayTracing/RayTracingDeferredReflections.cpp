// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Implements an experimental ray tracing reflection rendering algorithm based on ray and material sorting.
 * The algorithm consists of several separate stages:
 * - Generate reflection rays based on GBuffer (sorted in tiles by direction). Sorting may be optional in the future, based on performance measurements.
 * - Trace screen space reflection rays and output validity mask to avoid tracing/shading full rays [TODO; currently always tracing full rays]
 * - Trace reflection rays using lightweight RayGen shader and output material IDs
 * - Sort material IDs
 * - Execute material shaders and produce "Reflection GBuffer"
 * - Apply lighting to produce the final reflection buffer [TODO; currently done in material eval RGS]
 * 
 * Other features that are currently not implemented, but may be in the future:
 * - Roughness threshold
 * - Forced mirror-like reflections (similar to SSR low quality profile)
 * - Alpha masked materials
 * - Reflection capture for multi-bounce fallback
 * - Shadow maps instead of ray traced shadows
 * 
 * Features that will never be supported due to performance:
 * - Multi-bounce
 * - Multi-SPP
 * - Clearcoat
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

namespace 
{
	struct FSortedReflectionRay
	{
		float  Origin[3];
		uint32 PixelCoordinates; // X in low 16 bits, Y in high 16 bits
		float  Direction[3];
		uint32 DebugSortKey;
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
	using FPermutationDomain = TShaderPermutationDomain<FDeferredMaterialMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, RayTracingResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FSortedReflectionRay>, RayBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRayIntersectionBookmark>, BookmarkBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayHitDistanceOutput)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
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

	{
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}

	{
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
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
	FRDGTextureDesc OutputDesc = FPooledRenderTargetDesc::Create2DDesc(
		FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(),
		PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
		TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV,
		false);

	OutDenoiserInputs->Color          = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflections"));
	OutputDesc.Format                 = PF_R16F;
	OutDenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(OutputDesc, TEXT("RayTracingReflectionsHitDistance"));

	const FIntPoint RayTracingResolution = View.ViewRect.Size();

	const uint32 SortTileSize             = 64; // Ray sort tile is 32x32, material sort tile is 64x64, so we use 64 here (tile size is not configurable).
	const FIntPoint TileAlignedResolution = FIntPoint::DivideAndRoundUp(RayTracingResolution, SortTileSize) * SortTileSize;

	FRayTracingDeferredReflectionsRGS::FParameters CommonParameters;
	CommonParameters.RayTracingResolution    = RayTracingResolution;
	CommonParameters.TileAlignedResolution   = TileAlignedResolution;
	CommonParameters.ReflectionMaxRoughness  = Options.MaxRoughness;
	CommonParameters.TLAS                    = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	CommonParameters.SceneTextures           = SceneTextures;
	SetupSceneTextureSamplers(&CommonParameters.SceneTextureSamplers);
	CommonParameters.ViewUniformBuffer       = View.ViewUniformBuffer;
	CommonParameters.LightDataPacked         = View.RayTracingLightingDataUniformBuffer;
	CommonParameters.LightDataBuffer         = View.RayTracingLightingDataSRV;
	CommonParameters.SSProfilesTexture       = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
	CommonParameters.ReflectionStruct        = CreateReflectionUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	CommonParameters.ReflectionMaxNormalBias = GetRaytracingMaxNormalBias();

	// Generate sorted reflection rays

	const uint32 TileAlignedNumRays          = TileAlignedResolution.X * TileAlignedResolution.Y;
	const FRDGBufferDesc SortedRayBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FSortedReflectionRay), TileAlignedNumRays);
	FRDGBufferRef SortedRayBuffer            = GraphBuilder.CreateBuffer(SortedRayBufferDesc, TEXT("ReflectionRayBuffer"));

	const FRDGBufferDesc DeferredMaterialBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), TileAlignedNumRays);
	FRDGBufferRef DeferredMaterialBuffer            = GraphBuilder.CreateBuffer(DeferredMaterialBufferDesc, TEXT("RayTracingReflectionsMaterialBuffer"));

	const FRDGBufferDesc BookmarkBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FRayIntersectionBookmark), TileAlignedNumRays);
	FRDGBufferRef BookmarkBuffer            = GraphBuilder.CreateBuffer(BookmarkBufferDesc, TEXT("RayTracingReflectionsBookmarkBuffer"));

	AddGenerateReflectionRaysPass(GraphBuilder, View, SortedRayBuffer, CommonParameters);

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
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);

		auto RayGenShader = View.ShaderMap->GetShader<FRayTracingDeferredReflectionsRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenShader, &PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracingDeferredReflectionsGather %dx%d", RayTracingResolution.X, RayTracingResolution.Y),
			&PassParameters,
			ERDGPassFlags::Compute,
		[&PassParameters, this, &View, TileAlignedNumRays, RayGenShader](FRHICommandList& RHICmdList)
		{
			FRayTracingPipelineState* Pipeline = BindRayTracingDeferredMaterialGatherPipeline(RHICmdList, View, RayGenShader.GetRayTracingShader());

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
		PermutationVector.Set<FRayTracingDeferredReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);

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
