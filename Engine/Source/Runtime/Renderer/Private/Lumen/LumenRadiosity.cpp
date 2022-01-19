// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadiosity.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenRadianceCache.h"
#include "LumenSceneLighting.h"
#include "LumenTracingUtils.h"

#include "LumenHardwareRayTracingCommon.h"

int32 GLumenRadiosity = 1;
FAutoConsoleVariableRef CVarLumenRadiosity(
	TEXT("r.LumenScene.Radiosity"),
	GLumenRadiosity,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityDownsampleFactor = 2;
FAutoConsoleVariableRef CVarLumenRadiosityDownsampleFactor(
	TEXT("r.LumenScene.Radiosity.DownsampleFactor"),
	GLumenRadiosityDownsampleFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadiosityDenoising = 1;
FAutoConsoleVariableRef CVarRadiosityDenoise(
	TEXT("r.LumenScene.Radiosity.Denoising"),
	GRadiosityDenoising,
	TEXT("Whether to use denoising for radiosity."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GRadiosityTracesPerTexel = 8;
FAutoConsoleVariableRef CVarRadiosityTracesPerTexel(
	TEXT("r.LumenScene.Radiosity.TracesPerTexel"),
	GRadiosityTracesPerTexel,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GRadiosityTraceStepFactor = 2;
FAutoConsoleVariableRef CVarRadiosityTraceStepFactor(
	TEXT("r.LumenScene.Radiosity.TraceStepFactor"),
	GRadiosityTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityMinSampleRadius = 10;
FAutoConsoleVariableRef CVarLumenRadiosityMinSampleRadius(
	TEXT("r.LumenScene.Radiosity.MinSampleRadius"),
	GLumenRadiosityMinSampleRadius,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityMinTraceDistanceToSampleSurface = 10.0f;
FAutoConsoleVariableRef CVarLumenRadiosityMinTraceDistanceToSampleSurface(
	TEXT("r.LumenScene.Radiosity.MinTraceDistanceToSampleSurface"),
	GLumenRadiosityMinTraceDistanceToSampleSurface,
	TEXT("Ray hit distance from which we can start sampling surface cache in order to fix radiosity feedback loop where surface cache texel hits itself every frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityMaxRayIntensity = 10.0f;
FAutoConsoleVariableRef CVarLumenRadiosityMaxRayIntensity(
	TEXT("r.LumenScene.Radiosity.MaxRayIntensity"),
	GLumenRadiosityMaxRayIntensity,
	TEXT("Clamps Radiosity trace intensity, relative to current view exposure.  Useful for reducing artifacts from small bright emissive sources, but loses energy and adds view dependence."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityDistanceFieldSurfaceBias = 10.0f;
FAutoConsoleVariableRef CVarLumenRadiositySurfaceBias(
	TEXT("r.LumenScene.Radiosity.DistanceFieldSurfaceBias"),
	GLumenRadiosityDistanceFieldSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityDistanceFieldSurfaceSlopeBias = 5.0f;
FAutoConsoleVariableRef CVarLumenRadiosityDistanceFieldSurfaceBias(
	TEXT("r.LumenScene.Radiosity.DistanceFieldSurfaceSlopeBias"),
	GLumenRadiosityDistanceFieldSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityHardwareRayTracingSurfaceBias = 0.1f;
FAutoConsoleVariableRef CVarLumenRadiosityHardwareRayTracingSurfaceBias(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracingSurfaceBias"),
	GLumenRadiosityHardwareRayTracingSurfaceBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityHardwareRayTracingSurfaceSlopeBias = 0.2f;
FAutoConsoleVariableRef CVarLumenRadiosityHardwareRayTracingSlopeSurfaceBias(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracingSlopeSurfaceBias"),
	GLumenRadiosityHardwareRayTracingSurfaceSlopeBias,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityConeAngleScale = 1.0f;
FAutoConsoleVariableRef CVarLumenRadiosityConeAngleScale(
	TEXT("r.LumenScene.Radiosity.ConeAngleScale"),
	GLumenRadiosityConeAngleScale,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadiosityVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarRadiosityVoxelStepFactor(
	TEXT("r.LumenScene.Radiosity.VoxelStepFactor"),
	GLumenRadiosityVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadiosityHardwareRayTracing(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for radiosity (default = 1)."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadiosityHardwareRayTracingIndirect(
	TEXT("r.LumenScene.Radiosity.HardwareRayTracing.Indirect"),
	1,
	TEXT("Enables indirect dispatch for hardware ray tracing for radiosity (default = 1)."),
	ECVF_RenderThreadSafe
);

namespace LumenRadiosity
{
	// Must match LumenRadiosityProbeGather.ush
	constexpr uint32 NumRayDirections = 8;
	constexpr uint32 ProbeSHTileSize = 4;
	constexpr uint32 TRACE_BUFFER_STRIDE_IN_TILES = 512;
	constexpr uint32 TRACE_BUFFER_MICRO_TILE_SIZE = 8;

	FHemisphereDirectionSampleGenerator RayDirections;

	void AddRadiosityPass(
		FRDGBuilder& GraphBuilder,
		const FScene* Scene,
		const FViewInfo& View,
		bool bRenderSkylight,
		FLumenSceneData& LumenSceneData,
		FRDGTextureRef RadiosityAtlas,
		const FLumenCardTracingInputs& TracingInputs,
		const FLumenCardUpdateContext& CardUpdateContext);

	float GetConeHalfAngle();
	uint32 GetNumTracesPerTexel();
}

bool Lumen::UseHardwareRayTracedRadiosity(const FSceneViewFamily& ViewFamily)
{
#if RHI_RAYTRACING
	return IsRayTracingEnabled()
		&& Lumen::UseHardwareRayTracing()
		&& (CVarLumenRadiosityHardwareRayTracing.GetValueOnRenderThread() != 0)
		&& IsRadiosityEnabled(ViewFamily);
#else
	return false;
#endif
}

bool Lumen::IsRadiosityEnabled(const FSceneViewFamily& ViewFamily)
{
	return GLumenRadiosity != 0 
		&& ViewFamily.EngineShowFlags.LumenSecondaryBounces;
}

uint32 Lumen::GetRadiosityDownsampleFactor()
{
	return FMath::RoundUpToPowerOfTwo(FMath::Clamp(GLumenRadiosityDownsampleFactor, 1, 8));
}

uint32 LumenRadiosity::GetNumTracesPerTexel()
{
	return FMath::RoundUpToPowerOfTwo(FMath::Clamp(GRadiosityTracesPerTexel, 1, LumenRadiosity::NumRayDirections));
}

float LumenRadiosity::GetConeHalfAngle()
{
	return FMath::Max(LumenRadiosity::RayDirections.ConeHalfAngle * GLumenRadiosityConeAngleScale, 0.0f);
}

FIntPoint FLumenSceneData::GetRadiosityAtlasSize() const
{
	return FIntPoint::DivideAndRoundDown(PhysicalAtlasSize, Lumen::GetRadiosityDownsampleFactor());
}

class FBuildRadiosityTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildRadiosityTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildRadiosityTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildRadiosityTilesCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityCulling.usf", "BuildRadiosityTilesCS", SF_Compute);

class FSetRadiosityTileIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetRadiosityTileIndirectArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSetRadiosityTileIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardDispatchIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetRadiosityTileIndirectArgsCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityCulling.usf", "SetRadiosityTileIndirectArgs", SF_Compute);

enum class ERadiosityIndirectArgs
{
	ThreadPerTrace = 0 * sizeof(FRHIDispatchIndirectParameters),
	ThreadPerProbeSH = 1 * sizeof(FRHIDispatchIndirectParameters),
	ThreadPerRadiosityTexel = 2 * sizeof(FRHIDispatchIndirectParameters),
	HardwareRayTracingThreadPerTrace = 3 * sizeof(FRHIDispatchIndirectParameters),
	MAX = 4,
};

BEGIN_SHADER_PARAMETER_STRUCT(FLumenRadiosityTexelTraceParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTileData)
	SHADER_PARAMETER_ARRAY(FVector4f, RadiosityRayDirections, [LumenRadiosity::NumRayDirections])
	SHADER_PARAMETER(FIntPoint, RadiosityAtlasSize)
	SHADER_PARAMETER(uint32, NumTracesPerTexel)
	SHADER_PARAMETER(uint32, NumTracesPerTexelModMask)
	SHADER_PARAMETER(uint32, NumTracesPerTexelDivShift)
	SHADER_PARAMETER(float, TanRadiosityRayConeHalfAngle)
END_SHADER_PARAMETER_STRUCT()

class FLumenRadiosityIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityIndirectArgsCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityIndirectArgsCS", SF_Compute);

class FLumenRadiosityDistanceFieldTracingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityDistanceFieldTracingCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityDistanceFieldTracingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER(float, MaxRayIntensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceRadianceBuffer)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceGlobalSDF : SHADER_PERMUTATION_BOOL("TRACE_GLOBAL_SDF");
	using FPermutationDomain = TShaderPermutationDomain<FTraceGlobalSDF>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("ENABLE_DYNAMIC_SKY_LIGHT"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityDistanceFieldTracingCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityDistanceFieldTracingCS", SF_Compute);

#if RHI_RAYTRACING

class FLumenRadiosityHardwareRayTracingRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityHardwareRayTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenRadiosityHardwareRayTracingRGS, FLumenHardwareRayTracingRGS)

	class FIndirectDispatchDim : SHADER_PERMUTATION_BOOL("DIM_INDIRECT_DISPATCH");
	using FPermutationDomain = TShaderPermutationDomain<FIndirectDispatchDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER(uint32, NumThreadsToDispatch)
		SHADER_PARAMETER(float, MinTraceDistance)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, MaxRayIntensity)
		SHADER_PARAMETER(float, MinTraceDistanceToSampleSurface)
		SHADER_PARAMETER(uint32, MaxTraversalIterations)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWTraceRadianceBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, Lumen::ESurfaceCacheSampling::HighResPages, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_DYNAMIC_SKY_LIGHT"), 1);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityHardwareRayTracingRGS, "/Engine/Private/Lumen/Radiosity/LumenRadiosityHardwareRayTracing.usf", "LumenRadiosityHardwareRayTracingRGS", SF_RayGen);

bool IsHardwareRayTracingRadiosityIndirectDispatch()
{
	return GRHISupportsRayTracingDispatchIndirect && (CVarLumenRadiosityHardwareRayTracingIndirect.GetValueOnRenderThread() == 1);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadiosityLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedRadiosity(*View.Family))
	{
		FLumenRadiosityHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadiosityIndirectDispatch());
		TShaderRef<FLumenRadiosityHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadiosityHardwareRayTracingRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}
#endif // #if RHI_RAYTRACING

class FLumenRadiosityMergeTracesToSH : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityMergeTracesToSH)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityMergeTracesToSH, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHRedAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHGreenAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityProbeSHBlueAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadianceBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityMergeTracesToSH, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityMergeTracesToSH", SF_Compute);

class FLumenRadiosityFinalGatherCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadiosityFinalGatherCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadiosityFinalGatherCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenRadiosityTexelTraceParameters, RadiosityTexelTraceParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadiosityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHRedAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHGreenAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityProbeSHBlueAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadianceBuffer)
	END_SHADER_PARAMETER_STRUCT()

	class FUseProbes : SHADER_PERMUTATION_BOOL("USE_PROBES");
	using FPermutationDomain = TShaderPermutationDomain<FUseProbes>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadiosityFinalGatherCS, "/Engine/Private/Lumen/Radiosity/LumenRadiosity.usf", "LumenRadiosityFinalGatherCS", SF_Compute);

void LumenRadiosity::AddRadiosityPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	bool bRenderSkylight,
	FLumenSceneData& LumenSceneData,
	FRDGTextureRef RadiosityAtlas,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardUpdateContext& CardUpdateContext)
{
	LumenRadiosity::RayDirections.GenerateSamples(
		LumenRadiosity::NumRayDirections,
		1,
		LumenRadiosity::NumRayDirections,
		false,
		true /* Cosine distribution */);

	const uint32 NumCardTilesToUpdate = FMath::Max((CardUpdateContext.UpdateAtlasSize.X / Lumen::CardTileSize) * (CardUpdateContext.UpdateAtlasSize.Y / Lumen::CardTileSize), 64u);
	const uint32 RadiosityTileSize = Lumen::CardTileSize / Lumen::GetRadiosityDownsampleFactor();
	const uint32 NumTraceBufferTiles = (NumCardTilesToUpdate * LumenRadiosity::GetNumTracesPerTexel() * RadiosityTileSize * RadiosityTileSize) / (LumenRadiosity::TRACE_BUFFER_MICRO_TILE_SIZE * LumenRadiosity::TRACE_BUFFER_MICRO_TILE_SIZE);

	FIntPoint TraceBufferSize;
	TraceBufferSize.X = LumenRadiosity::TRACE_BUFFER_STRIDE_IN_TILES * LumenRadiosity::TRACE_BUFFER_MICRO_TILE_SIZE;
	TraceBufferSize.Y = ((NumTraceBufferTiles + LumenRadiosity::TRACE_BUFFER_STRIDE_IN_TILES - 1) / LumenRadiosity::TRACE_BUFFER_STRIDE_IN_TILES) * LumenRadiosity::TRACE_BUFFER_MICRO_TILE_SIZE;

	FRDGTextureRef TraceRadianceBuffer = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(TraceBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Lumen.RadiosityTraceRadiance"));

	FIntPoint RadiositySHAtlasSize;
	RadiositySHAtlasSize.X = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, LumenRadiosity::ProbeSHTileSize);
	RadiositySHAtlasSize.Y = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, LumenRadiosity::ProbeSHTileSize);

	FRDGTextureRef RadiosityProbeSHRedAtlas = LumenSceneData.RadiosityProbeSHRedAtlas ? GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityProbeSHRedAtlas) : nullptr;
	FRDGTextureRef RadiosityProbeSHGreenAtlas = LumenSceneData.RadiosityProbeSHGreenAtlas ? GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityProbeSHGreenAtlas) : nullptr;
	FRDGTextureRef RadiosityProbeSHBlueAtlas = LumenSceneData.RadiosityProbeSHBlueAtlas ? GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityProbeSHBlueAtlas) : nullptr;

	if (!RadiosityProbeSHRedAtlas || RadiosityProbeSHRedAtlas->Desc.Extent != RadiositySHAtlasSize)
	{
		RadiosityProbeSHRedAtlas = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(RadiositySHAtlasSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Lumen.RadiosityProbeSHRedAtlas"));
	}

	if (!RadiosityProbeSHGreenAtlas || RadiosityProbeSHGreenAtlas->Desc.Extent != RadiositySHAtlasSize)
	{
		RadiosityProbeSHGreenAtlas = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(RadiositySHAtlasSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Lumen.RadiosityProbeSHGreenAtlas"));
	}

	if (!RadiosityProbeSHBlueAtlas || RadiosityProbeSHBlueAtlas->Desc.Extent != RadiositySHAtlasSize)
	{
		RadiosityProbeSHBlueAtlas = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(RadiositySHAtlasSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Lumen.RadiosityProbeSHBlueAtlas"));
	}

	const uint32 MaxCardTilesX = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, Lumen::CardTileSize);
	const uint32 MaxCardTilesY = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, Lumen::CardTileSize);
	const uint32 MaxCardTiles = MaxCardTilesX * MaxCardTilesY;

	FRDGBufferRef CardTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.Radiosity.CardTileAllocator"));
	FRDGBufferRef CardTiles = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxCardTiles), TEXT("Lumen.Radiosity.CardTiles"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CardTileAllocator), 0);

	// Setup common radiosity tracing parameters
	FLumenRadiosityTexelTraceParameters RadiosityTexelTraceParameters;
	{
		RadiosityTexelTraceParameters.CardTileAllocator = GraphBuilder.CreateSRV(CardTileAllocator);
		RadiosityTexelTraceParameters.CardTileData = GraphBuilder.CreateSRV(CardTiles);
		RadiosityTexelTraceParameters.RadiosityAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
		RadiosityTexelTraceParameters.TanRadiosityRayConeHalfAngle = FMath::Tan(LumenRadiosity::GetConeHalfAngle());
		RadiosityTexelTraceParameters.NumTracesPerTexel = LumenRadiosity::GetNumTracesPerTexel();
		RadiosityTexelTraceParameters.NumTracesPerTexelModMask = (1u << FMath::FloorLog2(RadiosityTexelTraceParameters.NumTracesPerTexel)) - 1;
		RadiosityTexelTraceParameters.NumTracesPerTexelDivShift = FMath::FloorLog2(RadiosityTexelTraceParameters.NumTracesPerTexel);

		int32 NumSampleDirections = 0;
		const FVector4f* SampleDirections = nullptr;
		LumenRadiosity::RayDirections.GetSampleDirections(SampleDirections, NumSampleDirections);
		for (int32 i = 0; i < NumSampleDirections; i++)
		{
			// Scramble ray directions so that we can index them linearly in shader
			RadiosityTexelTraceParameters.RadiosityRayDirections[i] = SampleDirections[(i + 4) % LumenRadiosity::NumRayDirections];
		}
	}

	// Build a list of radiosity tiles for future processing
	{
		FBuildRadiosityTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildRadiosityTilesCS::FParameters>();
		PassParameters->IndirectArgBuffer = CardUpdateContext.DispatchCardPageIndicesIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RWCardTileAllocator = GraphBuilder.CreateUAV(CardTileAllocator);
		PassParameters->RWCardTileData = GraphBuilder.CreateUAV(CardTiles);
		PassParameters->CardPageIndexAllocator = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexAllocator);
		PassParameters->CardPageIndexData = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexData);
		auto ComputeShader = View.ShaderMap->GetShader<FBuildRadiosityTilesCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildRadiosityTiles"),
			ComputeShader,
			PassParameters,
			CardUpdateContext.DispatchCardPageIndicesIndirectArgs,
			FLumenCardUpdateContext::EIndirectArgOffset::ThreadPerTile);
	}

	FRDGBufferRef RadiosityIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>((int)ERadiosityIndirectArgs::MAX), TEXT("Lumen.RadiosityIndirectArgs"));

	// Setup indirect args for future passes
	{
		FLumenRadiosityIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityIndirectArgsCS::FParameters>();
		PassParameters->RWIndirectArgs = GraphBuilder.CreateUAV(RadiosityIndirectArgs);
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityIndirectArgsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("IndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	// Trace rays from surface cache texels
	if (Lumen::UseHardwareRayTracedRadiosity(*View.Family))
	{
#if RHI_RAYTRACING

		FLumenRadiosityHardwareRayTracingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityHardwareRayTracingRGS::FParameters>();
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			GetSceneTextureParameters(GraphBuilder),
			View,
			TracingInputs,
			&PassParameters->SharedParameters
		);
		PassParameters->HardwareRayTracingIndirectArgs = RadiosityIndirectArgs;

		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWTraceRadianceBuffer = GraphBuilder.CreateUAV(TraceRadianceBuffer);

		const uint32 NumThreadsToDispatch = GRHIPersistentThreadGroupCount * FLumenRadiosityHardwareRayTracingRGS::GetGroupSize();
		PassParameters->NumThreadsToDispatch = NumThreadsToDispatch;
		PassParameters->SurfaceBias = FMath::Clamp(GLumenRadiosityHardwareRayTracingSurfaceSlopeBias, 0.0f, 1000.0f);
		PassParameters->MaxRayIntensity = FMath::Clamp(GLumenRadiosityMaxRayIntensity, 0.0f, 1000000.0f);
		PassParameters->MinTraceDistance = FMath::Clamp(GLumenRadiosityHardwareRayTracingSurfaceBias, 0.0f, 1000.0f);
		PassParameters->MaxTraceDistance = Lumen::GetMaxTraceDistance();
		PassParameters->MinTraceDistanceToSampleSurface = GLumenRadiosityMinTraceDistanceToSampleSurface;
		PassParameters->MaxTraversalIterations = LumenHardwareRayTracing::GetMaxTraversalIterations();

		FLumenRadiosityHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityHardwareRayTracingRGS::FIndirectDispatchDim>(IsHardwareRayTracingRadiosityIndirectDispatch());
		TShaderRef<FLumenRadiosityHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadiosityHardwareRayTracingRGS>(PermutationVector);

		const FIntPoint DispatchResolution = FIntPoint(NumThreadsToDispatch, 1);
		FString Resolution = FString::Printf(TEXT("%ux%u"), DispatchResolution.X, DispatchResolution.Y);
		if (IsHardwareRayTracingRadiosityIndirectDispatch())
		{
			Resolution = FString::Printf(TEXT("<indirect>"));
		}
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HardwareRayTracing %s", *Resolution),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHIRayTracingCommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				FRayTracingPipelineState* RayTracingPipeline = View.LumenHardwareRayTracingMaterialPipeline;

				if (IsHardwareRayTracingRadiosityIndirectDispatch())
				{
					PassParameters->HardwareRayTracingIndirectArgs->MarkResourceAsUsed();
					RHICmdList.RayTraceDispatchIndirect(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						PassParameters->HardwareRayTracingIndirectArgs->GetIndirectRHICallBuffer(), (int)ERadiosityIndirectArgs::HardwareRayTracingThreadPerTrace);
				}
				else
				{
					RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
						DispatchResolution.X, DispatchResolution.Y);
				}
			}
		);
#endif
	}
	else
	{
		FLumenRadiosityDistanceFieldTracingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityDistanceFieldTracingCS::FParameters>();
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWTraceRadianceBuffer = GraphBuilder.CreateUAV(TraceRadianceBuffer);

		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, LumenRadiosity::GetConeHalfAngle());
		PassParameters->IndirectTracingParameters.StepFactor = FMath::Clamp(GRadiosityTraceStepFactor, 0.1f, 10.0f);
		PassParameters->IndirectTracingParameters.MinSampleRadius = FMath::Clamp(GLumenRadiosityMinSampleRadius, 0.01f, 100.0f);
		PassParameters->IndirectTracingParameters.SurfaceBias = FMath::Clamp(GLumenRadiosityDistanceFieldSurfaceSlopeBias, 0.0f, 1000.0f);
		PassParameters->IndirectTracingParameters.MinTraceDistance = FMath::Clamp(GLumenRadiosityDistanceFieldSurfaceBias, 0.0f, 1000.0f);
		PassParameters->IndirectTracingParameters.MaxTraceDistance = Lumen::GetMaxTraceDistance();
		PassParameters->IndirectTracingParameters.VoxelStepFactor = FMath::Clamp(GLumenRadiosityVoxelStepFactor, 0.1f, 10.0f);
		PassParameters->MaxRayIntensity = FMath::Clamp(GLumenRadiosityMaxRayIntensity, 0.0f, 1000000.0f);

		FLumenRadiosityDistanceFieldTracingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityDistanceFieldTracingCS::FTraceGlobalSDF>(Lumen::UseGlobalSDFTracing(*View.Family));
		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityDistanceFieldTracingCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DistanceFieldTracing"),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerTrace);
	}

	// Merge rays into a persistent SH atlas
	if (GRadiosityDenoising != 0)
	{
		FLumenRadiosityMergeTracesToSH::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityMergeTracesToSH::FParameters>();
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->TraceRadianceBuffer = TraceRadianceBuffer;
		PassParameters->RWRadiosityProbeSHRedAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHRedAtlas);
		PassParameters->RWRadiosityProbeSHGreenAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHGreenAtlas);
		PassParameters->RWRadiosityProbeSHBlueAtlas = GraphBuilder.CreateUAV(RadiosityProbeSHBlueAtlas);

		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityMergeTracesToSH>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MergeTracesToSH"),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerProbeSH);
	}

	// Final Gather
	{
		FLumenRadiosityFinalGatherCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadiosityFinalGatherCS::FParameters>();
		PassParameters->IndirectArgs = RadiosityIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->RadiosityTexelTraceParameters = RadiosityTexelTraceParameters;
		PassParameters->RWRadiosityAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadiosityAtlas));
		PassParameters->RadiosityProbeSHRedAtlas = RadiosityProbeSHRedAtlas;
		PassParameters->RadiosityProbeSHGreenAtlas = RadiosityProbeSHGreenAtlas;
		PassParameters->RadiosityProbeSHBlueAtlas = RadiosityProbeSHBlueAtlas;
		PassParameters->TraceRadianceBuffer = TraceRadianceBuffer;

		FLumenRadiosityFinalGatherCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenRadiosityFinalGatherCS::FUseProbes>(GRadiosityDenoising != 0);
		auto ComputeShader = View.ShaderMap->GetShader<FLumenRadiosityFinalGatherCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FinalGather"),
			ComputeShader,
			PassParameters,
			RadiosityIndirectArgs,
			(int)ERadiosityIndirectArgs::ThreadPerRadiosityTexel);
	}

	LumenSceneData.RadiosityProbeSHRedAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHRedAtlas);
	LumenSceneData.RadiosityProbeSHGreenAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHGreenAtlas);
	LumenSceneData.RadiosityProbeSHBlueAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityProbeSHBlueAtlas);
}

