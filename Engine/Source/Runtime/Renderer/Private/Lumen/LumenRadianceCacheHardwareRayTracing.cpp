// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"

#include "LumenRadianceCache.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

// Console variables
static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracing(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing"),
	0,
	TEXT("Enables hardware ray tracing for Lumen radiance cache (Default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingIndirect(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.Indirect"),
	0,
	TEXT("Enables indirect ray tracing dispatch on compatible hardware (Default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingLightingMode(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.LightingMode"),
	0,
	TEXT("Determines the lighting mode (Default = 0)\n")
	TEXT("0: interpolate final lighting from the surface cache\n")
	TEXT("1: evaluate material, and interpolate irradiance and indirect irradiance from the surface cache\n")
	TEXT("2: evaluate material and direct lighting, and interpolate indirect irradiance from the surface cache"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedRadianceCache()
	{
#if RHI_RAYTRACING
		return (CVarLumenRadianceCacheHardwareRayTracing.GetValueOnRenderThread() != 0) && IsRayTracingEnabled();
#else
		return false;
#endif
	}

	EHardwareRayTracingLightingMode GetRadianceCacheHardwareRayTracingLightingMode()
	{
#if RHI_RAYTRACING
		return EHardwareRayTracingLightingMode(CVarLumenRadianceCacheHardwareRayTracingLightingMode.GetValueOnRenderThread());
#else
		return EHardwareRayTracingLightingMode::LightingFromSurfaceCache;
#endif
	}
}

#if RHI_RAYTRACING

class FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS, FLumenHardwareRayTracingRGS)

	// Permutations
	class FDeferredMaterialModeDim : SHADER_PERMUTATION_BOOL("DIM_DEFERRED_MATERIAL_MODE");
	class FLightingModeDim : SHADER_PERMUTATION_INT("DIM_LIGHTING_MODE", static_cast<int32>(Lumen::EHardwareRayTracingLightingMode::MAX));
	using FPermutationDomain = TShaderPermutationDomain<FDeferredMaterialModeDim, FLightingModeDim>;

	// Parameters
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)

		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_REF(FRGSRadianceCacheParameters, RGSRadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TraceProbesIndirectArgs)
		SHADER_PARAMETER(FIntPoint, ProbeTraceTileResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceTileRadianceAndHitDistanceTexture)
		RDG_BUFFER_ACCESS(RadianceCacheHardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS", SF_RayGen);

class FSplatRadianceCacheIntoAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FSplatRadianceCacheIntoAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDepthProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, RadianceAndHitDistanceTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(TraceProbesIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "SplatRadianceCacheIntoAtlasCS", SF_Compute);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCache(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	bool bUseDeferredMaterial = false;
	int LightingMode = CVarLumenRadianceCacheHardwareRayTracingLightingMode.GetValueOnRenderThread();

	{
		FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FDeferredMaterialModeDim>(bUseDeferredMaterial);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FLightingModeDim>(LightingMode);
		TShaderRef<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS>(PermutationVector);

		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCacheDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(CVarLumenRadianceCacheHardwareRayTracingLightingMode.GetValueOnRenderThread());
	bool bUseMinimalPayload = LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;

	if (Lumen::UseHardwareRayTracedRadianceCache() && bUseMinimalPayload)
	{
		{
			FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FDeferredMaterialModeDim>(false);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FLightingModeDim>(0);
			TShaderRef<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

#endif // RHI_RAYTRACING

void RenderLumenHardwareRayTracingRadianceCacheTwoPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	FIntPoint ProbeTraceTileResolution,

	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV
)
{
#if RHI_RAYTRACING
	Lumen::EHardwareRayTracingLightingMode LightingMode = static_cast<Lumen::EHardwareRayTracingLightingMode>(CVarLumenRadianceCacheHardwareRayTracingLightingMode.GetValueOnRenderThread());
	bool bUseMinimalPayload = LightingMode == Lumen::EHardwareRayTracingLightingMode::LightingFromSurfaceCache;

	uint32 TraceTileGroupStride = 128;
	FIntPoint MaxTraceTileCoord = FIntPoint((MaxNumProbes * ProbeTraceTileResolution.X * ProbeTraceTileResolution.Y) / TraceTileGroupStride, TraceTileGroupStride);
	FIntPoint TraceTileRadianceAndHitDistanceTextureSize(MaxTraceTileCoord.X, MaxTraceTileCoord.Y * FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::GetGroupSize() * FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::GetGroupSize());
	FRDGTextureDesc TraceTileRadianceAndHitDistanceTextureDesc = FRDGTextureDesc::Create2D(
		TraceTileRadianceAndHitDistanceTextureSize,
		PF_FloatRGBA,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef TraceTileRadianceAndHitDistanceTexture = GraphBuilder.CreateTexture(TraceTileRadianceAndHitDistanceTextureDesc, TEXT("RadianceAndHitDistanceTexture"));

	// Cast rays
	{
		FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FParameters>();

		// TODO: Use culling from ScreenProbeGather or our own??
		FLumenMeshSDFGridParameters MeshSDFGridParameters;
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			SceneTextures,
			View,
			TracingInputs,
			&PassParameters->SharedParameters
		);
		SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, DiffuseConeHalfAngle);

		// Radiance cache arguments
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		FRGSRadianceCacheParameters RGSRadianceCacheParameters;
		SetupRGSRadianceCacheParameters(RadianceCacheParameters, RGSRadianceCacheParameters);
		PassParameters->RGSRadianceCacheParameters = CreateUniformBufferImmediate(RGSRadianceCacheParameters, UniformBuffer_SingleFrame);
		PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
		PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
		PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
		PassParameters->TraceProbesIndirectArgs = GraphBuilder.CreateSRV(TraceProbesIndirectArgs, PF_R32_UINT);
		PassParameters->ProbeTraceTileResolution = ProbeTraceTileResolution;

		PassParameters->RWTraceTileRadianceAndHitDistanceTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TraceTileRadianceAndHitDistanceTexture));
		PassParameters->RadianceCacheHardwareRayTracingIndirectArgs = RadianceCacheHardwareRayTracingIndirectArgs;

		// Permutation declaration
		bool bUseDeferredMaterial = false;
		FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FDeferredMaterialModeDim>(bUseDeferredMaterial);
		PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS::FLightingModeDim>(static_cast<int>(LightingMode));

		TShaderRef<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS> RayGenerationShader =
			View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIntoTemporaryBufferRGS>(PermutationVector);
		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		if (CVarLumenRadianceCacheHardwareRayTracingIndirect.GetValueOnRenderThread() != 0)
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("LumenRadianceCacheHardwareRayTracingTwoPassRGS [Indirect] LightingMode=%s, DeferredMaterial=%u", Lumen::GetRayTracedLightingModeName(LightingMode), bUseDeferredMaterial),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, &View, RayGenerationShader, bUseMinimalPayload](FRHICommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					FRayTracingPipelineState* RayTracingPipeline = bUseMinimalPayload ? View.LumenHardwareRayTracingMaterialPipeline : View.RayTracingMaterialPipeline;
					PassParameters->RadianceCacheHardwareRayTracingIndirectArgs->MarkResourceAsUsed();
					RHICmdList.RayTraceDispatchIndirect(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, PassParameters->RadianceCacheHardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
			);
		}
		else
		{
			FIntPoint DispatchResolution = TraceTileRadianceAndHitDistanceTextureSize;
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("LumenRadianceCacheHardwareRayTracingTwoPassRGS %ux%u LightingMode=%s, DeferredMaterial=%u", DispatchResolution.X, DispatchResolution.Y, Lumen::GetRayTracedLightingModeName(LightingMode), bUseDeferredMaterial),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, &View, RayGenerationShader, DispatchResolution, bUseMinimalPayload](FRHICommandList& RHICmdList)
				{
					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
					FRayTracingPipelineState* RayTracingPipeline = bUseMinimalPayload ? View.LumenHardwareRayTracingMaterialPipeline : View.RayTracingMaterialPipeline;
					RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, DispatchResolution.X, DispatchResolution.Y);
				}
			);
		}
	}

	// Reduce to Atlas
	{
		FSplatRadianceCacheIntoAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSplatRadianceCacheIntoAtlasCS::FParameters>();
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, -1.0f);
		PassParameters->RWRadianceProbeAtlasTexture = RadianceProbeAtlasTextureUAV;
		PassParameters->RWDepthProbeAtlasTexture = DepthProbeTextureUAV;
		PassParameters->RadianceAndHitDistanceTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(TraceTileRadianceAndHitDistanceTexture));
		PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
		PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
		PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->TraceProbesIndirectArgs = TraceProbesIndirectArgs;

		FSplatRadianceCacheIntoAtlasCS::FPermutationDomain PermutationVector;
		auto ComputeShader = View.ShaderMap->GetShader<FSplatRadianceCacheIntoAtlasCS>(PermutationVector);

		if (CVarLumenRadianceCacheHardwareRayTracingIndirect.GetValueOnRenderThread() != 0)
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SplatRadianceCacheIntoAtlasCS (Indirect)"),
				ComputeShader,
				PassParameters,
				PassParameters->TraceProbesIndirectArgs,
				0);
		}
		else
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SplatRadianceCacheIntoAtlasCS Res=%ux%u", MaxTraceTileCoord.Y, MaxTraceTileCoord.X),
				ComputeShader,
				PassParameters,
				FIntVector(MaxTraceTileCoord.Y, MaxTraceTileCoord.X, 1));
		}
	}
#else
	unimplemented();
#endif // RHI_RAYTRACING
}


void RenderLumenHardwareRayTracingRadianceCache(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	FIntPoint ProbeTraceTileResolution,

	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV
)
{
#if RHI_RAYTRACING
	return RenderLumenHardwareRayTracingRadianceCacheTwoPass(
		GraphBuilder,
		SceneTextures,
		View,
		TracingInputs,
		RadianceCacheParameters,
		DiffuseConeHalfAngle,
		MaxNumProbes,
		ProbeTraceTileResolution,

		ProbeTraceData,
		ProbeTraceTileData,
		ProbeTraceTileAllocator,
		TraceProbesIndirectArgs,
		RadianceCacheHardwareRayTracingIndirectArgs,
		RadianceProbeAtlasTextureUAV,
		DepthProbeTextureUAV
	);
#else
	unimplemented();
#endif // RHI_RAYTRACING
}
