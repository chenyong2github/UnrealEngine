// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenRadianceCache.cpp
=============================================================================*/

#include "LumenRadianceCache.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "LumenSceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenScreenProbeGather.h"

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

int32 GRadianceCacheProbesUpdateEveryNFrames = 10;
FAutoConsoleVariableRef CVarRadianceCacheProbesUpdateEveryNFrames(
	TEXT("r.Lumen.RadianceCache.ProbesUpdateEveryNFrames"),
	GRadianceCacheProbesUpdateEveryNFrames,
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

DECLARE_GPU_STAT(LumenRadianceCache);

namespace LumenRadianceCache
{
	void GetInterpolationParametersNoResources(
		FRDGBuilder& GraphBuilder, 
		const FRadianceCacheState& RadianceCacheState,
		const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs, 
		FRadianceCacheInterpolationParameters& OutParameters)
	{
		OutParameters.RadianceCacheInputs = RadianceCacheInputs;
		OutParameters.RadianceCacheInputs.NumProbeTracesBudget = GRadianceCacheForceFullUpdate ? 1000000 : OutParameters.RadianceCacheInputs.NumProbeTracesBudget;
		OutParameters.RadianceProbeIndirectionTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
		OutParameters.RadianceCacheFinalRadianceAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		OutParameters.RadianceCacheFinalIrradianceAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		OutParameters.RadianceCacheProbeOcclusionAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		OutParameters.RadianceCacheDepthAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		OutParameters.ProbeWorldOffset = nullptr;
		OutParameters.OverrideCacheOcclusionLighting = GRadianceCacheOverrideCacheOcclusionLighting;
		OutParameters.ShowBlackRadianceCacheLighting = GRadianceCacheShowBlackRadianceCacheLighting;

		for (int32 ClipmapIndex = 0; ClipmapIndex < RadianceCacheState.Clipmaps.Num(); ++ClipmapIndex)
		{
			const FRadianceCacheClipmap& Clipmap = RadianceCacheState.Clipmaps[ClipmapIndex];

			OutParameters.RadianceProbeClipmapTMin[ClipmapIndex] = Clipmap.ProbeTMin;
			OutParameters.WorldPositionToRadianceProbeCoordScale[ClipmapIndex] = Clipmap.WorldPositionToProbeCoordScale;
			OutParameters.WorldPositionToRadianceProbeCoordBias[ClipmapIndex] = Clipmap.WorldPositionToProbeCoordBias;
			OutParameters.RadianceProbeCoordToWorldPositionScale[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterScale;
			OutParameters.RadianceProbeCoordToWorldPositionBias[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterBias;
		}

		OutParameters.InvProbeFinalRadianceAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(RadianceCacheInputs.FinalProbeResolution * RadianceCacheInputs.ProbeAtlasResolutionInProbes);
		const int32 FinalIrradianceProbeResolution = RadianceCacheInputs.IrradianceProbeResolution + 2 * (1 << RadianceCacheInputs.FinalRadianceAtlasMaxMip);
		OutParameters.InvProbeFinalIrradianceAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(FinalIrradianceProbeResolution * RadianceCacheInputs.ProbeAtlasResolutionInProbes);
		OutParameters.InvProbeDepthAtlasResolution = FVector2D(1.0f, 1.0f) / FVector2D(RadianceCacheInputs.RadianceProbeResolution * RadianceCacheInputs.ProbeAtlasResolutionInProbes);
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
		OutParameters.RadianceCacheDepthAtlas = GraphBuilder.RegisterExternalTexture(RadianceCacheState.DepthProbeAtlasTexture, TEXT("Lumen.RadianceCacheDepthAtlas"));
		FRDGBufferRef ProbeWorldOffset = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeWorldOffset);
		OutParameters.ProbeWorldOffset = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeWorldOffset, PF_A32B32G32R32F));
	}
};

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

