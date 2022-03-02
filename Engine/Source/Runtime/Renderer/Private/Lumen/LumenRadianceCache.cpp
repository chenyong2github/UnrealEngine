// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadianceCache.cpp
=============================================================================*/

#include "LumenRadianceCache.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenScreenProbeGather.h"
#include "ShaderPrintParameters.h"

int32 GRadianceCacheUpdate = 1;
FAutoConsoleVariableRef CVarRadianceCacheUpdate(
	TEXT("r.Lumen.RadianceCache.Update"),
	GRadianceCacheUpdate,
	TEXT("Whether to update radiance cache every frame"),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheForceFullUpdate = 0;
FAutoConsoleVariableRef CVarRadianceForceFullUpdate(
	TEXT("r.Lumen.RadianceCache.ForceFullUpdate"),
	GRadianceCacheForceFullUpdate,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceNumFramesToKeepCachedProbes = 2;
FAutoConsoleVariableRef CVarRadianceCacheNumFramesToKeepCachedProbes(
	TEXT("r.Lumen.RadianceCache.NumFramesToKeepCachedProbes"),
	GRadianceNumFramesToKeepCachedProbes,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheOverrideCacheOcclusionLighting = 0;
FAutoConsoleVariableRef CVarRadianceCacheShowOnlyRadianceCacheLighting(
	TEXT("r.Lumen.RadianceCache.OverrideCacheOcclusionLighting"),
	GRadianceCacheOverrideCacheOcclusionLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheShowBlackRadianceCacheLighting = 0;
FAutoConsoleVariableRef CVarRadianceCacheShowBlackRadianceCacheLighting(
	TEXT("r.Lumen.RadianceCache.ShowBlackRadianceCacheLighting"),
	GRadianceCacheShowBlackRadianceCacheLighting,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheFilterProbes = 1;
FAutoConsoleVariableRef CVarRadianceCacheFilterProbes(
	TEXT("r.Lumen.RadianceCache.SpatialFilterProbes"),
	GRadianceCacheFilterProbes,
	TEXT("Whether to filter probe radiance between neighbors"),
	ECVF_RenderThreadSafe
);

int32 GRadianceCacheSortTraceTiles = 0;
FAutoConsoleVariableRef CVarRadianceCacheSortTraceTiles(
	TEXT("r.Lumen.RadianceCache.SortTraceTiles"),
	GRadianceCacheSortTraceTiles,
	TEXT("Whether to sort Trace Tiles by direction before tracing to extract coherency"),
	ECVF_RenderThreadSafe
);

float GLumenRadianceCacheFilterMaxRadianceHitAngle = .2f;
FAutoConsoleVariableRef GVarLumenRadianceCacheFilterMaxRadianceHitAngle(
	TEXT("r.Lumen.RadianceCache.SpatialFilterMaxRadianceHitAngle"),
	GLumenRadianceCacheFilterMaxRadianceHitAngle,
	TEXT("In Degrees.  Larger angles allow filtering of nearby features but more leaking."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadianceCacheSupersampleTileBRDFThreshold = .1f;
FAutoConsoleVariableRef CVarLumenRadianceCacheSupersampleTileBRDFThreshold(
	TEXT("r.Lumen.RadianceCache.SupersampleTileBRDFThreshold"),
	GLumenRadianceCacheSupersampleTileBRDFThreshold,
	TEXT("Value of the BRDF [0-1] above which to trace more rays to supersample the probe radiance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadianceCacheSupersampleDistanceFromCamera = 2000.0f;
FAutoConsoleVariableRef CVarLumenRadianceCacheSupersampleDistanceFromCamera(
	TEXT("r.Lumen.RadianceCache.SupersampleDistanceFromCamera"),
	GLumenRadianceCacheSupersampleDistanceFromCamera,
	TEXT("Only probes closer to the camera than this distance can be supersampled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenRadianceCacheDownsampleDistanceFromCamera = 4000.0f;
FAutoConsoleVariableRef CVarLumenRadianceCacheDownsampleDistanceFromCamera(
	TEXT("r.Lumen.RadianceCache.DownsampleDistanceFromCamera"),
	GLumenRadianceCacheDownsampleDistanceFromCamera,
	TEXT("Probes further than this distance from the camera are always downsampled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace LumenRadianceCache
{
	// Must match LumenRadianceCacheCommon.ush
	constexpr uint32 PRIORITY_HISTOGRAM_SIZE = 128;
	constexpr uint32 PROBES_TO_UPDATE_TRACE_COST_STRIDE = 2;

	FRadianceCacheInputs GetDefaultRadianceCacheInputs()
	{
		FRadianceCacheInputs RadianceCacheInputs;
		RadianceCacheInputs.CalculateIrradiance = 0;
		RadianceCacheInputs.IrradianceProbeResolution = 0;
		RadianceCacheInputs.InvClipmapFadeSize = 1.0f;
		return RadianceCacheInputs;
	}

	void GetInterpolationParametersNoResources(
		FRDGBuilder& GraphBuilder, 
		const FRadianceCacheState& RadianceCacheState,
		const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs, 
		FRadianceCacheInterpolationParameters& OutParameters)
	{
		OutParameters.RadianceCacheInputs = RadianceCacheInputs;
		OutParameters.RadianceCacheInputs.NumProbesToTraceBudget = GRadianceCacheForceFullUpdate ? 1000000 : OutParameters.RadianceCacheInputs.NumProbesToTraceBudget;
		OutParameters.RadianceProbeIndirectionTexture = nullptr;
		OutParameters.RadianceCacheFinalRadianceAtlas = nullptr;
		OutParameters.RadianceCacheFinalIrradianceAtlas = nullptr;
		OutParameters.RadianceCacheProbeOcclusionAtlas = nullptr;
		OutParameters.RadianceCacheDepthAtlas = nullptr;
		OutParameters.ProbeWorldOffset = nullptr;
		OutParameters.OverrideCacheOcclusionLighting = GRadianceCacheOverrideCacheOcclusionLighting;
		OutParameters.ShowBlackRadianceCacheLighting = GRadianceCacheShowBlackRadianceCacheLighting;
		OutParameters.ProbeAtlasResolutionModuloMask = (1u << FMath::FloorLog2(RadianceCacheInputs.ProbeAtlasResolutionInProbes.X)) - 1;
		OutParameters.ProbeAtlasResolutionDivideShift = FMath::FloorLog2(RadianceCacheInputs.ProbeAtlasResolutionInProbes.X);

		for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
		{
			const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];

			SetRadianceProbeClipmapTMin(OutParameters, ClipmapIndex, Clipmap.ProbeTMin);
			SetWorldPositionToRadianceProbeCoordScale(OutParameters, ClipmapIndex, Clipmap.WorldPositionToProbeCoordScale);
			SetWorldPositionToRadianceProbeCoordBias(OutParameters, ClipmapIndex, (FVector3f)Clipmap.WorldPositionToProbeCoordBias);
			SetRadianceProbeCoordToWorldPositionScale(OutParameters, ClipmapIndex, Clipmap.ProbeCoordToWorldCenterScale);
			SetRadianceProbeCoordToWorldPositionBias(OutParameters, ClipmapIndex, (FVector3f)Clipmap.ProbeCoordToWorldCenterBias);
		}

		const FVector2f ProbeAtlasResolutionInProbesAsFloat = FVector2f(RadianceCacheInputs.ProbeAtlasResolutionInProbes);
		OutParameters.InvProbeFinalRadianceAtlasResolution = FVector2f::UnitVector / (RadianceCacheInputs.FinalProbeResolution * ProbeAtlasResolutionInProbesAsFloat);	// LWC_TODO: Fix! Used to be FVector2D(RadianceCacheInputs.FinalProbeResolution * RadianceCacheInputs.ProbeAtlasResolutionInProbes). No auto conversion of ProbeAtlastResolutionInProbes to FVector2D. ADL thing?
		const int32 FinalIrradianceProbeResolution = RadianceCacheInputs.IrradianceProbeResolution + 2 * (1 << RadianceCacheInputs.FinalRadianceAtlasMaxMip);
		OutParameters.InvProbeFinalIrradianceAtlasResolution = FVector2f::UnitVector / (FinalIrradianceProbeResolution * ProbeAtlasResolutionInProbesAsFloat);
		OutParameters.InvProbeDepthAtlasResolution = FVector2f::UnitVector / (RadianceCacheInputs.RadianceProbeResolution * ProbeAtlasResolutionInProbesAsFloat);
	}

	void GetInterpolationParameters(
		const FViewInfo& View, 
		FRDGBuilder& GraphBuilder, 
		const FRadianceCacheState& RadianceCacheState,
		const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs,
		FRadianceCacheInterpolationParameters& OutParameters)
	{
		GetInterpolationParametersNoResources(GraphBuilder, RadianceCacheState, RadianceCacheInputs, OutParameters);

		OutParameters.RadianceProbeIndirectionTexture = RadianceCacheState.RadianceProbeIndirectionTexture ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeIndirectionTexture, TEXT("Lumen.RadianceCacheIndirectionTexture")) : nullptr;
		OutParameters.RadianceCacheFinalRadianceAtlas = RadianceCacheState.FinalRadianceAtlas ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalRadianceAtlas, TEXT("Lumen.RadianceCacheFinalRadianceAtlas")) : nullptr;
		OutParameters.RadianceCacheFinalIrradianceAtlas = RadianceCacheState.FinalIrradianceAtlas ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalIrradianceAtlas, TEXT("Lumen.RadianceCacheFinalIrradianceAtlas")) : nullptr;
		OutParameters.RadianceCacheProbeOcclusionAtlas = RadianceCacheState.ProbeOcclusionAtlas ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.ProbeOcclusionAtlas, TEXT("Lumen.RadianceCacheProbeOcclusionAtlas")) : nullptr;
		OutParameters.RadianceCacheDepthAtlas = RadianceCacheState.DepthProbeAtlasTexture ? GraphBuilder.RegisterExternalTexture(RadianceCacheState.DepthProbeAtlasTexture, TEXT("Lumen.RadianceCacheDepthAtlas")) : nullptr;
		FRDGBufferRef ProbeWorldOffset = RadianceCacheState.ProbeWorldOffset ? GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeWorldOffset) : nullptr;
		OutParameters.ProbeWorldOffset = ProbeWorldOffset ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeWorldOffset, PF_A32B32G32R32F)) : nullptr;
	}

	FRadianceCacheMarkParameters GetMarkParameters(
		FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV, 
		const FRadianceCacheState& RadianceCacheState, 
		const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs)
	{
		FRadianceCacheMarkParameters MarkParameters;
		MarkParameters.RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;

		for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
		{
			const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];

			SetWorldPositionToRadianceProbeCoord(MarkParameters.PackedWorldPositionToRadianceProbeCoord[ClipmapIndex], (FVector3f)Clipmap.WorldPositionToProbeCoordBias, Clipmap.WorldPositionToProbeCoordScale);
			SetRadianceProbeCoordToWorldPosition(MarkParameters.PackedRadianceProbeCoordToWorldPosition[ClipmapIndex], (FVector3f)Clipmap.ProbeCoordToWorldCenterBias, Clipmap.ProbeCoordToWorldCenterScale);
		}

		MarkParameters.RadianceProbeClipmapResolutionForMark = RadianceCacheInputs.RadianceProbeClipmapResolution;
		MarkParameters.NumRadianceProbeClipmapsForMark = RadianceCacheInputs.NumRadianceProbeClipmaps;
		MarkParameters.InvClipmapFadeSizeForMark = RadianceCacheInputs.InvClipmapFadeSize;

		return MarkParameters;
	}
};

class FMarkRadianceProbesUsedByVisualizeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkRadianceProbesUsedByVisualizeCS)
	SHADER_USE_PARAMETER_STRUCT(FMarkRadianceProbesUsedByVisualizeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheMarkParameters, RadianceCacheMarkParameters)
		END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkRadianceProbesUsedByVisualizeCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "MarkRadianceProbesUsedByVisualizeCS", SF_Compute);

void MarkUsedProbesForVisualize(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const LumenRadianceCache::FRadianceCacheMarkParameters& RadianceCacheMarkParameters)
{
	extern int32 GVisualizeLumenSceneTraceRadianceCache;

	if (View.Family->EngineShowFlags.VisualizeLumen && GVisualizeLumenSceneTraceRadianceCache != 0)
	{
		FMarkRadianceProbesUsedByVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkRadianceProbesUsedByVisualizeCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RadianceCacheMarkParameters = RadianceCacheMarkParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FMarkRadianceProbesUsedByVisualizeCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MarkRadianceProbes(Visualize)"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}
}

class FClearProbeFreeList : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearProbeFreeList)
	SHADER_USE_PARAMETER_STRUCT(FClearProbeFreeList, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, RWProbeWorldOffset)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearProbeFreeList, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "ClearProbeFreeListCS", SF_Compute);

class FClearProbeIndirectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearProbeIndirectionCS)
	SHADER_USE_PARAMETER_STRUCT(FClearProbeIndirectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearProbeIndirectionCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClearProbeIndirectionCS", SF_Compute);

class FUpdateCacheForUsedProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpdateCacheForUsedProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FUpdateCacheForUsedProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, LastFrameRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_ARRAY(FVector4f, PackedLastFrameRadianceProbeCoordToWorldPosition, [LumenRadianceCache::MaxClipmaps])
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, NumFramesToKeepCachedProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FUpdateCacheForUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "UpdateCacheForUsedProbesCS", SF_Compute);

class FClearRadianceCacheUpdateResourcesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearRadianceCacheUpdateResourcesCS);
	SHADER_USE_PARAMETER_STRUCT(FClearRadianceCacheUpdateResourcesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPriorityHistogram)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxTracesFromMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWProbesToUpdateTraceCost)
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

public:

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearRadianceCacheUpdateResourcesCS, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "ClearRadianceCacheUpdateResourcesCS", SF_Compute);

class FAllocateUsedProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateUsedProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FAllocateUsedProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWProbeLastTracedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPriorityHistogram)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeFreeList)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, DownsampleDistanceFromCameraSq)
		SHADER_PARAMETER(float, SupersampleDistanceFromCameraSq)
		SHADER_PARAMETER(float, FirstClipmapWorldExtentRcp)
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, MaxNumProbes)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FPersistentCache : SHADER_PERMUTATION_BOOL("PERSISTENT_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FPersistentCache>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

public:

	static uint32 GetGroupSize()
	{
		return 4;
	}
};

IMPLEMENT_GLOBAL_SHADER(FAllocateUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "AllocateUsedProbesCS", SF_Compute);

class FAllocateProbeTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateProbeTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FAllocateProbeTracesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWProbeLastTracedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWProbesToUpdateTraceCost)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxTracesFromMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeFreeList)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, FirstClipmapWorldExtentRcp)
		SHADER_PARAMETER(float, DownsampleDistanceFromCameraSq)
		SHADER_PARAMETER(float, SupersampleDistanceFromCameraSq)
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, MaxNumProbes)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
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

public:

	static uint32 GetGroupSize()
	{
		return 4;
	}
};

IMPLEMENT_GLOBAL_SHADER(FAllocateProbeTracesCS, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "AllocateProbeTracesCS", SF_Compute);

class FSelectMaxPriorityBucketCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSelectMaxPriorityBucketCS)
	SHADER_USE_PARAMETER_STRUCT(FSelectMaxPriorityBucketCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxTracesFromMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PriorityHistogram)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceAllocator)
		SHADER_PARAMETER(uint32, NumProbesToTraceBudget)
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

public:

	static uint32 GetGroupSize()
	{
		return 1;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSelectMaxPriorityBucketCS, "/Engine/Private/Lumen/LumenRadianceCacheUpdate.usf", "SelectMaxPriorityBucketCS", SF_Compute);

class FRadianceCacheUpdateStatsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRadianceCacheUpdateStatsCS)
	SHADER_USE_PARAMETER_STRUCT(FRadianceCacheUpdateStatsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PriorityHistogram)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxTracesFromMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbesToUpdateTraceCost)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeFreeListAllocator)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 1);
	}

public:

	static uint32 GetGroupSize()
	{
		return 1;
	}
};

IMPLEMENT_GLOBAL_SHADER(FRadianceCacheUpdateStatsCS, "/Engine/Private/Lumen/LumenRadianceCacheDebug.usf", "RadianceCacheUpdateStatsCS", SF_Compute);

class FSetupProbeIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupProbeIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupProbeIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWClearProbePDFsIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGenerateProbeTraceTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFilterProbesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPrepareProbeOcclusionIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFixupProbeBordersIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER(uint32, TraceFromProbesGroupSizeXY)
		SHADER_PARAMETER(uint32, FilterProbesGroupSizeXY)
		SHADER_PARAMETER(uint32, ClearProbePDFGroupSize)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupProbeIndirectArgsCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "SetupProbeIndirectArgsCS", SF_Compute);


class FComputeProbeWorldOffsetsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeProbeWorldOffsetsCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeProbeWorldOffsetsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWProbeWorldOffset)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeProbeWorldOffsetsCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ComputeProbeWorldOffsetsCS", SF_Compute);


class FClearProbePDFs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearProbePDFs)
	SHADER_USE_PARAMETER_STRUCT(FClearProbePDFs, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRadianceProbeSH_PDF)
		RDG_BUFFER_ACCESS(ClearProbePDFsIndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearProbePDFs, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClearProbePDFs", SF_Compute);


class FScatterScreenProbeBRDFToRadianceProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScatterScreenProbeBRDFToRadianceProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FScatterScreenProbeBRDFToRadianceProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRadianceProbeSH_PDF)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, BRDFProbabilityDensityFunctionSH)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FScatterScreenProbeBRDFToRadianceProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ScatterScreenProbeBRDFToRadianceProbesCS", SF_Compute);

class FGenerateProbeTraceTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateProbeTraceTilesCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateProbeTraceTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, RadianceProbeSH_PDF)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbesToUpdateTraceCost)
		SHADER_PARAMETER(float, SupersampleTileBRDFThreshold)
		SHADER_PARAMETER(float, SupersampleDistanceFromCameraSq)
		SHADER_PARAMETER(float, DownsampleDistanceFromCameraSq)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDebugBRDFProbabilityDensityFunction)
		SHADER_PARAMETER(uint32, DebugProbeBRDFOctahedronResolution)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(GenerateProbeTraceTilesIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	class FUniformTraces : SHADER_PERMUTATION_BOOL("FORCE_UNIFORM_TRACES");
	using FPermutationDomain = TShaderPermutationDomain<FUniformTraces>;
	
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

IMPLEMENT_GLOBAL_SHADER(FGenerateProbeTraceTilesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "GenerateProbeTraceTilesCS", SF_Compute);


class FSetupTraceFromProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupTraceFromProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupTraceFromProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceProbesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWSortProbeTraceTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRadianceCacheHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingRayAllocatorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER(uint32, SortTraceTilesGroupSize)
	END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupTraceFromProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "SetupTraceFromProbesCS", SF_Compute);


class FSortProbeTraceTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSortProbeTraceTilesCS)
	SHADER_USE_PARAMETER_STRUCT(FSortProbeTraceTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInputs, RadianceCacheInputs)
		RDG_BUFFER_ACCESS(SortProbeTraceTilesIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		// Group size affects sorting window, the larger the group the more coherency can be extracted
		return 1024;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SORT_TILES_THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FSortProbeTraceTilesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "SortProbeTraceTilesCS", SF_Compute);



class FRadianceCacheTraceFromProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRadianceCacheTraceFromProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FRadianceCacheTraceFromProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWDepthProbeAtlasTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(TraceProbesIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceGlobalSDF : SHADER_PERMUTATION_BOOL("TRACE_GLOBAL_SDF");
	class FDistantScene : SHADER_PERMUTATION_BOOL("TRACE_DISTANT_SCENE");
	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");

	using FPermutationDomain = TShaderPermutationDomain<FTraceGlobalSDF, FDistantScene, FDynamicSkyLight>;

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		// Must match RADIANCE_CACHE_TRACE_TILE_SIZE_2D
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FRadianceCacheTraceFromProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "TraceFromProbesCS", SF_Compute);


class FFilterProbeRadianceWithGatherCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFilterProbeRadianceWithGatherCS)
	SHADER_USE_PARAMETER_STRUCT(FFilterProbeRadianceWithGatherCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(FilterProbesIndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(float, SpatialFilterMaxRadianceHitAngle)
	END_SHADER_PARAMETER_STRUCT()

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

IMPLEMENT_GLOBAL_SHADER(FFilterProbeRadianceWithGatherCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "FilterProbeRadianceWithGatherCS", SF_Compute);


class FCalculateProbeIrradianceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCalculateProbeIrradianceCS)
	SHADER_USE_PARAMETER_STRUCT(FCalculateProbeIrradianceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalIrradianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FOctahedralSolidAngleParameters, OctahedralSolidAngleParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		RDG_BUFFER_ACCESS(CalculateProbeIrradianceIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FCalculateProbeIrradianceCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "CalculateProbeIrradianceCS", SF_Compute);


class FPrepareProbeOcclusionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPrepareProbeOcclusionCS)
	SHADER_USE_PARAMETER_STRUCT(FPrepareProbeOcclusionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWRadianceCacheProbeOcclusionAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(PrepareProbeOcclusionIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FPrepareProbeOcclusionCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "PrepareProbeOcclusionCS", SF_Compute);


class FFixupBordersAndGenerateMipsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFixupBordersAndGenerateMipsCS)
	SHADER_USE_PARAMETER_STRUCT(FFixupBordersAndGenerateMipsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalRadianceAtlasMip0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalRadianceAtlasMip1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalRadianceAtlasMip2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(FixupProbeBordersIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:

	class FGenerateMips : SHADER_PERMUTATION_BOOL("GENERATE_MIPS");
	using FPermutationDomain = TShaderPermutationDomain<FGenerateMips>;

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
	}
};

IMPLEMENT_GLOBAL_SHADER(FFixupBordersAndGenerateMipsCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "FixupBordersAndGenerateMipsCS", SF_Compute);

bool UpdateRadianceCacheState(FRDGBuilder& GraphBuilder, const FViewInfo& View, const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs, FRadianceCacheState& CacheState)
{
	bool bResetState = CacheState.ClipmapWorldExtent != RadianceCacheInputs.ClipmapWorldExtent || CacheState.ClipmapDistributionBase != RadianceCacheInputs.ClipmapDistributionBase;

	CacheState.ClipmapWorldExtent = RadianceCacheInputs.ClipmapWorldExtent;
	CacheState.ClipmapDistributionBase = RadianceCacheInputs.ClipmapDistributionBase;

	const int32 ClipmapResolution = RadianceCacheInputs.RadianceProbeClipmapResolution;
	const int32 NumClipmaps = RadianceCacheInputs.NumRadianceProbeClipmaps;

	const FVector NewViewOrigin = View.ViewMatrices.GetViewOrigin();

	CacheState.Clipmaps.SetNum(NumClipmaps);

	for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ++ClipmapIndex)
	{
		FRadianceCacheClipmap& Clipmap = CacheState.Clipmaps[ClipmapIndex];

		const float ClipmapExtent = RadianceCacheInputs.ClipmapWorldExtent * FMath::Pow(RadianceCacheInputs.ClipmapDistributionBase, ClipmapIndex);
		const float CellSize = (2.0f * ClipmapExtent) / ClipmapResolution;

		FIntVector GridCenter;
		GridCenter.X = FMath::FloorToInt(NewViewOrigin.X / CellSize);
		GridCenter.Y = FMath::FloorToInt(NewViewOrigin.Y / CellSize);
		GridCenter.Z = FMath::FloorToInt(NewViewOrigin.Z / CellSize);

		const FVector SnappedCenter = FVector(GridCenter) * CellSize;

		Clipmap.Center = SnappedCenter;
		Clipmap.Extent = ClipmapExtent;
		Clipmap.VolumeUVOffset = FVector(0.0f, 0.0f, 0.0f);
		Clipmap.CellSize = CellSize;

		// Shift the clipmap grid down so that probes align with other clipmaps
		const FVector ClipmapMin = Clipmap.Center - Clipmap.Extent - 0.5f * Clipmap.CellSize;

		Clipmap.ProbeCoordToWorldCenterBias = ClipmapMin + 0.5f * Clipmap.CellSize;
		Clipmap.ProbeCoordToWorldCenterScale = Clipmap.CellSize;

		Clipmap.WorldPositionToProbeCoordScale = 1.0f / CellSize;
		Clipmap.WorldPositionToProbeCoordBias = -ClipmapMin / CellSize;
		
		Clipmap.ProbeTMin = RadianceCacheInputs.CalculateIrradiance ? 0.0f : FVector(CellSize, CellSize, CellSize).Size();
	}

	return bResetState;
}

