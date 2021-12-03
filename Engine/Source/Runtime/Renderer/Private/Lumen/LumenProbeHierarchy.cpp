// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenProbeHierarchy.h"
#include "RenderGraph.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "DeferredShadingRenderer.h"
#include "RendererModule.h" 
#include "Math/Halton.h"


namespace
{

static TAutoConsoleVariable<int32> CVarScreenSpaceProbeTracing(
	TEXT("r.Lumen.ProbeHierarchy.ScreenSpaceProbeTracing"), 1,
	TEXT("Whether to trace probes with screen space rays."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarProbeOcclusion(
	TEXT("r.Lumen.ProbeHierarchy.ProbeOcclusion"), 1,
	TEXT("Whether to do any probe occlusion."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTileClassification(
	TEXT("r.Lumen.ProbeHierarchy.TileClassification"), 1,
	TEXT("Whether to use tile classification for faster probe occlusion and probe hierarchy tracing."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAdditionalSpecularRayThreshold(
	TEXT("r.Lumen.ProbeHierarchy.AdditionalSpecularRayThreshold"), 0.4f,
	TEXT("Roughness treshold under which to shoot an additional ray for specular."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSGIProbeOcclusion(
	TEXT("r.Lumen.ProbeHierarchy.SSGIProbeOcclusion"), 1,
	TEXT("Whether to trace screen space rays to test probe occlusion."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVoxelDiffuseProbeOcclusion(
	TEXT("r.Lumen.ProbeHierarchy.VoxelDiffuseProbeOcclusion"), 1,
	TEXT("Whether to cone trace voxel to test diffuse probe occlusion."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHierarchyDepth(
	TEXT("r.Lumen.ProbeHierarchy.Depth"), 4,
	TEXT("Run time depth of the probe hierarchy (default to 4)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxProbeSuperSampling(
	TEXT("r.Lumen.ProbeHierarchy.MaxProbeSuperSampling"), 2,
	TEXT("Square root maximum of super sampling allowed of ray per texel of the probes' IBL (default to 2, power of two, min at 1, max at 4)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxProbeResolution(
	TEXT("r.Lumen.ProbeHierarchy.MaxProbeResolution"), 8,
	TEXT("Maximum resolution of the probes' IBL (default to 8, power of two, min at 4, max at 32)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLeafProbeSamplingDivisor(
	TEXT("r.Lumen.ProbeHierarchy.LeafProbeSamplingDivisor"), 1,
	TEXT("Divisor on the number of sample that should be done per texel of the probes' IBL for leaves of the hierarchy."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDiffuseIndirectMipLevel(
	TEXT("r.Lumen.ProbeHierarchy.DiffuseIndirect.MipLevel"), 1,
	TEXT("Sample the cosine emisphere in specific mip level of the cubemap of the probes to reduce noise when can't afford many rays."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCounterParrallaxError(
	TEXT("r.Lumen.ProbeHierarchy.CounterParrallaxError"), 1.0f,
	TEXT("How much parrallax error is tolerated between probe in the hierarchy. Higher value is higher quality, but more expensive."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAntiTileAliasing(
	TEXT("r.Lumen.ProbeHierarchy.AntiTileAliasing"), 1,
	TEXT("Whether to enable anti tile aliasing."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDebugAntiTileAliasingX(
	TEXT("r.Lumen.ProbeHierarchy.DebugAntiTileAliasingX"), -1,
	TEXT(""),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDebugAntiTileAliasingY(
	TEXT("r.Lumen.ProbeHierarchy.DebugAntiTileAliasingY"), -1,
	TEXT(""),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarEnableBentNormal(
	TEXT("r.Lumen.ProbeHierarchy.EnableBentNormal"), 1,
	TEXT("Whether to occlude GI by material's bent normal."),
	ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FCommonProbeDenoiserParameters, )
	SHADER_PARAMETER(FIntPoint, EmitTileStorageExtent)
	SHADER_PARAMETER(FIntPoint, ResolveTileStorageExtent)
END_SHADER_PARAMETER_STRUCT()

constexpr int32 kMaxAtlasMipCount = 2;
constexpr int32 kIBLBorderSize = 1;

class FScatterLeafProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScatterLeafProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FScatterLeafProbesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, TilePixelOffset)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ProjectedProbesOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ProjectedTileCountersOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<uint>, DepthMinMaxOutput, [2])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FScatterParentProbesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScatterParentProbesCS)
	SHADER_USE_PARAMETER_STRUCT(FScatterParentProbesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, ChildEmitTileCount)
		SHADER_PARAMETER(FIntPoint, ChildEmitTileOffset)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProjectedProbes)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<uint>, ParentProbesOutput, [LumenProbeHierarchy::kProbeMaxHierarchyDepth - 1])
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<uint>, ParentTileCountersOutput, [LumenProbeHierarchy::kProbeMaxHierarchyDepth - 1])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FReduceProbeDepthBoundsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReduceProbeDepthBoundsCS)
	SHADER_USE_PARAMETER_STRUCT(FReduceProbeDepthBoundsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ParentTileCount)
		SHADER_PARAMETER(FIntPoint, ParentTileOffset)
		SHADER_PARAMETER(FIntPoint, TileCount)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float2>, ParentTiledDepthBounds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, TiledDepthBoundsOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FAssignEmitAtomicTileOffsetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAssignEmitAtomicTileOffsetCS)
	SHADER_USE_PARAMETER_STRUCT(FAssignEmitAtomicTileOffsetCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, EmitAtomicTileCount)
		SHADER_PARAMETER(int32, HierarchyLevelId)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TileCounters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, TileOffsetsOutput)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GlobalCounterOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FBuildHierarchyInfoCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildHierarchyInfoCS)
	SHADER_USE_PARAMETER_STRUCT(FBuildHierarchyInfoCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SCALAR_ARRAY(int32, LevelResolutionArray, [LumenProbeHierarchy::kProbeMaxHierarchyDepth])
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeGlobalCounters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ProbeHierarchyInfoOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FBuildProbeArrayCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildProbeArrayCS)
	SHADER_USE_PARAMETER_STRUCT(FBuildProbeArrayCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, LevelParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, EmitTileCount)
		SHADER_PARAMETER(float, CounterParrallaxError)
		SHADER_PARAMETER(FIntPoint, TilePixelOffset)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeHierarchyInfoBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProjectedProbes)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, EmitAtomicTileProbeOffsets)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ProbeListPerEmitTileOutput)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ProbeArrayOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FDilateProbeResolveTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDilateProbeResolveTilesCS)
	SHADER_USE_PARAMETER_STRUCT(FDilateProbeResolveTilesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, EmitTileCount)
		SHADER_PARAMETER(FIntPoint, TileCount)
		SHADER_PARAMETER(FIntPoint, TileOffset)
		SHADER_PARAMETER(int32, HierarchyId)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProbeListPerEmitTile)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, TiledDepthBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestHZB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZB)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeArray)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ProbePerTilesOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FSetupSelectParentProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupSelectParentProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupSelectParentProbeCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SETUP_PASS"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchParametersOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FSelectParentProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSelectParentProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FSelectParentProbeCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER(FIntPoint, ParentTilePixelOffset)
		SHADER_PARAMETER(FIntPoint, ParentResolveTileBoundary)
		SHADER_PARAMETER(int32, ParentHierarchyId)
		SHADER_PARAMETER(int32, LevelId)

		RDG_BUFFER_ACCESS(DispatchParameters, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbePerResolveTiles)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ProbeArrayInout)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ProbeParentListOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FResolveProbeIndexesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FResolveProbeIndexesCS)
	SHADER_USE_PARAMETER_STRUCT(FResolveProbeIndexesCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, GlobalEmitTileClassificationOffset)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbePerResolveTiles)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, ResolvedIndexesOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ProbeOcclusionDistanceOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FMaskProbesDirectionsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMaskProbesDirectionsCS)
	SHADER_USE_PARAMETER_STRUCT(FMaskProbesDirectionsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, SamplePerPixel)
		SHADER_PARAMETER(float, AdditionalSpecularRayThreshold)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolvedProbeIndexes)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DiffuseSampleMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SpecularSampleMaskTexture)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ProbeArrayInout)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FSetupComposeProbeAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupComposeProbeAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupComposeProbeAtlasCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_SCALAR_ARRAY(int32, GroupPerProbesArray, [LumenProbeHierarchy::kProbeMaxHierarchyDepth])
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchParametersOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SETUP_PASS"), 0);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

class FComposeProbeAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComposeProbeAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FComposeProbeAtlasCS, FGlobalShader)
		
	class FDownsampleDim : SHADER_PERMUTATION_BOOL("DIM_DOWNSAMPLE");
	class FFinalDim : SHADER_PERMUTATION_BOOL("DIM_OUTPUT_FINAL_ATLAS");
	using FPermutationDomain = TShaderPermutationDomain<FDownsampleDim, FFinalDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, LevelParameters)
		SHADER_PARAMETER(float, InvSampleCountPerCubemapTexel)

		RDG_BUFFER_ACCESS(DispatchParameters, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeParentList)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, ProbeAtlasColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProbeAtlasSampleMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, ParentProbeAtlasColor)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float3>, ProbeAtlasColorMipOutput, [2])
	END_SHADER_PARAMETER_STRUCT()
};

class FTraceIndirectLightingProbeHierarchyCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTraceIndirectLightingProbeHierarchyCS)
	SHADER_USE_PARAMETER_STRUCT(FTraceIndirectLightingProbeHierarchyCS, FGlobalShader)
		
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, LevelParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER(FVector2f, FinalProbeAtlasPixelSize)
		SHADER_PARAMETER(int32, SamplePerPixel)
		SHADER_PARAMETER(float, fSamplePerPixel)
		SHADER_PARAMETER(float, fInvSamplePerPixel)
		SHADER_PARAMETER(int32, DiffuseIndirectMipLevel)
		SHADER_PARAMETER(float, AdditionalSpecularRayThreshold)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, FinalProbeAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CompressedDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolvedProbeIndexes)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DiffuseSampleMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SpecularSampleMaskTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DiffuseLightingOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecularLightingOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FProbeOcclusionTileClassificationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FProbeOcclusionTileClassificationCS)
	SHADER_USE_PARAMETER_STRUCT(FProbeOcclusionTileClassificationCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonProbeDenoiserParameters, CommonProbeDenoiserParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(FIntPoint, AtomicTileExtent)
		SHADER_PARAMETER(float, AdditionalSpecularRayThreshold)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, TileClassificationOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AtomicTileCounterOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, CompressedDepthBufferOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, CompressedRoughnessOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, CompressedShadingModelOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FProbeOcclusionAssignTileOffsetsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FProbeOcclusionAssignTileOffsetsCS)
	SHADER_USE_PARAMETER_STRUCT(FProbeOcclusionAssignTileOffsetsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, AtomicTileCount)
		SHADER_PARAMETER(FIntPoint, AtomicTileExtent)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AtomicTileCounters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AtomicTileOffsetsOutput)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GlobalCounterOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FProbeOcclusionBuildTileListsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FProbeOcclusionBuildTileListsCS)
	SHADER_USE_PARAMETER_STRUCT(FProbeOcclusionBuildTileListsCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, TileCount)
		SHADER_PARAMETER(FIntPoint, AtomicTileExtent)
		SHADER_PARAMETER(int32, TileListMaxLength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TileClassificationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, AtomicTileOffsetTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TileListOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

// Generic probe hierarchy shaders
IMPLEMENT_GLOBAL_SHADER(FScatterLeafProbesCS,           "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyScatterLeaves.usf",            "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FScatterParentProbesCS,         "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyScatterParentHierarchy.usf",   "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FReduceProbeDepthBoundsCS,      "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyReduceDepthBounds.usf",        "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAssignEmitAtomicTileOffsetCS,  "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyAssignAtomicTileOffset.usf",   "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FBuildHierarchyInfoCS,          "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyBuildHierarchyInfo.usf",       "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FBuildProbeArrayCS,             "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyBuildProbeArray.usf",          "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDilateProbeResolveTilesCS,     "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyDilateResolveTiles.usf",       "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSetupSelectParentProbeCS,      "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchySelectParent.usf",             "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSelectParentProbeCS,           "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchySelectParent.usf",             "MainCS", SF_Compute);

// Indirect lighting specific shaders before probe occlusion.
IMPLEMENT_GLOBAL_SHADER(FResolveProbeIndexesCS,              "/Engine/Private/Lumen/FinalGather/LumenResolveProbeIndex.usf",                "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FProbeOcclusionTileClassificationCS, "/Engine/Private/Lumen/FinalGather/LumenProbeOcclusionTileClassification.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FProbeOcclusionAssignTileOffsetsCS,  "/Engine/Private/Lumen/FinalGather/LumenProbeOcclusionAssignTileOffsets.usf",  "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FProbeOcclusionBuildTileListsCS,     "/Engine/Private/Lumen/FinalGather/LumenProbeOcclusionBuildTileLists.usf",     "MainCS", SF_Compute);

// Indirect lighting specific shaders after probe occlusion.
IMPLEMENT_GLOBAL_SHADER(FMaskProbesDirectionsCS,                "/Engine/Private/Lumen/FinalGather/LumenMaskProbesDirections.usf",           "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSetupComposeProbeAtlasCS,              "/Engine/Private/Lumen/ProbeHierarchy/LumenComposeProbeAtlas.usf",           "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FComposeProbeAtlasCS,                   "/Engine/Private/Lumen/ProbeHierarchy/LumenComposeProbeAtlas.usf",           "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTraceIndirectLightingProbeHierarchyCS, "/Engine/Private/Lumen/FinalGather/LumenSampleProbeHierarchy.usf",           "MainCS", SF_Compute);

} // namespace


namespace LumenProbeHierarchy
{

const TCHAR* GetEventName(EProbeOcclusionClassification TileClassification)
{
	static const TCHAR* const kEventNames[] = {
		TEXT("Unlit"),
		TEXT("DefaultLitOnly"),
		TEXT("SimpleShading"),
		TEXT("SimpleShadingSpecular"),
		TEXT("SimpleShadingBentNormal"),
		TEXT("ComplexShadingBentNormal"),
	};
	static_assert(UE_ARRAY_COUNT(kEventNames) == int32(EProbeOcclusionClassification::MAX), "Fix me");
	return kEventNames[int32(TileClassification)];
}

FIndirectLightingProbeOcclusionOutputParameters CreateProbeOcclusionOutputParameters(
	FRDGBuilder& GraphBuilder,
	const FIndirectLightingProbeOcclusionParameters& ProbeOcclusionParameters,
	ERDGUnorderedAccessViewFlags ResourceViewFlags)
{
	FIndirectLightingProbeOcclusionOutputParameters OutputParameters;
	OutputParameters.DiffuseLightingOutput = GraphBuilder.CreateUAV(ProbeOcclusionParameters.DiffuseLighting, ResourceViewFlags);
	OutputParameters.SpecularLightingOutput = GraphBuilder.CreateUAV(ProbeOcclusionParameters.SpecularLighting, ResourceViewFlags);
	OutputParameters.DiffuseSampleMaskOutput = GraphBuilder.CreateUAV(ProbeOcclusionParameters.DiffuseSampleMask, ResourceViewFlags);
	OutputParameters.SpecularSampleMaskOutput = GraphBuilder.CreateUAV(ProbeOcclusionParameters.SpecularSampleMask, ResourceViewFlags);
	return OutputParameters;
}

FHierarchyLevelParameters GetLevelParameters(const FHierarchyParameters& HierarchyParameters, int32 HierarchyLevelId)
{
	check(HierarchyLevelId < HierarchyParameters.HierarchyDepth);
	FHierarchyLevelParameters LevelParameters;
	LevelParameters.LevelId = HierarchyLevelId;
	LevelParameters.LevelSuperSampling = GET_SCALAR_ARRAY_ELEMENT(HierarchyParameters.LevelSuperSamplingArray, HierarchyLevelId);
	LevelParameters.LevelResolution = GET_SCALAR_ARRAY_ELEMENT(HierarchyParameters.LevelResolutionArray, HierarchyLevelId);
	return LevelParameters;
}

EProbeTracingPermutation GetProbeTracingPermutation(const FHierarchyLevelParameters& LevelParameters)
{
	if (LevelParameters.LevelResolution * LevelParameters.LevelSuperSampling < 8)
	{
		return EProbeTracingPermutation::MultipleProbePerWave;
	}
	else
	{
		return EProbeTracingPermutation::SingleProbePerWave;
	}
}

float ComputeHierarchyLevelConeAngle(const FHierarchyLevelParameters& LevelParameters)
{
	int32 RaysPerFaceBorder = LevelParameters.LevelResolution *  LevelParameters.LevelSuperSampling;
	const int32 FaceCountOnEquator = 4;
	const float EquatorLength = 2.0f * PI;
	const float ConeAngleToHalfConeAngle = 0.5f;

	return  (ConeAngleToHalfConeAngle * EquatorLength) / (FaceCountOnEquator * RaysPerFaceBorder * FMath::Sqrt(static_cast<float>(kMaxParentProbeCount)));
}

FRDGTextureRef ComposeFinalProbeAtlas(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* GlobalShaderMap,
	const LumenProbeHierarchy::FHierarchyParameters& ProbeHierachyParameters,
	const LumenProbeHierarchy::FIndirectLightingAtlasParameters& IndirectLightingAtlasParameters,
	FRDGBufferRef ProbeParentList)
{
	RDG_EVENT_SCOPE(GraphBuilder, "ComposeProbeAtlas");

	FRDGBufferRef DispatchParameters = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(LumenProbeHierarchy::kProbeMaxHierarchyDepth),
		TEXT("ProbeHierarchy.ReduceProbeAtlasDispatch"));

	{
		FSetupComposeProbeAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupComposeProbeAtlasCS::FParameters>();
		PassParameters->HierarchyParameters = ProbeHierachyParameters;

		for (int32 HierarchyLevelId = 0; HierarchyLevelId < LumenProbeHierarchy::kProbeMaxHierarchyDepth; HierarchyLevelId++)
		{
			if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
			{
				FHierarchyLevelParameters LevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, HierarchyLevelId);
				int32 ResolutionMultiplier = LevelParameters.LevelResolution / LumenProbeHierarchy::kProbeResolution;
				GET_SCALAR_ARRAY_ELEMENT(PassParameters->GroupPerProbesArray, HierarchyLevelId) = ResolutionMultiplier * ResolutionMultiplier;
			}
			else
			{
				GET_SCALAR_ARRAY_ELEMENT(PassParameters->GroupPerProbesArray, HierarchyLevelId) = 0;
			}
		}

		PassParameters->DispatchParametersOutput = GraphBuilder.CreateUAV(DispatchParameters);

		TShaderMapRef<FSetupComposeProbeAtlasCS> ComputeShader(GlobalShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupComposeProbeAtlas"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FRDGTextureRef FinalProbeAtlas;
	{
		FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
			FIntPoint(
				ProbeHierachyParameters.ProbeAtlasGridSize.X * (kProbeResolution + kIBLBorderSize * 2) * 2,
				ProbeHierachyParameters.ProbeAtlasGridSize.Y * (kProbeResolution + kIBLBorderSize * 2) * 3),
			PF_FloatR11G11B10,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		ProbeAtlasDesc.NumMips = 2;
		FinalProbeAtlas = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("ProbeHierarchy.FinalProbeAtlas"));
	}

	FRDGTextureRef ParentProbeAtlasColor = IndirectLightingAtlasParameters.ProbeAtlasColor;

	// Compose the atlas, starting from the highest.
	for (int32 HierarchyLevelId = FMath::Max(ProbeHierachyParameters.HierarchyDepth - 2, 0); HierarchyLevelId >= 0; HierarchyLevelId--)
	{
		FRDGTextureRef NewProbeAtlasColor;

		if (HierarchyLevelId == 0)
		{
			NewProbeAtlasColor = FinalProbeAtlas;
		}
		else
		{
			NewProbeAtlasColor = GraphBuilder.CreateTexture(IndirectLightingAtlasParameters.ProbeAtlasColor->Desc, IndirectLightingAtlasParameters.ProbeAtlasColor->Name);
		}

		FComposeProbeAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComposeProbeAtlasCS::FParameters>();
		PassParameters->HierarchyParameters = ProbeHierachyParameters;
		PassParameters->LevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, HierarchyLevelId);
		PassParameters->InvSampleCountPerCubemapTexel = 1.0f / float(PassParameters->LevelParameters.LevelSuperSampling * PassParameters->LevelParameters.LevelSuperSampling);

		PassParameters->DispatchParameters = DispatchParameters;
		PassParameters->ProbeParentList = GraphBuilder.CreateSRV(ProbeParentList);
		PassParameters->ProbeAtlasColor = IndirectLightingAtlasParameters.ProbeAtlasColor;
		PassParameters->ProbeAtlasSampleMask = IndirectLightingAtlasParameters.ProbeAtlasSampleMask;
		PassParameters->ParentProbeAtlasColor = ParentProbeAtlasColor;

		for (int32 MipLevel = 0; MipLevel < NewProbeAtlasColor->Desc.NumMips; MipLevel++)
		{
			PassParameters->ProbeAtlasColorMipOutput[MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewProbeAtlasColor, MipLevel));
		}

		bool bDownsample2x = false;

		if (HierarchyLevelId + 1 < ProbeHierachyParameters.HierarchyDepth)
		{
			FHierarchyLevelParameters ParentLevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, HierarchyLevelId + 1);
			bDownsample2x = ParentLevelParameters.LevelResolution != PassParameters->LevelParameters.LevelResolution;
		}

		FComposeProbeAtlasCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComposeProbeAtlasCS::FDownsampleDim>(bDownsample2x);
		PermutationVector.Set<FComposeProbeAtlasCS::FFinalDim>(HierarchyLevelId == 0);

		TShaderMapRef<FComposeProbeAtlasCS> ComputeShader(GlobalShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComposeProbeAtlas(Level=%d%s%s)",
				HierarchyLevelId,
				PermutationVector.Get<FComposeProbeAtlasCS::FDownsampleDim>() ? TEXT(" Downsample") : TEXT(""),
				PermutationVector.Get<FComposeProbeAtlasCS::FFinalDim>() ? TEXT(" FinalAtlas") : TEXT("")),
			ComputeShader,
			PassParameters,
			DispatchParameters,
			/* IndirectArgOffset = */ sizeof(FRHIDispatchIndirectParameters) * HierarchyLevelId);

		ParentProbeAtlasColor = NewProbeAtlasColor;
	}

	return FinalProbeAtlas;
} // ComposeFinalProbeAtlas()


} // namespace LumenProbeHierarchy