IMPLEMENT_GLOBAL_SHADER(FClearProbeFreeList, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClearProbeFreeListCS", SF_Compute);

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
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeFreeList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, LastFrameRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_ARRAY(float, LastFrameRadianceProbeCoordToWorldPositionScale, [LumenRadianceCache::MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector, LastFrameRadianceProbeCoordToWorldPositionBias, [LumenRadianceCache::MaxClipmaps])
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

IMPLEMENT_GLOBAL_SHADER(FUpdateCacheForUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "UpdateCacheForUsedProbesCS", SF_Compute);

class FAllocateUsedProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateUsedProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FAllocateUsedProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeLastUsedFrame)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeFreeList)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(uint32, ProbesUpdateEveryNFrames)
		SHADER_PARAMETER(uint32, MaxNumProbes)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FUpdateNewProbesPass : SHADER_PERMUTATION_BOOL("UPDATE_NEW_PROBES_PASS");
	class FPersistentCache : SHADER_PERMUTATION_BOOL("PERSISTENT_CACHE");
	using FPermutationDomain = TShaderPermutationDomain<FUpdateNewProbesPass, FPersistentCache>;

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

IMPLEMENT_GLOBAL_SHADER(FAllocateUsedProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "AllocateUsedProbesCS", SF_Compute);


class FStoreNumNewProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStoreNumNewProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FStoreNumNewProbesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNumNewProbes)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceAllocator)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FStoreNumNewProbesCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "StoreNumNewProbesCS", SF_Compute);


class FClampProbeFreeListAllocatorCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClampProbeFreeListAllocatorCS)
	SHADER_USE_PARAMETER_STRUCT(FClampProbeFreeListAllocatorCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWProbeFreeListAllocator)
		SHADER_PARAMETER(uint32, MaxNumProbes)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClampProbeFreeListAllocatorCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "ClampProbeFreeListAllocatorCS", SF_Compute);

class FSetupProbeIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupProbeIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupProbeIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWClearProbePDFsIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWGenerateProbeTraceTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFilterProbesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCalculateProbeIrradianceIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWPrepareProbeOcclusionIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWFixupProbeBordersIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER(uint32, TraceFromProbesGroupSizeXY)
		SHADER_PARAMETER(uint32, FilterProbesGroupSizeXY)
		SHADER_PARAMETER(uint32, ClearProbePDFGroupSize)
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumNewProbes)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRadianceCacheHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
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

	class FDistantScene : SHADER_PERMUTATION_BOOL("TRACE_DISTANT_SCENE");
	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");

	using FPermutationDomain = TShaderPermutationDomain<FDistantScene, FDynamicSkyLight>;

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


class FCopyProbesAndFixupBordersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyProbesAndFixupBordersCS)
	SHADER_USE_PARAMETER_STRUCT(FCopyProbesAndFixupBordersCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWFinalRadianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceProbeAtlasTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		RDG_BUFFER_ACCESS(FixupProbeBordersIndirectArgs, ERHIAccess::IndirectArgs)
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

IMPLEMENT_GLOBAL_SHADER(FCopyProbesAndFixupBordersCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "CopyProbesAndFixupBordersCS", SF_Compute);


class FGenerateMipLevelCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGenerateMipLevelCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipLevelCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWFinalRadianceAtlasMip)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, FinalRadianceAtlasParentMip)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER(uint32, MipLevel)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		RDG_BUFFER_ACCESS(FixupProbeBordersIndirectArgs, ERHIAccess::IndirectArgs)
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