void RenderRadianceCache(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs,
	FRadianceCacheConfiguration Configuration,
	const FScene* Scene,
	const FViewInfo& View, 
	const FScreenProbeParameters* ScreenProbeParameters,
	FRDGBufferSRVRef BRDFProbabilityDensityFunctionSH,
	FMarkUsedRadianceCacheProbes MarkUsedRadianceCacheProbes,
	FRadianceCacheState& RadianceCacheState,
	LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters)
{
	if (GRadianceCacheUpdate != 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "RadianceCache");

		const TArray<FRadianceCacheClipmap> LastFrameClipmaps = RadianceCacheState.Clipmaps;
		bool bResizedHistoryState = UpdateRadianceCacheState(GraphBuilder, View, RadianceCacheInputs, RadianceCacheState);

		const FIntPoint RadianceProbeAtlasTextureSize(RadianceCacheInputs.ProbeAtlasResolutionInProbes * RadianceCacheInputs.RadianceProbeResolution);

		FRDGTextureRef DepthProbeAtlasTexture = nullptr;

		if (RadianceCacheState.DepthProbeAtlasTexture.IsValid()
			&& RadianceCacheState.DepthProbeAtlasTexture->GetDesc().Extent == RadianceProbeAtlasTextureSize)
		{
			DepthProbeAtlasTexture = GraphBuilder.RegisterExternalTexture(RadianceCacheState.DepthProbeAtlasTexture);
		}
		else
		{
			FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
				RadianceProbeAtlasTextureSize,
				PF_R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			DepthProbeAtlasTexture = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("Lumen.RadianceCache.DepthProbeAtlasTexture"));
			bResizedHistoryState = true;
		}

		FRDGTextureRef FinalIrradianceAtlas = nullptr;
		FRDGTextureRef ProbeOcclusionAtlas = nullptr;
		FRDGTextureRef FinalRadianceAtlas = nullptr;

		if (RadianceCacheInputs.CalculateIrradiance)
		{
			const FIntPoint FinalIrradianceAtlasSize(RadianceCacheInputs.ProbeAtlasResolutionInProbes * (RadianceCacheInputs.IrradianceProbeResolution + 2 * (1 << RadianceCacheInputs.FinalRadianceAtlasMaxMip)));

			if (RadianceCacheState.FinalIrradianceAtlas.IsValid()
				&& RadianceCacheState.FinalIrradianceAtlas->GetDesc().Extent == FinalIrradianceAtlasSize
				&& RadianceCacheState.FinalIrradianceAtlas->GetDesc().NumMips == RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1)
			{
				FinalIrradianceAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalIrradianceAtlas);
			}
			else
			{
				FRDGTextureDesc FinalRadianceAtlasDesc = FRDGTextureDesc::Create2D(
					FinalIrradianceAtlasSize,
					PF_FloatRGB,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV,
					RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1);

				FinalIrradianceAtlas = GraphBuilder.CreateTexture(FinalRadianceAtlasDesc, TEXT("Lumen.RadianceCache.FinalIrradianceAtlas"));
				bResizedHistoryState = true;
			}

			if (GRadianceCacheForceFullUpdate)
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalIrradianceAtlas)), FLinearColor::Black);
			}

			const FIntPoint ProbeOcclusionAtlasSize(RadianceCacheInputs.ProbeAtlasResolutionInProbes * (RadianceCacheInputs.OcclusionProbeResolution + 2 * (1 << RadianceCacheInputs.FinalRadianceAtlasMaxMip)));

			if (RadianceCacheState.ProbeOcclusionAtlas.IsValid()
				&& RadianceCacheState.ProbeOcclusionAtlas->GetDesc().Extent == ProbeOcclusionAtlasSize
				&& RadianceCacheState.ProbeOcclusionAtlas->GetDesc().NumMips == RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1)
			{
				ProbeOcclusionAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.ProbeOcclusionAtlas);
			}
			else
			{
				FRDGTextureDesc ProbeOcclusionAtlasDesc = FRDGTextureDesc::Create2D(
					ProbeOcclusionAtlasSize,
					PF_G16R16F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV,
					RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1);

				ProbeOcclusionAtlas = GraphBuilder.CreateTexture(ProbeOcclusionAtlasDesc, TEXT("Lumen.RadianceCache.ProbeOcclusionAtlas"));
				bResizedHistoryState = true;
			}
		}
		else
		{
			const FIntPoint FinalRadianceAtlasSize(RadianceCacheInputs.ProbeAtlasResolutionInProbes * RadianceCacheInputs.FinalProbeResolution);

			if (RadianceCacheState.FinalRadianceAtlas.IsValid()
				&& RadianceCacheState.FinalRadianceAtlas->GetDesc().Extent == FinalRadianceAtlasSize
				&& RadianceCacheState.FinalRadianceAtlas->GetDesc().NumMips == RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1)
			{
				FinalRadianceAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.FinalRadianceAtlas);
			}
			else
			{
				FRDGTextureDesc FinalRadianceAtlasDesc = FRDGTextureDesc::Create2D(
					FinalRadianceAtlasSize,
					PF_FloatRGB,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV,
					RadianceCacheInputs.FinalRadianceAtlasMaxMip + 1);

				FinalRadianceAtlas = GraphBuilder.CreateTexture(FinalRadianceAtlasDesc, TEXT("Lumen.RadianceCache.FinalRadianceAtlas"));
				bResizedHistoryState = true;
			}

			if (GRadianceCacheForceFullUpdate)
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas)), FLinearColor::Black);
			}
		}

		LumenRadianceCache::GetInterpolationParametersNoResources(GraphBuilder, RadianceCacheState, RadianceCacheInputs, RadianceCacheParameters);
		
		const FIntVector RadianceProbeIndirectionTextureSize = FIntVector(
			RadianceCacheInputs.RadianceProbeClipmapResolution * RadianceCacheInputs.NumRadianceProbeClipmaps, 
			RadianceCacheInputs.RadianceProbeClipmapResolution, 
			RadianceCacheInputs.RadianceProbeClipmapResolution);

		FRDGTextureDesc ProbeIndirectionDesc = FRDGTextureDesc::Create3D(
			RadianceProbeIndirectionTextureSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling);

		FRDGTextureRef RadianceProbeIndirectionTexture = GraphBuilder.CreateTexture(FRDGTextureDesc(ProbeIndirectionDesc), TEXT("Lumen.RadianceCache.RadianceProbeIndirectionTexture"));
		FRDGTextureUAVRef RadianceProbeIndirectionTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadianceProbeIndirectionTexture));

		RadianceCacheParameters.RadianceProbeIndirectionTexture = RadianceProbeIndirectionTexture;

		// Clear each clipmap indirection entry to invalid probe index
		{
			FClearProbeIndirectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearProbeIndirectionCS::FParameters>();
			PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;

			auto ComputeShader = View.ShaderMap->GetShader<FClearProbeIndirectionCS>(0);

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FClearProbeIndirectionCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearProbeIndirectionCS"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		LumenRadianceCache::FRadianceCacheMarkParameters RadianceCacheMarkParameters = LumenRadianceCache::GetMarkParameters(RadianceProbeIndirectionTextureUAV, RadianceCacheState, RadianceCacheInputs);

		// Mark indirection entries around positions that will be sampled by dependent features as used
		MarkUsedRadianceCacheProbes.Broadcast(GraphBuilder, View, RadianceCacheMarkParameters);

		const bool bPersistentCache = !GRadianceCacheForceFullUpdate 
			&& View.ViewState 
			&& IsValidRef(RadianceCacheState.RadianceProbeIndirectionTexture)
			&& RadianceCacheState.RadianceProbeIndirectionTexture->GetDesc().GetSize() == RadianceProbeIndirectionTextureSize
			&& !bResizedHistoryState
			&& !View.bLumenPropagateGlobalLightingChange;

		FRDGBufferRef ProbeFreeListAllocator = nullptr;
		FRDGBufferRef ProbeFreeList = nullptr;
		FRDGBufferRef ProbeLastUsedFrame = nullptr;
		FRDGBufferRef ProbeLastTracedFrame = nullptr;
		FRDGBufferRef ProbeWorldOffset = nullptr;
		const int32 MaxNumProbes = RadianceCacheInputs.ProbeAtlasResolutionInProbes.X * RadianceCacheInputs.ProbeAtlasResolutionInProbes.Y;

		if (IsValidRef(RadianceCacheState.ProbeFreeList) && RadianceCacheState.ProbeFreeList->Desc.NumElements == MaxNumProbes)
		{
			ProbeFreeListAllocator = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeListAllocator);
			ProbeFreeList = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeList);
			ProbeLastUsedFrame = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeLastUsedFrame);
			ProbeLastTracedFrame = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeLastTracedFrame);
			ProbeWorldOffset = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeWorldOffset);
		}
		else
		{
			ProbeFreeListAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(int32), 1), TEXT("Lumen.RadianceCache.ProbeFreeListAllocator"));
			ProbeFreeList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeFreeList"));
			ProbeLastUsedFrame = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeLastUsedFrame"));
			ProbeLastTracedFrame = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeLastTracedFrame"));
			ProbeWorldOffset = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeWorldOffset"));
		}

		FRDGBufferUAVRef ProbeFreeListAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeFreeListAllocator, PF_R32_SINT));
		FRDGBufferUAVRef ProbeFreeListUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeFreeList, PF_R32_UINT));
		FRDGBufferUAVRef ProbeLastUsedFrameUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeLastUsedFrame, PF_R32_UINT));
		FRDGBufferUAVRef ProbeWorldOffsetUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeWorldOffset, PF_A32B32G32R32F));

		if (!bPersistentCache || !IsValidRef(RadianceCacheState.ProbeFreeListAllocator))
		{
			FClearProbeFreeList::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearProbeFreeList::FParameters>();
			PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
			PassParameters->RWProbeFreeList = ProbeFreeListUAV;
			PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
			PassParameters->RWProbeWorldOffset = ProbeWorldOffsetUAV;
			PassParameters->MaxNumProbes = MaxNumProbes;

			auto ComputeShader = View.ShaderMap->GetShader<FClearProbeFreeList>();

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxNumProbes, FClearProbeFreeList::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearProbeFreeList"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}
		
		// Propagate probes from last frame to the new frame's indirection
		if (bPersistentCache)
		{
			FRDGTextureRef LastFrameRadianceProbeIndirectionTexture = GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeIndirectionTexture);

			{
				FUpdateCacheForUsedProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdateCacheForUsedProbesCS::FParameters>();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
				PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
				PassParameters->RWProbeFreeList = ProbeFreeListUAV;
				PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
				PassParameters->LastFrameRadianceProbeIndirectionTexture = LastFrameRadianceProbeIndirectionTexture;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FrameNumber = View.ViewState->GetFrameIndex();
				PassParameters->NumFramesToKeepCachedProbes = GRadianceNumFramesToKeepCachedProbes;

				for (int32 ClipmapIndex = 0; ClipmapIndex < LastFrameClipmaps.Num(); ++ClipmapIndex)
				{
					const FRadianceCacheClipmap& Clipmap = LastFrameClipmaps[ClipmapIndex];

					SetRadianceProbeCoordToWorldPosition(PassParameters->PackedLastFrameRadianceProbeCoordToWorldPosition[ClipmapIndex], (FVector3f)Clipmap.ProbeCoordToWorldCenterBias, Clipmap.ProbeCoordToWorldCenterScale);
				}

				auto ComputeShader = View.ShaderMap->GetShader<FUpdateCacheForUsedProbesCS>(0);

				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FUpdateCacheForUsedProbesCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("UpdateCacheForUsedProbes"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}
		}

		FRDGTextureUAVRef DepthProbeTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DepthProbeAtlasTexture));
		
		FRDGBufferRef ProbeAllocator = nullptr;

		if (IsValidRef(RadianceCacheState.ProbeAllocator))
		{
			ProbeAllocator = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeAllocator, TEXT("Lumen.RadianceCache.ProbeAllocator"));
		}
		else
		{
			ProbeAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.ProbeAllocator"));
		}

		FRDGBufferUAVRef ProbeAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeAllocator, PF_R32_UINT));

		if (!bPersistentCache || !IsValidRef(RadianceCacheState.ProbeAllocator))
		{
			FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, ProbeAllocatorUAV, 0);
		}

		FRDGBufferRef ProbeTraceData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeTraceData"));

		FRDGTextureRef RadianceProbeAtlasTextureSource = nullptr;

		FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
			RadianceProbeAtlasTextureSize,
			PF_FloatRGB,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		if (RadianceCacheState.RadianceProbeAtlasTexture.IsValid()
			&& RadianceCacheState.RadianceProbeAtlasTexture->GetDesc().Extent == RadianceProbeAtlasTextureSize)
		{
			RadianceProbeAtlasTextureSource = GraphBuilder.RegisterExternalTexture(RadianceCacheState.RadianceProbeAtlasTexture);
		}
		else
		{
			RadianceProbeAtlasTextureSource = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("Lumen.RadianceCache.RadianceProbeAtlasTextureSource"));
		}

		FRDGBufferRef ProbeTraceAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.ProbeTraceAllocator"));
		FRDGBufferUAVRef ProbeTraceAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceAllocator, PF_R32_UINT));

		FRDGBufferRef PriorityHistogram = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LumenRadianceCache::PRIORITY_HISTOGRAM_SIZE), TEXT("Lumen.RadianceCache.PriorityHistogram"));
		FRDGBufferRef MaxUpdateBucket = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.MaxUpdateBucket"));
		FRDGBufferRef MaxTracesFromMaxUpdateBucket = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.MaxTracesFromMaxUpdateBucket"));
		FRDGBufferRef ProbesToUpdateTraceCost = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LumenRadianceCache::PROBES_TO_UPDATE_TRACE_COST_STRIDE), TEXT("Lumen.RadianceCache.ProbesToUpdateTraceCost"));

		// Batch clear all resources required for the subsequent radiance cache probe update pass
		{
			FClearRadianceCacheUpdateResourcesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearRadianceCacheUpdateResourcesCS::FParameters>();
			PassParameters->RWProbeTraceAllocator = ProbeTraceAllocatorUAV;
			PassParameters->RWPriorityHistogram = GraphBuilder.CreateUAV(PriorityHistogram);
			PassParameters->RWMaxUpdateBucket = GraphBuilder.CreateUAV(MaxUpdateBucket);
			PassParameters->RWMaxTracesFromMaxUpdateBucket = GraphBuilder.CreateUAV(MaxTracesFromMaxUpdateBucket);
			PassParameters->RWProbesToUpdateTraceCost = GraphBuilder.CreateUAV(ProbesToUpdateTraceCost);

			auto ComputeShader = View.ShaderMap->GetShader<FClearRadianceCacheUpdateResourcesCS>();

			const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(LumenRadianceCache::PRIORITY_HISTOGRAM_SIZE, FClearRadianceCacheUpdateResourcesCS::GetGroupSize()), 1, 1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearRadianceCacheUpdateResources"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		// Allocated used probes
		{	
			FAllocateUsedProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateUsedProbesCS::FParameters>();
			PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
			PassParameters->RWPriorityHistogram = GraphBuilder.CreateUAV(PriorityHistogram);
			PassParameters->RWProbeLastTracedFrame = GraphBuilder.CreateUAV(ProbeLastTracedFrame);
			PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
			PassParameters->RWProbeAllocator = ProbeAllocatorUAV;
			PassParameters->RWProbeFreeListAllocator = bPersistentCache ? ProbeFreeListAllocatorUAV : nullptr;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->ProbeFreeList = bPersistentCache ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeFreeList, PF_R32_UINT)) : nullptr;
			PassParameters->FirstClipmapWorldExtentRcp = 1.0f / FMath::Max(RadianceCacheInputs.ClipmapWorldExtent, 1.0f);
			PassParameters->SupersampleDistanceFromCameraSq = GLumenRadianceCacheSupersampleDistanceFromCamera * GLumenRadianceCacheSupersampleDistanceFromCamera;
			PassParameters->DownsampleDistanceFromCameraSq = GLumenRadianceCacheDownsampleDistanceFromCamera * GLumenRadianceCacheDownsampleDistanceFromCamera;
			PassParameters->FrameNumber = View.ViewState ? View.ViewState->GetFrameIndex() : View.Family->FrameNumber;
			PassParameters->MaxNumProbes = MaxNumProbes;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;

			FAllocateUsedProbesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FAllocateUsedProbesCS::FPersistentCache>(bPersistentCache);
			auto ComputeShader = View.ShaderMap->GetShader<FAllocateUsedProbesCS>(PermutationVector);

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FAllocateUsedProbesCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AllocateUsedProbes"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		// Selected max priority bucket
		{
			FSelectMaxPriorityBucketCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectMaxPriorityBucketCS::FParameters>();
			PassParameters->RWMaxUpdateBucket = GraphBuilder.CreateUAV(MaxUpdateBucket);
			PassParameters->RWMaxTracesFromMaxUpdateBucket = GraphBuilder.CreateUAV(MaxTracesFromMaxUpdateBucket);
			PassParameters->PriorityHistogram = GraphBuilder.CreateSRV(PriorityHistogram);
			PassParameters->ProbeTraceAllocator = GraphBuilder.CreateSRV(ProbeTraceAllocator, PF_R32_UINT);
			PassParameters->NumProbesToTraceBudget = GRadianceCacheForceFullUpdate ? UINT32_MAX : RadianceCacheInputs.NumProbesToTraceBudget;

			auto ComputeShader = View.ShaderMap->GetShader<FSelectMaxPriorityBucketCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SelectMaxPriorityBucket"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		// Trace probes up to selected priority bucket
		{
			FAllocateProbeTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateProbeTracesCS::FParameters>();
			PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
			PassParameters->RWProbesToUpdateTraceCost = GraphBuilder.CreateUAV(ProbesToUpdateTraceCost);
			PassParameters->RWProbeLastTracedFrame = GraphBuilder.CreateUAV(ProbeLastTracedFrame);
			PassParameters->RWProbeTraceAllocator = ProbeTraceAllocatorUAV;
			PassParameters->RWProbeTraceData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->RWProbeFreeListAllocator = bPersistentCache ? ProbeFreeListAllocatorUAV : nullptr;
			PassParameters->MaxUpdateBucket = GraphBuilder.CreateSRV(MaxUpdateBucket, PF_R32_UINT);
			PassParameters->MaxTracesFromMaxUpdateBucket = GraphBuilder.CreateSRV(MaxTracesFromMaxUpdateBucket);
			PassParameters->ProbeLastUsedFrame = GraphBuilder.CreateSRV(ProbeLastUsedFrame, PF_R32_UINT);
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->ProbeFreeList = bPersistentCache ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeFreeList, PF_R32_UINT)) : nullptr;
			PassParameters->SupersampleDistanceFromCameraSq = GLumenRadianceCacheSupersampleDistanceFromCamera * GLumenRadianceCacheSupersampleDistanceFromCamera;
			PassParameters->DownsampleDistanceFromCameraSq = GLumenRadianceCacheDownsampleDistanceFromCamera * GLumenRadianceCacheDownsampleDistanceFromCamera;
			PassParameters->FirstClipmapWorldExtentRcp = 1.0f / FMath::Max(RadianceCacheInputs.ClipmapWorldExtent, 1.0f);
			PassParameters->FrameNumber = View.ViewState ? View.ViewState->GetFrameIndex() : View.Family->FrameNumber;
			PassParameters->MaxNumProbes = MaxNumProbes;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;

			auto ComputeShader = View.ShaderMap->GetShader<FAllocateProbeTracesCS>();

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FAllocateProbeTracesCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AllocateProbeTraces"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		FRDGBufferRef ClearProbePDFsIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(2), TEXT("Lumen.RadianceCache.ClearProbePDFsIndirectArgs"));
		FRDGBufferRef GenerateProbeTraceTilesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(3), TEXT("Lumen.RadianceCache.GenerateProbeTraceTilesIndirectArgs"));
		FRDGBufferRef ProbeTraceTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.ProbeTraceTileAllocator"));
		FRDGBufferRef FilterProbesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(5), TEXT("Lumen.RadianceCache.FilterProbesIndirectArgs"));
		FRDGBufferRef PrepareProbeOcclusionIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(7), TEXT("Lumen.RadianceCache.PrepareProbeOcclusionIndirectArgs"));
		FRDGBufferRef FixupProbeBordersIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(8), TEXT("Lumen.RadianceCache.FixupProbeBordersIndirectArgs"));

		{
			FSetupProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupProbeIndirectArgsCS::FParameters>();
			PassParameters->RWProbeAllocator = ProbeAllocatorUAV;
			PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
			PassParameters->RWClearProbePDFsIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ClearProbePDFsIndirectArgs, PF_R32_UINT));
			PassParameters->RWGenerateProbeTraceTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(GenerateProbeTraceTilesIndirectArgs, PF_R32_UINT));
			PassParameters->RWProbeTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RWFilterProbesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(FilterProbesIndirectArgs, PF_R32_UINT));
			PassParameters->RWPrepareProbeOcclusionIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(PrepareProbeOcclusionIndirectArgs, PF_R32_UINT));
			PassParameters->RWFixupProbeBordersIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(FixupProbeBordersIndirectArgs, PF_R32_UINT));
			PassParameters->ProbeTraceAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceAllocator, PF_R32_UINT));
			PassParameters->TraceFromProbesGroupSizeXY = FRadianceCacheTraceFromProbesCS::GetGroupSize();
			PassParameters->FilterProbesGroupSizeXY = FFilterProbeRadianceWithGatherCS::GetGroupSize();
			PassParameters->ClearProbePDFGroupSize = FClearProbePDFs::GetGroupSize();
			PassParameters->MaxNumProbes = MaxNumProbes;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			auto ComputeShader = View.ShaderMap->GetShader<FSetupProbeIndirectArgsCS>(0);

			const FIntVector GroupSize = FIntVector(1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SetupProbeIndirectArgsCS"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		if (RadianceCacheInputs.CalculateIrradiance)
		{
			FComputeProbeWorldOffsetsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeProbeWorldOffsetsCS::FParameters>();
			PassParameters->RWProbeWorldOffset = ProbeWorldOffsetUAV;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->IndirectArgs = GenerateProbeTraceTilesIndirectArgs;

			auto ComputeShader = View.ShaderMap->GetShader<FComputeProbeWorldOffsetsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ComputeProbeWorldOffsets"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0);
		}

		RadianceCacheParameters.ProbeWorldOffset = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeWorldOffset, PF_A32B32G32R32F));

		FRDGBufferRef RadianceProbeSH_PDF = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(int32), MaxNumProbes * (9 + 1)), TEXT("Lumen.RadianceCache.RadianceProbeSH_PDF"));

		const bool bGenerateBRDF_PDF = ScreenProbeParameters && BRDFProbabilityDensityFunctionSH;

		if (bGenerateBRDF_PDF)
		{
			{
				FClearProbePDFs::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearProbePDFs::FParameters>();
				PassParameters->RWRadianceProbeSH_PDF = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadianceProbeSH_PDF, PF_R32_SINT));
				PassParameters->ClearProbePDFsIndirectArgs = ClearProbePDFsIndirectArgs;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));

				auto ComputeShader = View.ShaderMap->GetShader<FClearProbePDFs>(0);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ClearProbePDFs"),
					ComputeShader,
					PassParameters,
					PassParameters->ClearProbePDFsIndirectArgs,
					0);
			}

			{
				FScatterScreenProbeBRDFToRadianceProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScatterScreenProbeBRDFToRadianceProbesCS::FParameters>();
				PassParameters->RWRadianceProbeSH_PDF = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadianceProbeSH_PDF, PF_R32_SINT));
				PassParameters->BRDFProbabilityDensityFunctionSH = BRDFProbabilityDensityFunctionSH;
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->ScreenProbeParameters = *ScreenProbeParameters;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;

				auto ComputeShader = View.ShaderMap->GetShader<FScatterScreenProbeBRDFToRadianceProbesCS>(0);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ScatterScreenProbeBRDFToRadianceProbes"),
					ComputeShader,
					PassParameters,
					ScreenProbeParameters->ProbeIndirectArgs,
					(uint32)EScreenProbeIndirectArgs::GroupPerProbe * sizeof(FRHIDispatchIndirectParameters));
			}
		}

		const int32 MaxProbeTraceTileResolution = RadianceCacheInputs.RadianceProbeResolution / FRadianceCacheTraceFromProbesCS::GetGroupSize() * 2;
		FRDGBufferRef ProbeTraceTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FIntPoint), MaxNumProbes * MaxProbeTraceTileResolution * MaxProbeTraceTileResolution), TEXT("Lumen.RadianceCache.ProbeTraceTileData"));

		const int32 DebugProbeBRDFOctahedronResolution = 8;
		FRDGTextureDesc DebugBRDFProbabilityDensityFunctionDesc = FRDGTextureDesc::Create2D(
			FIntPoint(RadianceCacheInputs.ProbeAtlasResolutionInProbes * DebugProbeBRDFOctahedronResolution),
			PF_R16F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef DebugBRDFProbabilityDensityFunction = GraphBuilder.CreateTexture(DebugBRDFProbabilityDensityFunctionDesc, TEXT("Lumen.RadianceCache.DebugBRDFProbabilityDensityFunction"));

		{
			FGenerateProbeTraceTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateProbeTraceTilesCS::FParameters>();
			PassParameters->RWProbeTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RWProbeTraceTileData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->RadianceProbeSH_PDF = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RadianceProbeSH_PDF, PF_R32_SINT));
			PassParameters->ProbesToUpdateTraceCost = GraphBuilder.CreateSRV(ProbesToUpdateTraceCost);
			PassParameters->SupersampleTileBRDFThreshold = GLumenRadianceCacheSupersampleTileBRDFThreshold;
			PassParameters->SupersampleDistanceFromCameraSq = GLumenRadianceCacheSupersampleDistanceFromCamera * GLumenRadianceCacheSupersampleDistanceFromCamera;
			PassParameters->DownsampleDistanceFromCameraSq = GLumenRadianceCacheDownsampleDistanceFromCamera * GLumenRadianceCacheDownsampleDistanceFromCamera;

			PassParameters->RWDebugBRDFProbabilityDensityFunction = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DebugBRDFProbabilityDensityFunction));
			PassParameters->DebugProbeBRDFOctahedronResolution = DebugProbeBRDFOctahedronResolution;

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->GenerateProbeTraceTilesIndirectArgs = GenerateProbeTraceTilesIndirectArgs;

			FGenerateProbeTraceTilesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FGenerateProbeTraceTilesCS::FUniformTraces>(!bGenerateBRDF_PDF);
			auto ComputeShader = View.ShaderMap->GetShader<FGenerateProbeTraceTilesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GenerateProbeTraceTiles"),
				ComputeShader,
				PassParameters,
				PassParameters->GenerateProbeTraceTilesIndirectArgs,
				0);
		}

		FRDGBufferRef TraceProbesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(4), TEXT("Lumen.RadianceCache.TraceProbesIndirectArgs"));
		FRDGBufferRef SortProbeTraceTilesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(5), TEXT("Lumen.RadianceCache.SortProbeTraceTilesIndirectArgs"));
		FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(6), TEXT("Lumen.RadianceCache.RadianceCacheHardwareRayTracingIndirectArgs"));
		FRDGBufferRef HardwareRayTracingRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.HardwareRayTracing.RayAllocatorBuffer"));

		{
			FSetupTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupTraceFromProbesCS::FParameters>();
			PassParameters->RWTraceProbesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceProbesIndirectArgs, PF_R32_UINT));
			PassParameters->RWSortProbeTraceTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(SortProbeTraceTilesIndirectArgs, PF_R32_UINT));
			PassParameters->RWRadianceCacheHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadianceCacheHardwareRayTracingIndirectArgs, PF_R32_UINT));
			PassParameters->RWHardwareRayTracingRayAllocatorBuffer = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(HardwareRayTracingRayAllocatorBuffer, PF_R32_UINT));
			PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->SortTraceTilesGroupSize = FSortProbeTraceTilesCS::GetGroupSize();
			auto ComputeShader = View.ShaderMap->GetShader<FSetupTraceFromProbesCS>(0);

			const FIntVector GroupSize = FIntVector(1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SetupTraceFromProbesCS"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		if (GRadianceCacheSortTraceTiles)
		{
			FRDGBufferRef SortedProbeTraceTileData = GraphBuilder.CreateBuffer(ProbeTraceTileData->Desc, TEXT("Lumen.RadianceCache.SortedProbeTraceTileData"));

			FSortProbeTraceTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSortProbeTraceTilesCS::FParameters>();
			PassParameters->RWProbeTraceTileData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(SortedProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->SortProbeTraceTilesIndirectArgs = SortProbeTraceTilesIndirectArgs;
			PassParameters->RadianceCacheInputs = RadianceCacheInputs;

			auto ComputeShader = View.ShaderMap->GetShader<FSortProbeTraceTilesCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SortTraceTiles"),
				ComputeShader,
				PassParameters,
				PassParameters->SortProbeTraceTilesIndirectArgs,
				0);

			ProbeTraceTileData = SortedProbeTraceTileData;
		}

		FRDGTextureUAVRef RadianceProbeAtlasTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadianceProbeAtlasTextureSource));

		if (Lumen::UseHardwareRayTracedRadianceCache())
		{
			float DiffuseConeHalfAngle = -1.0f;
			RenderLumenHardwareRayTracingRadianceCache(
				GraphBuilder,
				Scene,
				GetSceneTextureParameters(GraphBuilder),
				View,
				TracingInputs,
				RadianceCacheParameters,
				Configuration,
				DiffuseConeHalfAngle,
				MaxNumProbes,
				MaxProbeTraceTileResolution,
				ProbeTraceData,
				ProbeTraceTileData,
				ProbeTraceTileAllocator,
				TraceProbesIndirectArgs,
				HardwareRayTracingRayAllocatorBuffer,
				RadianceCacheHardwareRayTracingIndirectArgs,
				RadianceProbeAtlasTextureUAV,
				DepthProbeTextureUAV
			);
		}
		else
		{
			FRadianceCacheTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadianceCacheTraceFromProbesCS::FParameters>();
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			SetupLumenDiffuseTracingParametersForProbe(View, PassParameters->IndirectTracingParameters, -1.0f);
			PassParameters->RWRadianceProbeAtlasTexture = RadianceProbeAtlasTextureUAV;
			PassParameters->RWDepthProbeAtlasTexture = DepthProbeTextureUAV;
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->TraceProbesIndirectArgs = TraceProbesIndirectArgs;

			FRadianceCacheTraceFromProbesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FTraceGlobalSDF>(Lumen::UseGlobalSDFTracing(*View.Family));
			PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FDistantScene>(Scene->LumenSceneData->DistantCardIndices.Num() > 0);
			PermutationVector.Set<FRadianceCacheTraceFromProbesCS::FDynamicSkyLight>(Lumen::ShouldHandleSkyLight(Scene, *View.Family));
			auto ComputeShader = View.ShaderMap->GetShader<FRadianceCacheTraceFromProbesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TraceFromProbes Res=%ux%u", RadianceCacheInputs.RadianceProbeResolution, RadianceCacheInputs.RadianceProbeResolution),
				ComputeShader,
				PassParameters,
				PassParameters->TraceProbesIndirectArgs,
				0);
		}

		FRDGTextureRef RadianceProbeAtlasTexture = RadianceProbeAtlasTextureSource;

		if (GRadianceCacheFilterProbes)
		{
			FRDGTextureRef FilteredRadianceProbeAtlasTexture = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("Lumen.RadianceCache.FilteredRadianceProbeAtlasTexture"));

			{
				FFilterProbeRadianceWithGatherCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFilterProbeRadianceWithGatherCS::FParameters>();
				PassParameters->RWRadianceProbeAtlasTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FilteredRadianceProbeAtlasTexture));
				PassParameters->RadianceProbeAtlasTexture = RadianceProbeAtlasTexture;
				PassParameters->DepthProbeAtlasTexture = DepthProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FilterProbesIndirectArgs = FilterProbesIndirectArgs;
				PassParameters->SpatialFilterMaxRadianceHitAngle = GLumenRadianceCacheFilterMaxRadianceHitAngle;

				auto ComputeShader = View.ShaderMap->GetShader<FFilterProbeRadianceWithGatherCS>(0);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("FilterProbeRadiance Res=%ux%u", RadianceCacheInputs.RadianceProbeResolution, RadianceCacheInputs.RadianceProbeResolution),
					ComputeShader,
					PassParameters,
					PassParameters->FilterProbesIndirectArgs,
					0);
			}

			RadianceProbeAtlasTexture = FilteredRadianceProbeAtlasTexture;
		}

		if (RadianceCacheInputs.CalculateIrradiance)
		{
			const int32 OctahedralSolidAngleTextureSize = 16;
			FOctahedralSolidAngleParameters OctahedralSolidAngleParameters;
			OctahedralSolidAngleParameters.OctahedralSolidAngleTextureResolutionSq = OctahedralSolidAngleTextureSize * OctahedralSolidAngleTextureSize;
			OctahedralSolidAngleParameters.OctahedralSolidAngleTexture = InitializeOctahedralSolidAngleTexture(GraphBuilder, View.ShaderMap, OctahedralSolidAngleTextureSize, RadianceCacheState.OctahedralSolidAngleTextureRT);

			{
				FCalculateProbeIrradianceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalculateProbeIrradianceCS::FParameters>();
				PassParameters->RWFinalIrradianceAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalIrradianceAtlas));
				PassParameters->RadianceProbeAtlasTexture = RadianceProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->OctahedralSolidAngleParameters = OctahedralSolidAngleParameters;
				PassParameters->View = View.ViewUniformBuffer;
				// GenerateProbeTraceTilesIndirectArgs is the same so we can reuse it
				PassParameters->CalculateProbeIrradianceIndirectArgs = GenerateProbeTraceTilesIndirectArgs;

				auto ComputeShader = View.ShaderMap->GetShader<FCalculateProbeIrradianceCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CalculateProbeIrradiance Res=%ux%u", RadianceCacheInputs.IrradianceProbeResolution, RadianceCacheInputs.IrradianceProbeResolution),
					ComputeShader,
					PassParameters,
					GenerateProbeTraceTilesIndirectArgs,
					0);
			}

			RadianceCacheParameters.RadianceCacheFinalIrradianceAtlas = FinalIrradianceAtlas;

			{
				FPrepareProbeOcclusionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrepareProbeOcclusionCS::FParameters>();
				PassParameters->RWRadianceCacheProbeOcclusionAtlas = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ProbeOcclusionAtlas));
				PassParameters->DepthProbeAtlasTexture = DepthProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->PrepareProbeOcclusionIndirectArgs = PrepareProbeOcclusionIndirectArgs;

				auto ComputeShader = View.ShaderMap->GetShader<FPrepareProbeOcclusionCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("PrepareProbeOcclusion Res=%ux%u", RadianceCacheInputs.OcclusionProbeResolution, RadianceCacheInputs.OcclusionProbeResolution),
					ComputeShader,
					PassParameters,
					PrepareProbeOcclusionIndirectArgs,
					0);
			}

			RadianceCacheParameters.RadianceCacheProbeOcclusionAtlas = ProbeOcclusionAtlas;
		}
		else
		{
			FRDGTextureUAVRef FinalRadianceAtlasUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas));

			const bool bGenerateMips = RadianceCacheInputs.FinalRadianceAtlasMaxMip > 0;

			ensureMsgf(RadianceCacheInputs.FinalRadianceAtlasMaxMip <= 2, TEXT("Requested mip is more than supported by shader"));
			{
				FFixupBordersAndGenerateMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFixupBordersAndGenerateMipsCS::FParameters>();
				PassParameters->RWFinalRadianceAtlasMip0 = FinalRadianceAtlasUAV;

				if (bGenerateMips)
				{
					PassParameters->RWFinalRadianceAtlasMip1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas, 1));
					PassParameters->RWFinalRadianceAtlasMip2 = PassParameters->RWFinalRadianceAtlasMip1;

					if (RadianceCacheInputs.FinalRadianceAtlasMaxMip > 1)
					{
						PassParameters->RWFinalRadianceAtlasMip2 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas, 2));
					}
				}
				
				PassParameters->RadianceProbeAtlasTexture = RadianceProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FixupProbeBordersIndirectArgs = FixupProbeBordersIndirectArgs;

				FFixupBordersAndGenerateMipsCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FFixupBordersAndGenerateMipsCS::FGenerateMips>(bGenerateMips);
				auto ComputeShader = View.ShaderMap->GetShader<FFixupBordersAndGenerateMipsCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("FixupBordersAndGenerateMips"),
					ComputeShader,
					PassParameters,
					FixupProbeBordersIndirectArgs,
					0);
			}

			RadianceCacheParameters.RadianceCacheFinalRadianceAtlas = FinalRadianceAtlas;
		}

		if (RadianceCacheInputs.RadianceCacheStats != 0)
		{
			FRadianceCacheUpdateStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadianceCacheUpdateStatsCS::FParameters>();
			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintUniformBuffer);
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->PriorityHistogram = GraphBuilder.CreateSRV(PriorityHistogram);
			PassParameters->MaxUpdateBucket = GraphBuilder.CreateSRV(MaxUpdateBucket);
			PassParameters->MaxTracesFromMaxUpdateBucket = GraphBuilder.CreateSRV(MaxTracesFromMaxUpdateBucket);
			PassParameters->ProbesToUpdateTraceCost = GraphBuilder.CreateSRV(ProbesToUpdateTraceCost);
			PassParameters->ProbeTraceAllocator = GraphBuilder.CreateSRV(ProbeTraceAllocator, PF_R32_UINT);
			PassParameters->ProbeAllocator = GraphBuilder.CreateSRV(ProbeAllocator, PF_R32_UINT);
			PassParameters->ProbeFreeListAllocator = GraphBuilder.CreateSRV(ProbeFreeListAllocator, PF_R32_UINT);
			PassParameters->MaxNumProbes = MaxNumProbes;

			auto ComputeShader = View.ShaderMap->GetShader<FRadianceCacheUpdateStatsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RadianceCacheUpdateStats"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		RadianceCacheState.ProbeFreeListAllocator = GraphBuilder.ConvertToExternalBuffer(ProbeFreeListAllocator);
		RadianceCacheState.ProbeFreeList = GraphBuilder.ConvertToExternalBuffer(ProbeFreeList);
		RadianceCacheState.ProbeAllocator = GraphBuilder.ConvertToExternalBuffer(ProbeAllocator);
		RadianceCacheState.ProbeLastUsedFrame = GraphBuilder.ConvertToExternalBuffer(ProbeLastUsedFrame);
		RadianceCacheState.ProbeLastTracedFrame = GraphBuilder.ConvertToExternalBuffer(ProbeLastTracedFrame);
		RadianceCacheState.ProbeWorldOffset = GraphBuilder.ConvertToExternalBuffer(ProbeWorldOffset);
		RadianceCacheState.RadianceProbeIndirectionTexture = GraphBuilder.ConvertToExternalTexture(RadianceProbeIndirectionTexture);
		RadianceCacheState.DepthProbeAtlasTexture = GraphBuilder.ConvertToExternalTexture(DepthProbeAtlasTexture);
		RadianceCacheState.RadianceProbeAtlasTexture = GraphBuilder.ConvertToExternalTexture(RadianceProbeAtlasTextureSource);

		if (FinalRadianceAtlas)
		{
			RadianceCacheState.FinalRadianceAtlas = GraphBuilder.ConvertToExternalTexture(FinalRadianceAtlas);
		}
		else
		{
			RadianceCacheState.FinalRadianceAtlas = nullptr;
		}
		
		if (FinalIrradianceAtlas)
		{
			RadianceCacheState.FinalIrradianceAtlas = GraphBuilder.ConvertToExternalTexture(FinalIrradianceAtlas);
			RadianceCacheState.ProbeOcclusionAtlas = GraphBuilder.ConvertToExternalTexture(ProbeOcclusionAtlas);
		}
		else
		{
			RadianceCacheState.FinalIrradianceAtlas = nullptr;
			RadianceCacheState.ProbeOcclusionAtlas = nullptr;
		}

		RadianceCacheParameters.RadianceCacheDepthAtlas = DepthProbeAtlasTexture;
	}
	else // GRadianceCacheUpdate != 0
	{
		LumenRadianceCache::GetInterpolationParameters(View, GraphBuilder, RadianceCacheState, RadianceCacheInputs, RadianceCacheParameters);
	}
}