FRDGTextureUAVRef CreateProbeHierarchyDebugOutputUAV(
	FRDGBuilder& GraphBuilder,
	const FIntPoint& Extent,
	const TCHAR* DebugName)
{
	LLM_SCOPE_BYTAG(Lumen);
#if 1
	FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2D(
		Extent,
		PF_FloatRGBA,
		FClearValueBinding::Transparent,
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef DebugTexture = GraphBuilder.CreateTexture(DebugOutputDesc, DebugName);

	FRDGTextureUAVRef DebugUAV = GraphBuilder.CreateUAV(DebugTexture);
	//AddClearUAVPass(GraphBuilder, DebugUAV, FLinearColor::Transparent);
	return DebugUAV;
};
#else
	return FRDGTextureUAVRef(nullptr);
};
#endif

DECLARE_GPU_STAT(LumenProbeDenoiser);


FSSDSignalTextures FDeferredShadingSceneRenderer::RenderLumenProbeHierarchy(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	FLumenSceneFrameTemporaries& FrameTemporaries,
	const HybridIndirectLighting::FCommonParameters& CommonParameters,
	const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColorMip,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos)
{
	using namespace LumenProbeHierarchy;

	LLM_SCOPE_BYTAG(Lumen);
	RDG_GPU_STAT_SCOPE(GraphBuilder, LumenProbeDenoiser);

	const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);

	const bool bAntiTileAliasing = CVarAntiTileAliasing.GetValueOnRenderThread() != 0 && View.ViewState;

	const FIntPoint SceneBufferExtent = SceneTextures.Config.Extent;

	const int32 MaxHierarchDepth = kProbeMaxHierarchyDepth;
	const int32 HierarchyDepth = FMath::Clamp(CVarHierarchyDepth.GetValueOnRenderThread(), 1, MaxHierarchDepth);

	auto ComputeEmitTileSize = [&](int32 HierarchyLevelId)
	{
		check(HierarchyLevelId >= 0);
		check(HierarchyLevelId < HierarchyDepth);
		return kProbeEmitTileSize << HierarchyLevelId;
	};

	auto ComputeResolveTileSize = [&](int32 HierarchyLevelId)
	{
		check(HierarchyLevelId >= 0);
		check(HierarchyLevelId < HierarchyDepth);
		return kProbeTileClassificationSize << HierarchyLevelId;
	};

	auto ComputeTileCount = [&](const FIntPoint& ViewSize, int32 TileSize)
	{
	check(FMath::IsPowerOfTwo(TileSize));
	FIntPoint TileCount = FIntPoint::DivideAndRoundUp(ViewSize, TileSize);

	if (bAntiTileAliasing)
	{
		TileCount.X += 1;
		TileCount.Y += 1;
	}

	return TileCount;
	};

	auto ComputeEmitTileCount = [&](const FIntPoint& ViewSize, int32 HierarchyLevelId)
	{
		return ComputeTileCount(ViewSize, ComputeEmitTileSize(HierarchyLevelId));
	};

	auto ComputeResolveTileCount = [&](const FIntPoint& ViewSize, int32 HierarchyLevelId)
	{
		return ComputeTileCount(ViewSize, ComputeResolveTileSize(HierarchyLevelId));
	};

	FIntPoint EmitTileStorageExtent = FIntPoint::DivideAndRoundUp(ComputeEmitTileCount(SceneBufferExtent, /* HierarchyLevelId = */ 0), 8) * 8;
	FIntPoint ResolveTileStorageExtent = FIntPoint::DivideAndRoundUp(ComputeResolveTileCount(SceneBufferExtent, /* HierarchyLevelId = */ 0), 8) * 8;

	FHierarchyParameters ProbeHierachyParameters;
	{
		ProbeHierachyParameters.HierarchyDepth = HierarchyDepth;
	ProbeHierachyParameters.CounterParrallaxError = FMath::Clamp(CVarCounterParrallaxError.GetValueOnRenderThread(), 1.0f, 16.0f);
	ProbeHierachyParameters.MaxProbeCount = EmitTileStorageExtent.X * EmitTileStorageExtent.Y * kProbeMaxEmitPerTile * 2;

	int32 MaxProbeSuperSampling = FMath::Clamp(CVarMaxProbeSuperSampling.GetValueOnRenderThread(), 1, 4);
	check(FMath::IsPowerOfTwo(MaxProbeSuperSampling));

	int32 MaxProbeResolution = CVarMaxProbeResolution.GetValueOnRenderThread();
	check(FMath::IsPowerOfTwo(MaxProbeResolution));

	int32 LeafProbeSamplingDivisor = CVarLeafProbeSamplingDivisor.GetValueOnRenderThread();
	check(FMath::IsPowerOfTwo(LeafProbeSamplingDivisor));

	// #lumen_todo: Reduce the probe tracing costs in the city fly through.
	if (GLumenFastCameraMode == 1)
	{
		// 3 level is the sweet spot to take down as many infinitely long rays as possible, without having level build and compositing slowing things down.
		ProbeHierachyParameters.HierarchyDepth = FMath::Min(ProbeHierachyParameters.HierarchyDepth, 3);

		// Keep one ray per texel for IBL on all levels to reduce number of infinitely long rays.
		MaxProbeSuperSampling = 1;

		// Keeps resolution of the probe consistent on all level to reduce number of infinitely long rays.
		MaxProbeResolution = 4;
	}

	for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
	{
		if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
		{
			int32 DesiredSamplesPerLevel0Texel = FMath::Max((1 << HierarchyLevelId) / LeafProbeSamplingDivisor, 1);

			int32 SuperSampling = FMath::Min(DesiredSamplesPerLevel0Texel, MaxProbeSuperSampling);
			int32 ResolutionMultiplier = DesiredSamplesPerLevel0Texel / SuperSampling;

			check(ResolutionMultiplier <= kMinAtlasGridSize);

			GET_SCALAR_ARRAY_ELEMENT(ProbeHierachyParameters.LevelResolutionArray, HierarchyLevelId) = FMath::Clamp(kProbeResolution * ResolutionMultiplier, 1, MaxProbeResolution);
			GET_SCALAR_ARRAY_ELEMENT(ProbeHierachyParameters.LevelSuperSamplingArray, HierarchyLevelId) = SuperSampling;
		}
		else
		{
			GET_SCALAR_ARRAY_ELEMENT(ProbeHierachyParameters.LevelResolutionArray, HierarchyLevelId) = kProbeResolution;
			GET_SCALAR_ARRAY_ELEMENT(ProbeHierachyParameters.LevelSuperSamplingArray, HierarchyLevelId) = 1;
		}
	}
	}

	FRDGTextureDesc ProjectedTileCountersDesc;
	{
		FIntPoint AtomicBufferExtent;
		AtomicBufferExtent.X = FMath::DivideAndRoundUp(EmitTileStorageExtent.X, 4) * 4;
		AtomicBufferExtent.Y = FMath::DivideAndRoundUp(EmitTileStorageExtent.Y, 4) * 4;

		ProjectedTileCountersDesc = FRDGTextureDesc::Create2D(
			AtomicBufferExtent,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
	}

	FCommonProbeDenoiserParameters CommonProbeDenoiserParameters;
	{
		CommonProbeDenoiserParameters.EmitTileStorageExtent = EmitTileStorageExtent;
		CommonProbeDenoiserParameters.ResolveTileStorageExtent = ResolveTileStorageExtent;
	}

	// Offset of the entire tile classification to avoid aliasing.
	FIntPoint GlobalEmitTileClassificationOffset = FIntPoint::ZeroValue;
	if (bAntiTileAliasing)
	{
		// The range of the offsets needs to fully jitter an emit tile size of the highest hierarchy depth.
		int32 TileClassificationOffsetRange = kProbeEmitTileSize << (ProbeHierachyParameters.HierarchyDepth - 1);

		int32 OffsetIndex = View.ViewState->FrameIndex % (TileClassificationOffsetRange * TileClassificationOffsetRange);

		GlobalEmitTileClassificationOffset.X = FMath::FloorToInt(Halton(OffsetIndex + 1, 2) * TileClassificationOffsetRange);
		GlobalEmitTileClassificationOffset.Y = FMath::FloorToInt(Halton(OffsetIndex + 1, 3) * TileClassificationOffsetRange);

		if (CVarDebugAntiTileAliasingX.GetValueOnRenderThread() >= 0)
		{
			GlobalEmitTileClassificationOffset.X = CVarDebugAntiTileAliasingX.GetValueOnRenderThread();
		}

		if (CVarDebugAntiTileAliasingY.GetValueOnRenderThread() >= 0)
		{
			GlobalEmitTileClassificationOffset.Y = CVarDebugAntiTileAliasingY.GetValueOnRenderThread();
		}

		GlobalEmitTileClassificationOffset.X = FMath::Clamp(GlobalEmitTileClassificationOffset.X, 0, TileClassificationOffsetRange - 1);
		GlobalEmitTileClassificationOffset.Y = FMath::Clamp(GlobalEmitTileClassificationOffset.Y, 0, TileClassificationOffsetRange - 1);
	}

	// Compute offset to apply to pixel coordinate of a specific group size such that: ThreadId = PixelPosition + TileOffset;
	auto ComputeTileClassificationOffset = [&](int32 ParentTileSize, int32 ChildTileSize)
	{
		check(FMath::IsPowerOfTwo(ParentTileSize));
		check(FMath::IsPowerOfTwo(ChildTileSize));
		check(ChildTileSize < ParentTileSize);
		return FIntPoint(
			(GlobalEmitTileClassificationOffset.X % ParentTileSize) / ChildTileSize,
			(GlobalEmitTileClassificationOffset.Y % ParentTileSize) / ChildTileSize);
	};

	// Compute the probe occlusion parameters
	FProbeOcclusionParameters ProbeOcclusionParameters;
	{
		ProbeOcclusionParameters.GlobalEmitTileClassificationOffset = GlobalEmitTileClassificationOffset;
		ProbeOcclusionParameters.ResolveTileCount = ComputeTileCount(View.ViewRect.Size(), kProbeTileClassificationSize);
	}

	FEmitProbeParameters EmitProbeParameters;
	EmitProbeParameters.MaxProbeCount = ProbeHierachyParameters.MaxProbeCount;
	EmitProbeParameters.EmitTileStorageExtent = CommonProbeDenoiserParameters.EmitTileStorageExtent;
	for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; ++HierarchyLevelId)
	{
		EmitProbeParameters.ProbeTileCount[HierarchyLevelId] = FIntPoint(0, 0);
		EmitProbeParameters.ProbeListsPerEmitTile[HierarchyLevelId] = nullptr;

		if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
		{
			EmitProbeParameters.ProbeTileCount[HierarchyLevelId] = ComputeEmitTileCount(View.ViewRect.Size(), HierarchyLevelId);
		}
	}

	// Build the frustum probe hierarchy from the depth buffer.
	TStaticArray<FRDGBufferRef, kProbeMaxHierarchyDepth> ProbeListsPerResolveTile;
	{
		RDG_EVENT_SCOPE(GraphBuilder, "BuildFrustumProbeHierarchy(%s)",
			bAntiTileAliasing ? TEXT("AntiTileAliasing") : TEXT(""));


		FIntPoint ProbesPerEmitTileStorage;
		ProbesPerEmitTileStorage.X = FMath::FloorToInt(FMath::Sqrt(static_cast<float>(kProbeMaxEmitPerTile)));
		ProbesPerEmitTileStorage.Y = kProbeMaxEmitPerTile / ProbesPerEmitTileStorage.X;

		TStaticArray<FRDGTextureRef, kProbeMaxHierarchyDepth> ProjectedTileCounters;
		{
			for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
			{
				if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
				{
					ProjectedTileCounters[HierarchyLevelId] = GraphBuilder.CreateTexture(ProjectedTileCountersDesc, TEXT("ProbeHierarchy.ProjectedTileCounters"));

					const uint32 ClearVal[4] = { 0, 0, 0, 0 };
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ProjectedTileCounters[HierarchyLevelId]), ClearVal);
				}
				else
				{
					ProjectedTileCounters[HierarchyLevelId] = nullptr;
				}
			}
		}

		TStaticArray<FRDGTextureRef, kProbeMaxHierarchyDepth> ProjectedProbes;
		FRDGTextureRef TiledDepthBounds;

		// Build the leaf probe of the hierarchy from depth buffer.
		{
			int32 TileSize = ComputeEmitTileSize(/* HierarchyLevelId = */ 0);
			FIntPoint TileCount = ComputeTileCount(View.ViewRect.Size(), TileSize);

			// Allocate resources
			{
				FRDGTextureDesc ProjectedProbesDesc = FRDGTextureDesc::Create2D(
					FIntPoint(EmitTileStorageExtent.X * ProbesPerEmitTileStorage.X, EmitTileStorageExtent.Y * ProbesPerEmitTileStorage.Y),
					PF_R32_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);

				ProjectedProbes[0] = GraphBuilder.CreateTexture(ProjectedProbesDesc, TEXT("ProbeHierarchy.BuildFrustum.ProjectedProbes"));
			}

			{
				int32 LastHierarchyLevelId = ProbeHierachyParameters.HierarchyDepth - 1;
				FRDGTextureDesc TiledDepthBoundsDesc = FRDGTextureDesc::Create2D(
					ComputeResolveTileCount(SceneBufferExtent, LastHierarchyLevelId) * (1 << LastHierarchyLevelId),
					PF_G16R16F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);
				TiledDepthBoundsDesc.NumMips = FMath::Max(ProbeHierachyParameters.HierarchyDepth, 2);

				TiledDepthBounds = GraphBuilder.CreateTexture(TiledDepthBoundsDesc, TEXT("ProbeHierarchy.BuildFrustum.TiledDepthBounds"));
			}

			FScatterLeafProbesCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FScatterLeafProbesCS::FParameters>();
			PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
			PassParameters->SceneTextures = CommonParameters.SceneTextures;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TilePixelOffset = ComputeTileClassificationOffset(/* ParentTileSize = */ TileSize, /* ChildTileSize = */ 1);

			PassParameters->ProjectedProbesOutput = GraphBuilder.CreateUAV(ProjectedProbes[0]);
			PassParameters->ProjectedTileCountersOutput = GraphBuilder.CreateUAV(ProjectedTileCounters[0]);

			for (int32 MipLevel = 0; MipLevel < 2; MipLevel++)
			{
				PassParameters->DepthMinMaxOutput[MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TiledDepthBounds, MipLevel));
			}

			PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
				GraphBuilder, SceneBufferExtent, TEXT("Debug.ProbeHierarchy.BuildFrustum.ScatterLeafProbes"));

			TShaderMapRef<FScatterLeafProbesCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ScatterLeafProbes %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				ComputeShader,
				PassParameters,
				FIntVector(TileCount.X, TileCount.Y, 1));

			//CommonProbeParameters.ProbeCountBuffer = GraphBuilder.CreateSRV(ProbeCount);
		}

		// Build hierarchy of probes
		for (int32 HierarchyLevelId = 1; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
		{
			if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
			{
				FRDGTextureDesc ProjectedProbesDesc = FRDGTextureDesc::Create2D(
					FIntPoint(EmitTileStorageExtent.X * ProbesPerEmitTileStorage.X, EmitTileStorageExtent.Y * ProbesPerEmitTileStorage.Y),
					PF_R32_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);

				ProjectedProbes[HierarchyLevelId] = GraphBuilder.CreateTexture(ProjectedProbesDesc, TEXT("ProbeHierarchy.BuildFrustum.ProjectedProbes"));
			}
			else
			{
				ProjectedProbes[HierarchyLevelId] = nullptr;
				continue;
			}

			const int32 ReduceTileSize = 2;
			const int32 TilePerGroup = 8 / ReduceTileSize;

			int32 TileSize = ComputeEmitTileSize(HierarchyLevelId);
			int32 ChildTileSize = TileSize / 2;

			FIntPoint TileCount = ComputeTileCount(View.ViewRect.Size(), TileSize);

			FScatterParentProbesCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FScatterParentProbesCS::FParameters>();
			PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->ChildEmitTileCount = ComputeTileCount(View.ViewRect.Size(), ChildTileSize);
			PassParameters->ChildEmitTileOffset = ComputeTileClassificationOffset(
				/* ParentTileSize = */ TileSize, /* ChildTileSize = */ ChildTileSize);

			PassParameters->ProjectedProbes = ProjectedProbes[HierarchyLevelId - 1];

			PassParameters->ParentProbesOutput[0] = GraphBuilder.CreateUAV(ProjectedProbes[HierarchyLevelId]);
			PassParameters->ParentTileCountersOutput[0] = GraphBuilder.CreateUAV(ProjectedTileCounters[HierarchyLevelId]);
			PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
				GraphBuilder,
				FIntPoint(EmitTileStorageExtent.X * ProbesPerEmitTileStorage.X, EmitTileStorageExtent.Y * ProbesPerEmitTileStorage.Y),
				TEXT("Debug.ProbeHierarchy.BuildFrustum.ScatterParentProbes"));

			TShaderMapRef<FScatterParentProbesCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ScatterParentProbes(Level=%i) %dx%d", HierarchyLevelId, TileCount.X, TileCount.Y),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(TileCount, TilePerGroup));
		}

		// Reduce depth bounds so the tile classification dilatation prune useless probes for higher hierarchy levels.
		for (int32 HierarchyLevelId = 2; HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth; HierarchyLevelId++)
		{
			int32 ParentTileSize = ComputeResolveTileSize(HierarchyLevelId - 1);
			int32 TileSize = ComputeResolveTileSize(HierarchyLevelId);

			FReduceProbeDepthBoundsCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FReduceProbeDepthBoundsCS::FParameters>();
			PassParameters->ParentTileCount = ComputeTileCount(View.ViewRect.Size(), ParentTileSize);
			PassParameters->ParentTileOffset = ComputeTileClassificationOffset(TileSize, ParentTileSize);
			PassParameters->TileCount = ComputeTileCount(View.ViewRect.Size(), TileSize);
			PassParameters->ParentTiledDepthBounds = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(TiledDepthBounds, HierarchyLevelId - 1));
			PassParameters->TiledDepthBoundsOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TiledDepthBounds, HierarchyLevelId));

			TShaderMapRef<FReduceProbeDepthBoundsCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReduceProbeDepthBounds(Level=%i) %dx%d", HierarchyLevelId, PassParameters->TileCount.X, PassParameters->TileCount.Y),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(PassParameters->TileCount, 8));
		}

		FRDGBufferRef ProbeGlobalCountersBuffer;
		{
			ProbeGlobalCountersBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), kProbeMaxHierarchyDepth),
				TEXT("ProbeHierarchy.BuildFrustum.GlobalProbeCounters"));

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ProbeGlobalCountersBuffer, PF_R32_UINT), 0);
		}

		TStaticArray<FRDGTextureRef, kProbeMaxHierarchyDepth> ProjectedTileOffsets;
		{
			FRDGBufferUAVRef GlobalCounterOutput = GraphBuilder.CreateUAV(
				ProbeGlobalCountersBuffer, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);

			for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
			{
				if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
				{
					ProjectedTileOffsets[HierarchyLevelId] = GraphBuilder.CreateTexture(
						ProjectedTileCountersDesc, TEXT("ProbeHierarchy.BuildFrustum.ProjectedTileOffsets"));
				}
				else
				{
					ProjectedTileOffsets[HierarchyLevelId] = nullptr;
					continue;
				}

				FIntPoint EmitTileCount = ComputeEmitTileCount(View.ViewRect.Size(), HierarchyLevelId);
				FIntPoint EmitAtomicTileCount = FIntPoint::DivideAndRoundUp(EmitTileCount, 8);

				FAssignEmitAtomicTileOffsetCS::FParameters* PassParameters =
					GraphBuilder.AllocParameters<FAssignEmitAtomicTileOffsetCS::FParameters>();
				PassParameters->EmitAtomicTileCount = EmitAtomicTileCount;
				PassParameters->HierarchyLevelId = HierarchyLevelId;

				PassParameters->TileCounters = ProjectedTileCounters[HierarchyLevelId];
				PassParameters->TileOffsetsOutput = GraphBuilder.CreateUAV(ProjectedTileOffsets[HierarchyLevelId]);
				PassParameters->GlobalCounterOutput = GlobalCounterOutput;
				PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
					GraphBuilder,
					ProjectedTileCountersDesc.Extent,
					TEXT("Debug.ProbeHierarchy.BuildFrustum.AssignEmitAtomicTileOffsets"));

				TShaderMapRef<FAssignEmitAtomicTileOffsetCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("AssignEmitAtomicTileOffsets(Level=%i) %dx%d",
						HierarchyLevelId, PassParameters->EmitAtomicTileCount.X, PassParameters->EmitAtomicTileCount.Y),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(PassParameters->EmitAtomicTileCount, 8));
			}
		}

		// Builds final infos about each hierarchy
		{
			FRDGBufferRef ProbeHierarchyInfoBuffer;

			ProbeHierarchyInfoBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 3 * kProbeMaxHierarchyDepth),
				TEXT("ProbeHierarchy.ProbeHierarchyInfo"));

			FBuildHierarchyInfoCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildHierarchyInfoCS::FParameters>();
			PassParameters->LevelResolutionArray = ProbeHierachyParameters.LevelResolutionArray;
			PassParameters->ProbeGlobalCounters = GraphBuilder.CreateSRV(ProbeGlobalCountersBuffer, PF_R32_UINT);
			PassParameters->ProbeHierarchyInfoOutput = GraphBuilder.CreateUAV(ProbeHierarchyInfoBuffer);

			TShaderMapRef<FBuildHierarchyInfoCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BuildHierarchyInfo"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));

			ProbeHierachyParameters.ProbeHierarchyInfoBuffer = GraphBuilder.CreateSRV(ProbeHierarchyInfoBuffer);
		}

		// Builds final probe array
		TStaticArray<FRDGTextureRef, kProbeMaxHierarchyDepth> ProbeListsPerEmitTile;
		{
			FRDGBufferRef ProbeArray = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f) * 2, ProbeHierachyParameters.MaxProbeCount),
				TEXT("ProbeHierarchy.ProbeArray"));

			FRDGBufferUAVRef ProbeArrayOutput = GraphBuilder.CreateUAV(ProbeArray, ERDGUnorderedAccessViewFlags::SkipBarrier);

			for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
			{
				if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
				{
					FRDGTextureDesc ProbeListPerEmitTileDesc = FRDGTextureDesc::Create2D(
						FIntPoint(EmitTileStorageExtent.X * ProbesPerEmitTileStorage.X, EmitTileStorageExtent.Y * ProbesPerEmitTileStorage.Y),
						PF_R32_UINT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV);

					ProbeListsPerEmitTile[HierarchyLevelId] = GraphBuilder.CreateTexture(
						ProbeListPerEmitTileDesc,
						TEXT("ProbeHierarchy.BuildFrustum.ProbeListsPerEmitTile"));
				}
				else
				{
					ProbeListsPerEmitTile[HierarchyLevelId] = nullptr;
					continue;
				}

				int32 TileSize = ComputeEmitTileSize(HierarchyLevelId);
				FIntPoint TileCount = ComputeTileCount(View.ViewRect.Size(), TileSize);

				FBuildProbeArrayCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildProbeArrayCS::FParameters>();
				PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
				PassParameters->LevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, HierarchyLevelId);
				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->EmitTileCount = TileCount;
				PassParameters->CounterParrallaxError = ProbeHierachyParameters.CounterParrallaxError;
				PassParameters->TilePixelOffset = ComputeTileClassificationOffset(/* ParentTileSize = */ TileSize, /* ChildTileSize = */ 1);

				PassParameters->ProbeHierarchyInfoBuffer = ProbeHierachyParameters.ProbeHierarchyInfoBuffer;
				PassParameters->ProjectedProbes = ProjectedProbes[HierarchyLevelId];
				PassParameters->EmitAtomicTileProbeOffsets = ProjectedTileOffsets[HierarchyLevelId];

				PassParameters->ProbeListPerEmitTileOutput = GraphBuilder.CreateUAV(ProbeListsPerEmitTile[HierarchyLevelId]);
				PassParameters->ProbeArrayOutput = ProbeArrayOutput;
				PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
					GraphBuilder,
					TileCount, TEXT("Debug.ProbeHierarchy.BuildFrustum.BuildProbeArray"));

				TShaderMapRef<FBuildProbeArrayCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("BuildProbeArray(Level=%i) %dx%d", HierarchyLevelId, TileCount.X, TileCount.Y),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(TileCount, 8));
			}

			ProbeHierachyParameters.ProbeArray = GraphBuilder.CreateSRV(ProbeArray);
		}

		EmitProbeParameters.ProbeListsPerEmitTile = ProbeListsPerEmitTile;

		// Dilate the resolve tiles from the emit tiles
		for (int32 HierarchyLevelId = 0; HierarchyLevelId < kProbeMaxHierarchyDepth; HierarchyLevelId++)
		{
			if (HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth)
			{
				ProbeListsPerResolveTile[HierarchyLevelId] = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), ResolveTileStorageExtent.X * ResolveTileStorageExtent.Y * (kMaxProbePerResolveTile + 1)),
					TEXT("ProbeHierarchy.BuildFrustum.ProbeListsPerResolveTile"));
			}
			else
			{
				ProbeListsPerResolveTile[HierarchyLevelId] = nullptr;
				continue;
			}

			int32 EmitTileSize = ComputeEmitTileSize(HierarchyLevelId);
			int32 TileSize = ComputeResolveTileSize(HierarchyLevelId);

			FIntPoint EmitTileCount = ComputeTileCount(View.ViewRect.Size(), EmitTileSize);
			FIntPoint TileCount = ComputeTileCount(View.ViewRect.Size(), TileSize);

			FDilateProbeResolveTilesCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FDilateProbeResolveTilesCS::FParameters>();
			PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->EmitTileCount = EmitTileCount;
			PassParameters->TileCount = TileCount;
			PassParameters->TileOffset = ComputeTileClassificationOffset(
				/* ParentTileSize = */ EmitTileSize, /* ChildTileSize = */ TileSize);
			PassParameters->HierarchyId = HierarchyLevelId;

			PassParameters->ProbeListPerEmitTile = ProbeListsPerEmitTile[HierarchyLevelId];
			PassParameters->TiledDepthBounds = TiledDepthBounds;
			PassParameters->ClosestHZB = View.ClosestHZB;
			PassParameters->FurthestHZB = View.HZB;
			PassParameters->ProbeArray = ProbeHierachyParameters.ProbeArray;

			PassParameters->ProbePerTilesOutput = GraphBuilder.CreateUAV(ProbeListsPerResolveTile[HierarchyLevelId]);
			PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
				GraphBuilder, TileCount, TEXT("Debug.ProbeHierarchy.BuildFrustum.DilateProbeTiles"));

			TShaderMapRef<FDilateProbeResolveTilesCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DilateProbeTiles(Level=%i) %dx%d", HierarchyLevelId, TileCount.X, TileCount.Y),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(TileCount, 8));
		}
	}

	// Full probe occlusion tracing.
	FRDGTextureRef ResolvedProbeIndexes = nullptr;

	FIndirectLightingProbeOcclusionParameters IndirectLightingProbeOcclusionParameters;
	{
		RDG_EVENT_SCOPE(GraphBuilder, "ProbeOcclusion(RayPerPixel=%d)", CommonParameters.RayCountPerPixel);

		// Stocastically selects the probes on per pixel basis, outputing probe index and tracing distance that should be use for probe occlusion.
		{
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					SceneBufferExtent,
					PF_R16_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);

				ResolvedProbeIndexes = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.Occlusion.ProbeIndexes"));
			}

			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					SceneBufferExtent,
					PF_R16F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);

				IndirectLightingProbeOcclusionParameters.ProbeOcclusionDistanceTexture = 
					GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.Occlusion.Distance"));
			}

			FResolveProbeIndexesCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FResolveProbeIndexesCS::FParameters>();
			PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
			PassParameters->HierarchyParameters = ProbeHierachyParameters;
			PassParameters->SceneTextures = CommonParameters.SceneTextures;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->GlobalEmitTileClassificationOffset = ProbeOcclusionParameters.GlobalEmitTileClassificationOffset;
			PassParameters->ProbePerResolveTiles = GraphBuilder.CreateSRV(ProbeListsPerResolveTile[0]);

			PassParameters->ResolvedIndexesOutput = GraphBuilder.CreateUAV(ResolvedProbeIndexes);
			PassParameters->ProbeOcclusionDistanceOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.ProbeOcclusionDistanceTexture);
			PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
				GraphBuilder, SceneBufferExtent, TEXT("Debug.ProbeHierarchy.ResolveProbeIndexes"));

			TShaderMapRef<FResolveProbeIndexesCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ResolveProbeIndexes %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				ComputeShader,
				PassParameters,
				FIntVector(ProbeOcclusionParameters.ResolveTileCount.X, ProbeOcclusionParameters.ResolveTileCount.Y, 1));
		}

		// Classify screen space tiles.
		// TODO: Try to merge with FResolveProbeIndexesCS
		{
			FIntPoint TileClassificationCount = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), kTracingClassificationTileSize);
			FIntPoint TileClassificationExtent = FIntPoint::DivideAndRoundUp(SceneBufferExtent, kTracingClassificationTileSize);

			// Classify tiles.
			FRDGTextureRef TileClassificationTexture = nullptr;
			FRDGTextureRef AtomicTileCountersTexture = nullptr;
			FIntPoint AtomicTileCount;
			FIntPoint AtomicTileExtent;
			{
				// Allocate compressed data.
				{
					FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
						SceneBufferExtent,
						PF_R16F,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV);

					IndirectLightingProbeOcclusionParameters.CompressedDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.CompressedDepth"));
				
					Desc.Format = PF_R8;
					IndirectLightingProbeOcclusionParameters.CompressedRoughnessTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.CompressedRoughness"));

					Desc.Format = PF_R8_UINT;
					IndirectLightingProbeOcclusionParameters.CompressedShadingModelTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.CompressedShadingModelID"));
				}

				// Allocate tile classification
				{
					FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
						TileClassificationExtent,
						PF_R8_UINT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV);

					TileClassificationTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.Occlusion.Classification"));
				}

				// Allocate atomic tile counters.
				{
					AtomicTileCount = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), kTracingClassificationTileSize * 8);
					AtomicTileExtent = FIntPoint::DivideAndRoundUp(SceneBufferExtent, kTracingClassificationTileSize * 8);

					FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
						FIntPoint(AtomicTileExtent.X, AtomicTileExtent.Y * int32(EProbeOcclusionClassification::MAX)),
						PF_R32_UINT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV);

					AtomicTileCountersTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.Occlusion.AtomicTileCounters"));
				}

				FProbeOcclusionTileClassificationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FProbeOcclusionTileClassificationCS::FParameters>();
				PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
				PassParameters->SceneTextures = CommonParameters.SceneTextures;
				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->AtomicTileExtent = AtomicTileExtent;
				PassParameters->AdditionalSpecularRayThreshold = CVarAdditionalSpecularRayThreshold.GetValueOnRenderThread();

				PassParameters->TileClassificationOutput = GraphBuilder.CreateUAV(TileClassificationTexture);
				PassParameters->AtomicTileCounterOutput = GraphBuilder.CreateUAV(AtomicTileCountersTexture);
				PassParameters->CompressedDepthBufferOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.CompressedDepthTexture);
				PassParameters->CompressedRoughnessOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.CompressedRoughnessTexture);
				PassParameters->CompressedShadingModelOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.CompressedShadingModelTexture);
				PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
					GraphBuilder, AtomicTileCountersTexture->Desc.Extent, TEXT("Debug.ProbeHierarchy.Occlusion.TileClassification"));

				static const uint32 ClearColor[4] = { 0, 0, 0, 0 };
				AddClearUAVPass(GraphBuilder, PassParameters->AtomicTileCounterOutput, ClearColor);

				TShaderMapRef<FProbeOcclusionTileClassificationCS> ComputeShader(View.ShaderMap);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TileClassification %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
					ComputeShader,
					PassParameters,
					FIntVector(TileClassificationCount.X, TileClassificationCount.Y, 1));
			}

			FRDGTextureRef AtomicTileOffsetsTexture = nullptr;
			FRDGBufferRef GlobalClassificationCountersBuffer = nullptr;
			{
				{
					FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
						FIntPoint(AtomicTileExtent.X, AtomicTileExtent.Y * int32(EProbeOcclusionClassification::MAX)),
						PF_R32_UINT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV);

					AtomicTileOffsetsTexture = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.Occlusion.AtomicTileOffsets"));
				}

				{
					FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(int32), int32(EProbeOcclusionClassification::MAX));

					GlobalClassificationCountersBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("ProbeHierarchy.Occlusion.GlobalClassificationCounters"));
				}

				FProbeOcclusionAssignTileOffsetsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FProbeOcclusionAssignTileOffsetsCS::FParameters>();
				PassParameters->AtomicTileCount = AtomicTileCount;
				PassParameters->AtomicTileExtent = AtomicTileExtent;

				PassParameters->AtomicTileCounters = AtomicTileCountersTexture;
				PassParameters->AtomicTileOffsetsOutput = GraphBuilder.CreateUAV(AtomicTileOffsetsTexture);
				PassParameters->GlobalCounterOutput = GraphBuilder.CreateUAV(GlobalClassificationCountersBuffer, PF_R32_UINT);
				PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
					GraphBuilder, AtomicTileOffsetsTexture->Desc.Extent, TEXT("Debug.ProbeHierarchy.Occlusion.AssignTileOffsets"));

				AddClearUAVPass(GraphBuilder, PassParameters->GlobalCounterOutput, 0);

				TShaderMapRef<FProbeOcclusionAssignTileOffsetsCS> ComputeShader(View.ShaderMap);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("AssignOffsets %dx%d", AtomicTileCount.X, AtomicTileCount.Y),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(AtomicTileCount, 8));
			}

			FRDGBufferRef TileListBuffer = nullptr;
			int32 TileListMaxLength;
			{
				{
					TileListMaxLength = TileClassificationExtent.X * TileClassificationExtent.Y;

					FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), TileListMaxLength * int32(EProbeOcclusionClassification::MAX));
					TileListBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("ProbeHierarchy.Occlusion.TileListBuffer"));
				}

				FProbeOcclusionBuildTileListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FProbeOcclusionBuildTileListsCS::FParameters>();
				PassParameters->TileCount = TileClassificationCount;
				PassParameters->AtomicTileExtent = AtomicTileExtent;
				PassParameters->TileListMaxLength = TileListMaxLength;

				PassParameters->TileClassificationTexture = TileClassificationTexture;
				PassParameters->AtomicTileOffsetTexture = AtomicTileOffsetsTexture;
				PassParameters->TileListOutput = GraphBuilder.CreateUAV(TileListBuffer);
				PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
					GraphBuilder, TileClassificationCount, TEXT("Debug.ProbeHierarchy.Occlusion.BuildTileLists"));

				TShaderMapRef<FProbeOcclusionBuildTileListsCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("BuildTileLists %dx%x", TileClassificationCount.X, TileClassificationCount.Y),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(TileClassificationCount, 8));
			}

			int32 MaxTileClassificationCount = TileClassificationCount.X * TileClassificationCount.Y;
			IndirectLightingProbeOcclusionParameters.MaxTilePerDispatch = GRHIMaxDispatchThreadGroupsPerDimension.Y;
			IndirectLightingProbeOcclusionParameters.DispatchCount = FMath::DivideAndRoundUp(MaxTileClassificationCount, IndirectLightingProbeOcclusionParameters.MaxTilePerDispatch);
			IndirectLightingProbeOcclusionParameters.TileListBuffer = GraphBuilder.CreateSRV(TileListBuffer);
			IndirectLightingProbeOcclusionParameters.GlobalClassificationCountersBuffer = GraphBuilder.CreateSRV(GlobalClassificationCountersBuffer, PF_R32_UINT);
			IndirectLightingProbeOcclusionParameters.TileListMaxLength = TileListMaxLength;
		}

		bool bProbeOcclusion = CVarProbeOcclusion.GetValueOnRenderThread() != 0;

		// Allocate input for screen space denoiser.
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				SceneBufferExtent,
				PF_FloatR11G11B10,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			IndirectLightingProbeOcclusionParameters.DiffuseLighting = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.ResolveDiffuseIndirect"));
			IndirectLightingProbeOcclusionParameters.SpecularLighting = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.ResolveSpecularIndirect"));

			if (CommonParameters.RayCountPerPixel <= 4)
			{
				Desc.Format = PF_R8_UINT;
			}
			else if (CommonParameters.RayCountPerPixel <= 8)
			{
				Desc.Format = PF_R16_UINT;
			}
			else if (CommonParameters.RayCountPerPixel <= 16)
			{
				Desc.Format = PF_R32_UINT;
			}
			else
			{
				unimplemented();
			}
			IndirectLightingProbeOcclusionParameters.DiffuseSampleMask = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.ResolveDiffuseSampleMask"));
			IndirectLightingProbeOcclusionParameters.SpecularSampleMask = GraphBuilder.CreateTexture(Desc, TEXT("ProbeHierarchy.ResolveSpecularSampleMask"));
		}

		IndirectLightingProbeOcclusionParameters.EnableBentNormal = CVarEnableBentNormal.GetValueOnRenderThread();
		IndirectLightingProbeOcclusionParameters.AdditionalSpecularRayThreshold = CVarAdditionalSpecularRayThreshold.GetValueOnRenderThread();

		// Performs the screen space tracing first, given it can give the highest frequency detail.
		{
			bool bScreenSpaceProbeOcclusion = View.PrevViewInfo.ScreenSpaceRayTracingInput.IsValid() && CVarSSGIProbeOcclusion.GetValueOnRenderThread();

			if (bProbeOcclusion && bScreenSpaceProbeOcclusion)
			{
				ScreenSpaceRayTracing::TraceIndirectProbeOcclusion(
					GraphBuilder,
					CommonParameters,
					PrevSceneColorMip,
					View,
					IndirectLightingProbeOcclusionParameters);
			}
			else
			{
				static const uint32 MaskClearColor[4] = { 0, 0, 0, 0 };

				FIndirectLightingProbeOcclusionOutputParameters ProbeOcclusionOutputParameters = CreateProbeOcclusionOutputParameters(
					GraphBuilder, IndirectLightingProbeOcclusionParameters, ERDGUnorderedAccessViewFlags::None);

				AddClearUAVPass(GraphBuilder, ProbeOcclusionOutputParameters.DiffuseLightingOutput, FLinearColor::Transparent);
				AddClearUAVPass(GraphBuilder, ProbeOcclusionOutputParameters.DiffuseSampleMaskOutput, MaskClearColor);

				AddClearUAVPass(GraphBuilder, ProbeOcclusionOutputParameters.SpecularLightingOutput, FLinearColor::Transparent);
				AddClearUAVPass(GraphBuilder, ProbeOcclusionOutputParameters.SpecularSampleMaskOutput, MaskClearColor);
			}
		}

		// Fallback to voxel tracing for when screen space tracing gets uncertain in some areas
		{
			if (bProbeOcclusion &&
				CVarVoxelDiffuseProbeOcclusion.GetValueOnRenderThread() &&
				ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen)
			{
				RenderLumenProbeOcclusion(
					GraphBuilder,
					View,
					FrameTemporaries,
					CommonParameters,
					IndirectLightingProbeOcclusionParameters);
			}
		}
	}

	// Compute the probe direction masks and select parent probes
	FRDGBufferRef ProbeParentList;
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Finish probe hierarchy");

		// Compute the probe direction masks based on probe occlusion masks
		{
			FMaskProbesDirectionsCS::FParameters* PassParameters =
				GraphBuilder.AllocParameters<FMaskProbesDirectionsCS::FParameters>();
			PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
			PassParameters->HierarchyParameters = ProbeHierachyParameters;
			PassParameters->SceneTextures = CommonParameters.SceneTextures;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->SamplePerPixel = CommonParameters.RayCountPerPixel;
			PassParameters->AdditionalSpecularRayThreshold = CVarAdditionalSpecularRayThreshold.GetValueOnRenderThread();

			PassParameters->ResolvedProbeIndexes = ResolvedProbeIndexes;
			PassParameters->DiffuseSampleMaskTexture = IndirectLightingProbeOcclusionParameters.DiffuseSampleMask;
			PassParameters->SpecularSampleMaskTexture = IndirectLightingProbeOcclusionParameters.SpecularSampleMask;

			PassParameters->ProbeArrayInout = GraphBuilder.CreateUAV(ProbeHierachyParameters.ProbeArray->Desc.Buffer);
			PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
				GraphBuilder, SceneBufferExtent, TEXT("Debug.ProbeHierarchy.MaskProbesDirections"));

			TShaderMapRef<FMaskProbesDirectionsCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MaskProbesDirections %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), 8));
		}

		//return FSSDSignalTextures();

		// Selects parent probes.
		{
			FRDGBufferRef DispatchParameters = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(LumenProbeHierarchy::kProbeMaxHierarchyDepth),
				TEXT("ProbeHierarchy.SelectParentProbeDispatch"));

			ProbeParentList = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(int32) * 2 * kMaxParentProbeCount, ProbeHierachyParameters.MaxProbeCount),
				TEXT("ProbeHierarchy.ProbeParentList"));

			{
				FSetupSelectParentProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupSelectParentProbeCS::FParameters>();
				PassParameters->HierarchyParameters = ProbeHierachyParameters;
				PassParameters->DispatchParametersOutput = GraphBuilder.CreateUAV(DispatchParameters);

				TShaderMapRef<FSetupSelectParentProbeCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SetupSelectParentProbe"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			for (int32 HierarchyLevelId = 0; HierarchyLevelId < (ProbeHierachyParameters.HierarchyDepth - 1); HierarchyLevelId++)
			{
				int32 ParentTileSize = ComputeResolveTileSize(HierarchyLevelId + 1);

				FSelectParentProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectParentProbeCS::FParameters>();
				PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
				PassParameters->HierarchyParameters = ProbeHierachyParameters;
				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->ParentTilePixelOffset = ComputeTileClassificationOffset(/* ParentTileSize = */ ParentTileSize, /* ChildTileSize = */ 1);
				PassParameters->ParentResolveTileBoundary = ComputeTileCount(View.ViewRect.Size(), ParentTileSize) - FIntPoint(1, 1);
				PassParameters->ParentHierarchyId = HierarchyLevelId + 1;
				PassParameters->LevelId = HierarchyLevelId;
				PassParameters->DispatchParameters = DispatchParameters;
				PassParameters->ProbePerResolveTiles = GraphBuilder.CreateSRV(ProbeListsPerResolveTile[HierarchyLevelId + 1]);
				PassParameters->ProbeArrayInout = GraphBuilder.CreateUAV(ProbeHierachyParameters.ProbeArray->Desc.Buffer);
				PassParameters->ProbeParentListOutput = GraphBuilder.CreateUAV(ProbeParentList);

				TShaderMapRef<FSelectParentProbeCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SelectParentProbe(Level=%i)", HierarchyLevelId),
					ComputeShader,
					PassParameters,
					DispatchParameters,
					/* IndirectArgOffset = */ sizeof(FRHIDispatchIndirectParameters) * HierarchyLevelId);
			}

			if (ProbeHierachyParameters.HierarchyDepth == 1)
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ProbeParentList), 0);
			}
		}
	}

	// Allocate indirect lighting atlas.
	LumenProbeHierarchy::FIndirectLightingAtlasParameters IndirectLightingAtlasParameters;
	{
		int32 TotalEmitTileCount = CommonProbeDenoiserParameters.EmitTileStorageExtent.X * CommonProbeDenoiserParameters.EmitTileStorageExtent.Y;
		int32 TotalEmitProbeCount = TotalEmitTileCount * kProbeMaxEmitPerTile;

		ProbeHierachyParameters.ProbeAtlasGridSize.X = FMath::Max(kMinAtlasGridSize, int32(FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(static_cast<float>(TotalEmitProbeCount))))));
		ProbeHierachyParameters.ProbeAtlasGridSize.Y = kMinAtlasGridSize * FMath::Max(1, FMath::DivideAndRoundUp(FMath::DivideAndRoundUp(TotalEmitProbeCount, ProbeHierachyParameters.ProbeAtlasGridSize.X), kMinAtlasGridSize));

		ProbeHierachyParameters.ProbeIndexAbscissMask = (ProbeHierachyParameters.ProbeAtlasGridSize.X / kMinAtlasGridSize) - 1;
		ProbeHierachyParameters.ProbeIndexOrdinateShift = FMath::Log2(static_cast<float>(ProbeHierachyParameters.ProbeAtlasGridSize.X / kMinAtlasGridSize));

		FRDGTextureDesc ProbeAtlasDesc = FRDGTextureDesc::Create2D(
			FIntPoint(
				ProbeHierachyParameters.ProbeAtlasGridSize.X * kProbeResolution * 2,
				ProbeHierachyParameters.ProbeAtlasGridSize.Y * kProbeResolution * 3),
			PF_FloatR11G11B10,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		IndirectLightingAtlasParameters.ProbeAtlasColor = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("ProbeHierarchy.ProbeAtlasColor"));

		int32 MaxSuperSample = 1;

		for (int32 HierarchyLevelId = 0; HierarchyLevelId < ProbeHierachyParameters.HierarchyDepth; HierarchyLevelId++)
		{
			FHierarchyLevelParameters LevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, HierarchyLevelId);

			MaxSuperSample = FMath::Max(MaxSuperSample, LevelParameters.LevelSuperSampling);
		}

		const int32 kBitsPerRay = 2;

		int32 MaxRayPerPixel = MaxSuperSample * MaxSuperSample;
		int32 RequiredMaskingBitsPerPixel = MaxRayPerPixel * kBitsPerRay;

		EPixelFormat SampleBitMask = PF_Unknown;
		if (RequiredMaskingBitsPerPixel <= 8)
		{
			SampleBitMask = PF_R8_UINT;
		}
		else if (RequiredMaskingBitsPerPixel <= 32)
		{
			SampleBitMask = PF_R32_UINT;
		}
		else
		{
			unimplemented();
		}
		ProbeAtlasDesc.Format = SampleBitMask;
		IndirectLightingAtlasParameters.ProbeAtlasSampleMask = GraphBuilder.CreateTexture(ProbeAtlasDesc, TEXT("ProbeHierarchy.ProbeAtlasSampleMask"));
	}

	if (View.PrevViewInfo.ScreenSpaceRayTracingInput.IsValid() && CVarScreenSpaceProbeTracing.GetValueOnRenderThread())
	{
		ScreenSpaceRayTracing::TraceProbe(
			GraphBuilder, View,
			CommonParameters.SceneTextures,
			PrevSceneColorMip,
			ProbeHierachyParameters,
			/* inout */ IndirectLightingAtlasParameters);
	}
	else
	{
		static const uint32 ClearColor[4] = { 0, 0, 0, 0 };

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasColor), FLinearColor::Transparent);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasSampleMask), ClearColor);
	}

	// Trace from Probes
	if (ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen)
	{
		RenderLumenProbe(
			GraphBuilder, View, FrameTemporaries,
			ProbeHierachyParameters,
			IndirectLightingAtlasParameters,
			EmitProbeParameters);
	}

	// Compose the parent probes into the leaves.
	FRDGTextureRef FinalProbeAtlas = ComposeFinalProbeAtlas(
		GraphBuilder,
		View.ShaderMap,
		ProbeHierachyParameters,
		IndirectLightingAtlasParameters,
		ProbeParentList);

	RDG_EVENT_SCOPE(GraphBuilder, "ResolveFullScreenIndirectLighting(RayPerPixel=%d)", CommonParameters.RayCountPerPixel);

	// Resolve indirect lighting from probe hierarchy.
	// This pass is mandatory in case of specular from diffuse as it renormalizes accumulated specular samples.
	{
		FTraceIndirectLightingProbeHierarchyCS::FParameters* PassParameters =
			GraphBuilder.AllocParameters<FTraceIndirectLightingProbeHierarchyCS::FParameters>();
		PassParameters->CommonProbeDenoiserParameters = CommonProbeDenoiserParameters;
		PassParameters->HierarchyParameters = ProbeHierachyParameters;
		PassParameters->LevelParameters = LumenProbeHierarchy::GetLevelParameters(ProbeHierachyParameters, /* HierarchyLevelId = */ 0);
		PassParameters->SceneTextures = CommonParameters.SceneTextures;
		PassParameters->CompressedDepthTexture = IndirectLightingProbeOcclusionParameters.CompressedDepthTexture;
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

		PassParameters->FinalProbeAtlasPixelSize.X = 1.0f / float(FinalProbeAtlas->Desc.Extent.X);
		PassParameters->FinalProbeAtlasPixelSize.Y = 1.0f / float(FinalProbeAtlas->Desc.Extent.Y);
		PassParameters->SamplePerPixel = CommonParameters.RayCountPerPixel;
		PassParameters->fSamplePerPixel = CommonParameters.RayCountPerPixel;
		PassParameters->fInvSamplePerPixel = 1.0f / float(CommonParameters.RayCountPerPixel);
		PassParameters->DiffuseIndirectMipLevel = CVarDiffuseIndirectMipLevel.GetValueOnRenderThread();
		PassParameters->AdditionalSpecularRayThreshold = CVarAdditionalSpecularRayThreshold.GetValueOnRenderThread();

		PassParameters->FinalProbeAtlas = FinalProbeAtlas;
		PassParameters->ResolvedProbeIndexes = ResolvedProbeIndexes;
		PassParameters->DiffuseSampleMaskTexture = IndirectLightingProbeOcclusionParameters.DiffuseSampleMask;
		PassParameters->SpecularSampleMaskTexture = IndirectLightingProbeOcclusionParameters.SpecularSampleMask;

		PassParameters->DiffuseLightingOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.DiffuseLighting);
		PassParameters->SpecularLightingOutput = GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.SpecularLighting);
		PassParameters->DebugOutput = CreateProbeHierarchyDebugOutputUAV(
			GraphBuilder, SceneBufferExtent, TEXT("Debug.ProbeHierarchy.TraceProbeHierarchy"));

		TShaderMapRef<FTraceIndirectLightingProbeHierarchyCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceProbeHierarchy %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			ComputeShader,
			PassParameters,
			FIntVector(ProbeOcclusionParameters.ResolveTileCount.X, ProbeOcclusionParameters.ResolveTileCount.Y, 1));
	}

	if (!View.Family->EngineShowFlags.LumenReflections)
	{
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(IndirectLightingProbeOcclusionParameters.SpecularLighting), FLinearColor::Black);
	}

	FSSDSignalTextures ScreenSpaceDenoiserInputs;
	ScreenSpaceDenoiserInputs.Textures[0] = IndirectLightingProbeOcclusionParameters.DiffuseLighting;
	ScreenSpaceDenoiserInputs.Textures[1] = IndirectLightingProbeOcclusionParameters.SpecularLighting;

	// Add light screen space denoising to clean full res stocasticity
	FSSDSignalTextures DenoiserOutputs = IScreenSpaceDenoiser::DenoiseIndirectProbeHierarchy(
		GraphBuilder,
		View, PreviousViewInfos,
		CommonParameters.SceneTextures,
		ScreenSpaceDenoiserInputs,
		IndirectLightingProbeOcclusionParameters.CompressedDepthTexture,
		IndirectLightingProbeOcclusionParameters.CompressedShadingModelTexture);

	return DenoiserOutputs;
}