IMPLEMENT_GLOBAL_SHADER(FGenerateMipLevelCS, "/Engine/Private/Lumen/LumenRadianceCache.usf", "GenerateMipLevelCS", SF_Compute);


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

		const FVector ClipmapMin = Clipmap.Center - Clipmap.Extent;

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
	const FScene* Scene,
	const FViewInfo& View, 
	const FScreenProbeParameters* ScreenProbeParameters,
	FRDGBufferSRVRef BRDFProbabilityDensityFunctionSH,
	FMarkUsedRadianceCacheProbes MarkUsedRadianceCacheProbes,
	const void* MarkUsedProbesData,
	FRadianceCacheState& RadianceCacheState,
	LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters)
{
	if (GRadianceCacheUpdate != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenRadianceCache);
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
		}

		FRDGTextureRef DebugBRDFProbabilityDensityFunction = nullptr;

		if (RadianceCacheState.DebugBRDFProbabilityDensityFunction.IsValid())
		{
			DebugBRDFProbabilityDensityFunction = GraphBuilder.RegisterExternalTexture(RadianceCacheState.DebugBRDFProbabilityDensityFunction);
		}
		else
		{
			FRDGTextureDesc DebugBRDFProbabilityDensityFunctionDesc = FRDGTextureDesc::Create2D(
				FIntPoint(RadianceCacheInputs.ProbeAtlasResolutionInProbes * 8),
				PF_FloatRGB,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			DebugBRDFProbabilityDensityFunction = GraphBuilder.CreateTexture(DebugBRDFProbabilityDensityFunctionDesc, TEXT("Lumen.RadianceCache.DebugBRDFProbabilityDensityFunction"));
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

		// Mark indirection entries around positions that will be sampled by dependent features as used
		MarkUsedRadianceCacheProbes.ExecuteIfBound(GraphBuilder, View, RadianceCacheParameters, RadianceProbeIndirectionTextureUAV, MarkUsedProbesData);

		const bool bPersistentCache = !GRadianceCacheForceFullUpdate 
			&& View.ViewState 
			&& IsValidRef(RadianceCacheState.RadianceProbeIndirectionTexture)
			&& RadianceCacheState.RadianceProbeIndirectionTexture->GetDesc().GetSize() == RadianceProbeIndirectionTextureSize
			&& !bResizedHistoryState;

		FRDGBufferRef ProbeFreeListAllocator = nullptr;
		FRDGBufferRef ProbeFreeList = nullptr;
		FRDGBufferRef ProbeLastUsedFrame = nullptr;
		FRDGBufferRef ProbeWorldOffset = nullptr;
		const int32 MaxNumProbes = RadianceCacheInputs.ProbeAtlasResolutionInProbes.X * RadianceCacheInputs.ProbeAtlasResolutionInProbes.Y;

		if (IsValidRef(RadianceCacheState.ProbeFreeList) && RadianceCacheState.ProbeFreeList->Desc.NumElements == MaxNumProbes)
		{
			ProbeFreeListAllocator = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeListAllocator);
			ProbeFreeList = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeFreeList);
			ProbeLastUsedFrame = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeLastUsedFrame);
			ProbeWorldOffset = GraphBuilder.RegisterExternalBuffer(RadianceCacheState.ProbeWorldOffset);
		}
		else
		{
			ProbeFreeListAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(int32), 1), TEXT("Lumen.RadianceCache.ProbeFreeListAllocator"));
			ProbeFreeList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeFreeList"));
			ProbeLastUsedFrame = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeLastUsedFrame"));
			ProbeWorldOffset = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeWorldOffset"));
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
					PassParameters->LastFrameRadianceProbeCoordToWorldPositionScale[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterScale;
					PassParameters->LastFrameRadianceProbeCoordToWorldPositionBias[ClipmapIndex] = Clipmap.ProbeCoordToWorldCenterBias;
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

		FRDGBufferRef ProbeTraceData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4), MaxNumProbes), TEXT("Lumen.RadianceCache.ProbeTraceData"));

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
		FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, ProbeTraceAllocatorUAV, 0);

		FRDGBufferRef NumNewProbes = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.NumNewProbes"));

		// Update probe lighting in two passes:
		// The first operates on new probes (cache misses) which trace at a lower resolution when over budget.
		// The second operates on existing probes which need retracing to propagate lighting changes. These trace less often when new probe traces are over budget, but always full resolution.

		for (int32 UpdatePassIndex = 0; UpdatePassIndex < 2; UpdatePassIndex++)
		{
			const bool bUpdateNewProbes = UpdatePassIndex == 0;
			const bool bUpdateExistingProbes = UpdatePassIndex == 1;

			{
				FAllocateUsedProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateUsedProbesCS::FParameters>();
				PassParameters->RWRadianceProbeIndirectionTexture = RadianceProbeIndirectionTextureUAV;
				PassParameters->RWProbeLastUsedFrame = ProbeLastUsedFrameUAV;
				PassParameters->RWProbeAllocator = ProbeAllocatorUAV;
				PassParameters->RWProbeTraceAllocator = ProbeTraceAllocatorUAV;
				PassParameters->RWProbeTraceData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RWProbeFreeListAllocator = bPersistentCache ? ProbeFreeListAllocatorUAV : nullptr;
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->ProbeFreeList = bPersistentCache ? GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeFreeList, PF_R32_UINT)) : nullptr;
				PassParameters->FrameNumber = View.ViewState->GetFrameIndex();
				PassParameters->ProbesUpdateEveryNFrames = GRadianceCacheProbesUpdateEveryNFrames;
				PassParameters->MaxNumProbes = MaxNumProbes;
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;

				FAllocateUsedProbesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FAllocateUsedProbesCS::FUpdateNewProbesPass>(bUpdateNewProbes);
				PermutationVector.Set<FAllocateUsedProbesCS::FPersistentCache>(bPersistentCache);
				auto ComputeShader = View.ShaderMap->GetShader<FAllocateUsedProbesCS>(PermutationVector);

				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(RadianceProbeIndirectionTexture->Desc.GetSize(), FAllocateUsedProbesCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					bUpdateNewProbes ? RDG_EVENT_NAME("AllocateNewProbeTraces") : RDG_EVENT_NAME("AllocateExistingProbeTraces"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}

			if (bUpdateNewProbes)
			{
				FStoreNumNewProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStoreNumNewProbesCS::FParameters>();
				PassParameters->RWNumNewProbes = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(NumNewProbes, PF_R32_UINT));
				PassParameters->RWProbeTraceAllocator = ProbeTraceAllocatorUAV;
				auto ComputeShader = View.ShaderMap->GetShader<FStoreNumNewProbesCS>(0);

				const FIntVector GroupSize = FIntVector(1);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("StoreNumNewProbes"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}
		}

		FRDGBufferRef ClearProbePDFsIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(2), TEXT("Lumen.RadianceCache.ClearProbePDFsIndirectArgs"));
		FRDGBufferRef GenerateProbeTraceTilesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(3), TEXT("Lumen.RadianceCache.GenerateProbeTraceTilesIndirectArgs"));
		FRDGBufferRef ProbeTraceTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.RadianceCache.ProbeTraceTileAllocator"));
		FRDGBufferRef FilterProbesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(5), TEXT("Lumen.RadianceCache.FilterProbesIndirectArgs"));
		FRDGBufferRef CalculateProbeIrradianceIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(6), TEXT("Lumen.RadianceCache.CalculateProbeIrradianceIndirectArgs"));
		FRDGBufferRef PrepareProbeOcclusionIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(7), TEXT("Lumen.RadianceCache.PrepareProbeOcclusionIndirectArgs"));
		FRDGBufferRef FixupProbeBordersIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(8), TEXT("Lumen.RadianceCache.FixupProbeBordersIndirectArgs"));

		{
			FSetupProbeIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupProbeIndirectArgsCS::FParameters>();
			PassParameters->RWClearProbePDFsIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ClearProbePDFsIndirectArgs, PF_R32_UINT));
			PassParameters->RWGenerateProbeTraceTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(GenerateProbeTraceTilesIndirectArgs, PF_R32_UINT));
			PassParameters->RWProbeTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RWFilterProbesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(FilterProbesIndirectArgs, PF_R32_UINT));
			PassParameters->RWCalculateProbeIrradianceIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CalculateProbeIrradianceIndirectArgs, PF_R32_UINT));
			PassParameters->RWPrepareProbeOcclusionIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(PrepareProbeOcclusionIndirectArgs, PF_R32_UINT));
			PassParameters->RWFixupProbeBordersIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(FixupProbeBordersIndirectArgs, PF_R32_UINT));
			PassParameters->ProbeTraceAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceAllocator, PF_R32_UINT));
			PassParameters->TraceFromProbesGroupSizeXY = FRadianceCacheTraceFromProbesCS::GetGroupSize();
			PassParameters->FilterProbesGroupSizeXY = FFilterProbeRadianceWithGatherCS::GetGroupSize();
			PassParameters->ClearProbePDFGroupSize = FClearProbePDFs::GetGroupSize();
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

		{
			FGenerateProbeTraceTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateProbeTraceTilesCS::FParameters>();
			PassParameters->RWProbeTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RWProbeTraceTileData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->RadianceProbeSH_PDF = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RadianceProbeSH_PDF, PF_R32_SINT));
			PassParameters->NumNewProbes = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(NumNewProbes, PF_R32_UINT));
			PassParameters->SupersampleTileBRDFThreshold = GLumenRadianceCacheSupersampleTileBRDFThreshold;
			PassParameters->SupersampleDistanceFromCameraSq = GLumenRadianceCacheSupersampleDistanceFromCamera * GLumenRadianceCacheSupersampleDistanceFromCamera;
			PassParameters->DownsampleDistanceFromCameraSq = GLumenRadianceCacheDownsampleDistanceFromCamera * GLumenRadianceCacheDownsampleDistanceFromCamera;

			PassParameters->RWDebugBRDFProbabilityDensityFunction = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DebugBRDFProbabilityDensityFunction));
			PassParameters->DebugProbeBRDFOctahedronResolution = 8;

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
		FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(4), TEXT("Lumen.RadianceCache.RadianceCacheHardwareRayTracingIndirectArgs"));

		{
			FSetupTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupTraceFromProbesCS::FParameters>();
			PassParameters->RWTraceProbesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(TraceProbesIndirectArgs, PF_R32_UINT));
			PassParameters->RWRadianceCacheHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(RadianceCacheHardwareRayTracingIndirectArgs, PF_R32_UINT));
			PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			auto ComputeShader = View.ShaderMap->GetShader<FSetupTraceFromProbesCS>(0);

			const FIntVector GroupSize = FIntVector(1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SetupTraceFromProbesCS"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		FRDGTextureUAVRef RadianceProbeAtlasTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RadianceProbeAtlasTextureSource));

		if (Lumen::UseHardwareRayTracedRadianceCache())
		{
			float DiffuseConeHalfAngle = -1.0f;
			RenderLumenHardwareRayTracingRadianceCache(
				GraphBuilder,
				GetSceneTextureParameters(GraphBuilder),
				View,
				TracingInputs,
				RadianceCacheParameters,
				DiffuseConeHalfAngle,
				MaxNumProbes,
				MaxProbeTraceTileResolution,
				ProbeTraceData,
				ProbeTraceTileData,
				ProbeTraceTileAllocator,
				TraceProbesIndirectArgs,
				RadianceCacheHardwareRayTracingIndirectArgs,
				RadianceProbeAtlasTextureUAV,
				DepthProbeTextureUAV
			);
		}
		else
		{
			FRadianceCacheTraceFromProbesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadianceCacheTraceFromProbesCS::FParameters>();
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, -1.0f);
			PassParameters->RWRadianceProbeAtlasTexture = RadianceProbeAtlasTextureUAV;
			PassParameters->RWDepthProbeAtlasTexture = DepthProbeTextureUAV;
			PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
			PassParameters->ProbeTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileData, PF_R32G32_UINT));
			PassParameters->ProbeTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceTileAllocator, PF_R32_UINT));
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->TraceProbesIndirectArgs = TraceProbesIndirectArgs;

			FRadianceCacheTraceFromProbesCS::FPermutationDomain PermutationVector;
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
				PassParameters->CalculateProbeIrradianceIndirectArgs = CalculateProbeIrradianceIndirectArgs;

				auto ComputeShader = View.ShaderMap->GetShader<FCalculateProbeIrradianceCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CalculateProbeIrradiance Res=%ux%u", RadianceCacheInputs.IrradianceProbeResolution, RadianceCacheInputs.IrradianceProbeResolution),
					ComputeShader,
					PassParameters,
					CalculateProbeIrradianceIndirectArgs,
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

			{
				FCopyProbesAndFixupBordersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCopyProbesAndFixupBordersCS::FParameters>();
				PassParameters->RWFinalRadianceAtlas = FinalRadianceAtlasUAV;
				PassParameters->RadianceProbeAtlasTexture = RadianceProbeAtlasTexture;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->FixupProbeBordersIndirectArgs = FixupProbeBordersIndirectArgs;

				auto ComputeShader = View.ShaderMap->GetShader<FCopyProbesAndFixupBordersCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CopyProbesAndFixupBorders"),
					ComputeShader,
					PassParameters,
					FixupProbeBordersIndirectArgs,
					0);
			}

			for (uint32 MipLevel = 1; MipLevel <= RadianceCacheInputs.FinalRadianceAtlasMaxMip; MipLevel++)
			{
				FGenerateMipLevelCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateMipLevelCS::FParameters>();
				PassParameters->RWFinalRadianceAtlasMip = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(FinalRadianceAtlas, MipLevel));
				PassParameters->FinalRadianceAtlasParentMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(FinalRadianceAtlas, MipLevel - 1));
				PassParameters->RadianceCacheParameters = RadianceCacheParameters;
				PassParameters->ProbeTraceData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ProbeTraceData, PF_A32B32G32R32F));
				PassParameters->MipLevel = MipLevel;
				PassParameters->FixupProbeBordersIndirectArgs = FixupProbeBordersIndirectArgs;
				PassParameters->View = View.ViewUniformBuffer;

				auto ComputeShader = View.ShaderMap->GetShader<FGenerateMipLevelCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GenerateMipLevel"),
					ComputeShader,
					PassParameters,
					FixupProbeBordersIndirectArgs, //@todo - dispatch the right number of threads for this mip instead of mip0
					0);
			}

			RadianceCacheParameters.RadianceCacheFinalRadianceAtlas = FinalRadianceAtlas;
		}

		if (bPersistentCache)
		{
			FClampProbeFreeListAllocatorCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClampProbeFreeListAllocatorCS::FParameters>();
			PassParameters->RWProbeFreeListAllocator = ProbeFreeListAllocatorUAV;
			PassParameters->MaxNumProbes = MaxNumProbes;
			auto ComputeShader = View.ShaderMap->GetShader<FClampProbeFreeListAllocatorCS>(0);

			const FIntVector GroupSize = FIntVector(1);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClampProbeFreeListAllocator"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		RadianceCacheState.ProbeFreeListAllocator = GraphBuilder.ConvertToExternalBuffer(ProbeFreeListAllocator);
		RadianceCacheState.ProbeFreeList = GraphBuilder.ConvertToExternalBuffer(ProbeFreeList);
		RadianceCacheState.ProbeAllocator = GraphBuilder.ConvertToExternalBuffer(ProbeAllocator);
		RadianceCacheState.ProbeLastUsedFrame = GraphBuilder.ConvertToExternalBuffer(ProbeLastUsedFrame);
		RadianceCacheState.ProbeWorldOffset = GraphBuilder.ConvertToExternalBuffer(ProbeWorldOffset);
		RadianceCacheState.RadianceProbeIndirectionTexture = GraphBuilder.ConvertToExternalTexture(RadianceProbeIndirectionTexture);
		RadianceCacheState.DepthProbeAtlasTexture = GraphBuilder.ConvertToExternalTexture(DepthProbeAtlasTexture);
		RadianceCacheState.RadianceProbeAtlasTexture = GraphBuilder.ConvertToExternalTexture(RadianceProbeAtlasTextureSource);
		RadianceCacheState.DebugBRDFProbabilityDensityFunction = GraphBuilder.ConvertToExternalTexture(DebugBRDFProbabilityDensityFunction);

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
