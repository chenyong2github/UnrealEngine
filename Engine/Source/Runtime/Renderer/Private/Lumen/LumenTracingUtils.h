// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTracingUtils.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "DistanceFieldLightingShared.h"
#include "LumenSceneData.h"
#include "IndirectLightRendering.h"
#include "LumenProbeHierarchy.h"

class FHemisphereDirectionSampleGenerator
{
public:
	TArray<FVector4> SampleDirections;
	float ConeHalfAngle = 0;
	int32 Seed = 0;
	int32 PowerOfTwoDivisor = 1;
	bool bFullSphere = false;
	bool bCosineDistribution = false;

	void GenerateSamples(int32 TargetNumSamples, int32 InPowerOfTwoDivisor, int32 InSeed, bool bInFullSphere = false, bool bInCosineDistribution = false);

	void GetSampleDirections(const FVector4*& OutDirections, int32& OutNumDirections) const
	{
		OutDirections = SampleDirections.GetData();
		OutNumDirections = SampleDirections.Num();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenVoxelTracingParameters, )
	SHADER_PARAMETER(uint32, NumClipmapLevels)
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVScale, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVBias, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldCenter, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldExtent, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSamplingExtent, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardTracingParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)

	// GPU Scene
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWSurfaceCacheFeedbackBufferAllocator)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, RWSurfaceCacheFeedbackBuffer)
	SHADER_PARAMETER(uint32, SurfaceCacheFeedbackBufferSize)
	SHADER_PARAMETER(uint32, SurfaceCacheFeedbackBufferTileWrapMask)
	SHADER_PARAMETER(FIntPoint, SurfaceCacheFeedbackBufferTileJitter)
	SHADER_PARAMETER(float, SurfaceCacheFeedbackResLevelBias)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FinalLightingAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IrradianceAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectIrradianceAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VoxelLighting)
	SHADER_PARAMETER_STRUCT_REF(FLumenVoxelTracingParameters, LumenVoxelTracingParameters)
	SHADER_PARAMETER(uint32, NumGlobalSDFClipmaps)
END_SHADER_PARAMETER_STRUCT()

class FLumenCardTracingInputs
{
public:

	FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View, bool bSurfaceCachaFeedback = true);

	FRDGTextureRef FinalLightingAtlas;
	FRDGTextureRef IrradianceAtlas;
	FRDGTextureRef IndirectIrradianceAtlas;
	FRDGTextureRef AlbedoAtlas;
	FRDGTextureRef OpacityAtlas;
	FRDGTextureRef NormalAtlas;
	FRDGTextureRef EmissiveAtlas;
	FRDGTextureRef DepthAtlas;
	FRDGTextureRef VoxelLighting;
	FRDGBufferUAVRef SurfaceCacheFeedbackBufferAllocatorUAV;
	FRDGBufferUAVRef SurfaceCacheFeedbackBufferUAV;
	uint32 SurfaceCacheFeedbackBufferSize;
	uint32 SurfaceCacheFeedbackBufferTileWrapMask;
	FIntPoint SurfaceCacheFeedbackBufferTileJitter;
	FIntVector VoxelGridResolution;
	int32 NumClipmapLevels;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVScale;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVBias;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldCenter;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldExtent;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldSamplingExtent;
	TStaticArray<FVector4, MaxVoxelClipmapLevels> ClipmapVoxelSizeAndRadius;
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer;
};

BEGIN_SHADER_PARAMETER_STRUCT(FOctahedralSolidAngleParameters, )
	SHADER_PARAMETER(float, OctahedralSolidAngleTextureResolutionSq)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, OctahedralSolidAngleTexture)
END_SHADER_PARAMETER_STRUCT()

extern void GetLumenCardTracingParameters(const FViewInfo& View, const FLumenCardTracingInputs& TracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly = false);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenMeshSDFTracingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
	SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlas)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenMeshSDFGridParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, TracingParameters)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumGridCulledMeshSDFObjects)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectStartOffsetArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectIndicesArray)
	SHADER_PARAMETER(uint32, CardGridPixelSizeShift)
	SHADER_PARAMETER(FVector3f, CardGridZParams)
	SHADER_PARAMETER(FIntVector, CullGridSize)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenIndirectTracingParameters, )
	SHADER_PARAMETER(float, StepFactor)
	SHADER_PARAMETER(float, VoxelStepFactor)
	SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
	SHADER_PARAMETER(float, DiffuseConeHalfAngle)
	SHADER_PARAMETER(float, TanDiffuseConeHalfAngle)
	SHADER_PARAMETER(float, MinSampleRadius)
	SHADER_PARAMETER(float, MinTraceDistance)
	SHADER_PARAMETER(float, MaxTraceDistance)
	SHADER_PARAMETER(float, MaxMeshSDFTraceDistance)
	SHADER_PARAMETER(float, SurfaceBias)
	SHADER_PARAMETER(float, CardInterpolateInfluenceRadius)
	SHADER_PARAMETER(float, SpecularFromDiffuseRoughnessStart)
	SHADER_PARAMETER(float, SpecularFromDiffuseRoughnessEnd)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenDiffuseTracingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(HybridIndirectLighting::FCommonParameters, CommonDiffuseParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
	SHADER_PARAMETER(float, SampleWeight)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledNormal)
END_SHADER_PARAMETER_STRUCT()

void VisualizeHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRDGTextureRef SceneColor);

extern void CullMeshSDFObjectsToViewGrid(
	const FViewInfo& View,
	const FScene* Scene,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	int32 GridPixelsPerCellXY,
	int32 GridSizeZ,
	FVector ZParams,
	FRDGBuilder& GraphBuilder,
	FLumenMeshSDFGridParameters& OutGridParameters);

extern void CullMeshSDFObjectsToProbes(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	const LumenProbeHierarchy::FHierarchyParameters& ProbeHierarchyParameters,
	const LumenProbeHierarchy::FEmitProbeParameters& EmitProbeParameters,
	FLumenMeshSDFGridParameters& OutGridParameters);

extern void CullForCardTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	FLumenCardTracingInputs TracingInputs,
	const FLumenIndirectTracingParameters& IndirectTracingParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters);

extern void SetupLumenDiffuseTracingParameters(FLumenIndirectTracingParameters& OutParameters);
extern void SetupLumenDiffuseTracingParametersForProbe(FLumenIndirectTracingParameters& OutParameters, float DiffuseConeAngle);
extern FVector GetLumenSceneViewOrigin(const FViewInfo& View, int32 ClipmapIndex);
extern int32 GetNumLumenVoxelClipmaps();
extern void UpdateDistantScene(FScene* Scene, FViewInfo& View);
extern float ComputeMaxCardUpdateDistanceFromCamera();

extern FRDGTextureRef InitializeOctahedralSolidAngleTexture(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	int32 OctahedralSolidAngleTextureSize,
	TRefCountPtr<IPooledRenderTarget>& OctahedralSolidAngleTextureRT);

extern int32 GLumenIrradianceFieldGather;

namespace LumenIrradianceFieldGather
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs();
}

namespace LumenRadiosity
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs();
}