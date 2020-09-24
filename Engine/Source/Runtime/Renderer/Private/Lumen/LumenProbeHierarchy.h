// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScenePrivate.h"
#include "SceneTextureParameters.h"


namespace LumenProbeHierarchy
{

// Length of a border of the cubemap of the Probe in pixels
constexpr int32 kProbeResolution = 4;

// Pixel radius of the leaves probes in the hierarchy
constexpr int32 kProbeHierarchyMinPixelRadius = 64;

// Size of the tile when emitting Probe according to depth buffer
constexpr int32 kProbeEmitTileSize = 16;

// Length of a border of the cubemap of the Probe in pixels
constexpr int32 kProbeMaxEmitPerTile = 8;

// Maximum number of Probe per tiles.
constexpr int32 kMaxProbePerResolveTile = 63;

/** Maximum resolution of rays ray tracing pixel 8192x8192.
 * Chosen to be as small as possProbee to avoid warp divergence in on the full res application to scene color. */
constexpr int32 kProbeTileClassificationSize = 8;

// Minimum and maximum number of hierarchy
constexpr int32 kProbeMaxHierarchyDepth = 8;

// Number of exponent on the size of the probed when going in the hierarchy
constexpr int32 kProbeHierarchyEXPONENT = 2;

// Minimum number of atlas on each coordinate of the atlas.
constexpr int32 kMinAtlasGridSize = 16;

// Classification tile size for tracing probe occlusion and probe hierarchy.
constexpr int32 kTracingClassificationTileSize = 8;

// Maximum number of parents a child probe can have.
constexpr int32 kMaxParentProbeCount = 4;


/* Common parameters for probe hierarchy rendering. */
BEGIN_SHADER_PARAMETER_STRUCT(FHierarchyParameters, )
	// Allocated grid size of lowest resolution probes
	SHADER_PARAMETER(FIntPoint, ProbeAtlasGridSize)

	// Bits operator to transform a tracing PixelRayIndex into ray storage coordinates in probe atlas.
	SHADER_PARAMETER(int32, ProbeIndexAbscissMask)
	SHADER_PARAMETER(int32, ProbeIndexOrdinateShift)

	// Number of depth in the hierarchy 
	SHADER_PARAMETER(int32, HierarchyDepth)

	// Maximum number of probe that can be allocated in the atlas.
	SHADER_PARAMETER(int32, MaxProbeCount)

	SHADER_PARAMETER(float, CounterParrallaxError)

	SHADER_PARAMETER_ARRAY(int32, LevelResolutionArray, [kProbeMaxHierarchyDepth])
	SHADER_PARAMETER_ARRAY(int32, LevelSuperSamplingArray, [kProbeMaxHierarchyDepth])

	// Infos about the different hierarchy of probes
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeHierarchyInfoBuffer)

	// Array of all Probes.
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeArray)
END_SHADER_PARAMETER_STRUCT()


/* Common parameters for indirect lighting probe hierarchy rendering. */
BEGIN_SHADER_PARAMETER_STRUCT(FIndirectLightingAtlasParameters, )
	// Atlas of R11G11B10 of the Probes
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ProbeAtlasColor)

	// Atlas alpha channels of the probes.
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ProbeAtlasAlpha)

	// Atlas of bit mask of ray directions.
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ProbeAtlasSampleMask)
END_SHADER_PARAMETER_STRUCT()


/* Common parameters for probe occlusion passes. */
BEGIN_SHADER_PARAMETER_STRUCT(FProbeOcclusionParameters, )
	// Number of tiles for the full res view.
	SHADER_PARAMETER(FIntPoint, ResolveTileCount)

	// Offset to apply to pixel coordinate such that: ThreadId = PixelPosition + ResolveTileOffset;
	SHADER_PARAMETER(FIntPoint, GlobalEmitTileClassificationOffset)
END_SHADER_PARAMETER_STRUCT()


/* Common parameters for probe occlusion passes. */
BEGIN_SHADER_PARAMETER_STRUCT(FHierarchyLevelParameters, )
	// Id of the level in the hierarchy.
	SHADER_PARAMETER(int32, LevelId)

	// Resolution of the probe in texels.
	SHADER_PARAMETER(int32, LevelResolution)

	// Number of ray shot per texel of the probe.
	SHADER_PARAMETER(int32, LevelSuperSampling)
END_SHADER_PARAMETER_STRUCT()


/* Common parameters for indirect lighting probe hierarchy rendering. */
BEGIN_SHADER_PARAMETER_STRUCT(FIndirectLightingProbeOcclusionParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CompressedDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CompressedRoughnessTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, CompressedShadingModelTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ProbeOcclusionDistanceTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DiffuseLighting)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SpecularLighting)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DiffuseSampleMask)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SpecularSampleMask)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileListBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GlobalClassificationCountersBuffer)
	SHADER_PARAMETER(int32, TileListMaxLength)
	SHADER_PARAMETER(int32, MaxTilePerDispatch)
	SHADER_PARAMETER(int32, DispatchCount)
	SHADER_PARAMETER(int32, EnableBentNormal)
	SHADER_PARAMETER(float, AdditionalSpecularRayThreshold)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FIndirectLightingProbeOcclusionOutputParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DiffuseLightingOutput)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecularLightingOutput)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, DiffuseSampleMaskOutput)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SpecularSampleMaskOutput)
END_SHADER_PARAMETER_STRUCT()

FIndirectLightingProbeOcclusionOutputParameters CreateProbeOcclusionOutputParameters(
	FRDGBuilder& GraphBuilder,
	const FIndirectLightingProbeOcclusionParameters& ProbeOcclusionParameters,
	ERDGUnorderedAccessViewFlags ResourceViewFlags);

/* Emit probe parameters for culling light probes. */
class FEmitProbeParameters
{
public:
	uint32 MaxProbeCount;
	FIntPoint EmitTileStorageExtent;
	FIntPoint ProbeTileCount[kProbeMaxHierarchyDepth];
	TStaticArray<FRDGTextureRef, kProbeMaxHierarchyDepth> ProbeListsPerEmitTile;
};

enum class ELightingTerm
{
	Diffuse,
	Specular,
	MAX
};

enum class EProbeTracingPermutation
{
	MultipleProbePerWave,
	SingleProbePerWave,
	MAX
};

enum class EProbeOcclusionClassification
{
	Unlit,
	DefaultLitOnly,
	SimpleShading,
	SimpleShadingSpecular,
	SimpleShadingBentNormal,
	ComplexShadingBentNormal,
	MAX
};

const TCHAR* GetEventName(EProbeOcclusionClassification TileClassification);


class FProbeTracingPermutationDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_TRACING_PERMUTATION", EProbeTracingPermutation);


/** Returns information about a given hierarchy level. */
FHierarchyLevelParameters GetLevelParameters(const FHierarchyParameters& HierarchyParameters, int32 HierarchyLevelId);
EProbeTracingPermutation GetProbeTracingPermutation(const FHierarchyLevelParameters& LevelInfo);

/** Compute the ideal cone angle to trace probes hierarchy level. */
float ComputeHierarchyLevelConeAngle(const FHierarchyLevelParameters& LevelParameters);

/** Compose the probe hierarchy into final probe atlas ready for sampling. */
FRDGTextureRef ComposeFinalProbeAtlas(
	FRDGBuilder& GraphBuilder,
	const FHierarchyParameters& ProbeHierachyParameters,
	const FIndirectLightingAtlasParameters& IndirectLightingAtlasParameters,
	FRDGBufferRef ProbeParentList);

} // LumenProbeHierarchy