void FDeferredShadingSceneRenderer::RenderRadiosityForLumenScene(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	FGlobalShaderMap* GlobalShaderMap, 
	FRDGTextureRef RadiosityAtlas,
	const FLumenCardUpdateContext& CardUpdateContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FViewInfo& View = Views[0];
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	extern int32 GLumenSceneRecaptureLumenSceneEveryFrame;

	if (Lumen::IsRadiosityEnabled(ViewFamily)
		&& !GLumenSceneRecaptureLumenSceneEveryFrame
		&& LumenSceneData.bFinalLightingAtlasContentsValid
		&& (Lumen::UseHardwareRayTracedRadiosity(ViewFamily) || TracingInputs.NumClipmapLevels > 0))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Radiosity");

		const bool bRenderSkylight = Lumen::ShouldHandleSkyLight(Scene, ViewFamily);

		LumenRadiosity::AddRadiosityPass(
			GraphBuilder,
			Scene,
			View,
			bRenderSkylight,
			LumenSceneData,
			RadiosityAtlas,
			TracingInputs,
			CardUpdateContext);

		// Update Final Lighting
		Lumen::CombineLumenSceneLighting(
			Scene,
			View,
			GraphBuilder,
			TracingInputs,
			CardUpdateContext);
	}
	else
	{
		AddClearRenderTargetPass(GraphBuilder, RadiosityAtlas);
	}
}
