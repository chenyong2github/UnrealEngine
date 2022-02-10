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

class FLumenCardUpdateContext;

class FHemisphereDirectionSampleGenerator
{
public:
	TArray<FVector4f> SampleDirections;
	float ConeHalfAngle = 0;
	int32 Seed = 0;
	int32 PowerOfTwoDivisor = 1;
	bool bFullSphere = false;
	bool bCosineDistribution = false;

	void GenerateSamples(int32 TargetNumSamples, int32 InPowerOfTwoDivisor, int32 InSeed, bool bInFullSphere = false, bool bInCosineDistribution = false);

	void GetSampleDirections(const FVector4f*& OutDirections, int32& OutNumDirections) const
	{
		OutDirections = SampleDirections.GetData();
		OutNumDirections = SampleDirections.Num();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenVoxelTracingParameters, )
	SHADER_PARAMETER(uint32, NumClipmapLevels)
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapWorldToUVScale, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapWorldToUVBias, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapWorldCenter, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapWorldExtent, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapWorldSamplingExtent, [MaxVoxelClipmapLevels])
	SHADER_PARAMETER_ARRAY(FVector4f, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardTracingParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)

	// GPU Scene
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardPageLastUsedBuffer)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardPageHighResLastUsedBuffer)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWSurfaceCacheFeedbackBufferAllocator)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, RWSurfaceCacheFeedbackBuffer)
	SHADER_PARAMETER(uint32, SurfaceCacheFeedbackBufferSize)
	SHADER_PARAMETER(uint32, SurfaceCacheFeedbackBufferTileWrapMask)
	SHADER_PARAMETER(FIntPoint, SurfaceCacheFeedbackBufferTileJitter)
	SHADER_PARAMETER(float, SurfaceCacheFeedbackResLevelBias)
	SHADER_PARAMETER(uint32, SurfaceCacheUpdateFrameIndex)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DirectLightingAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectLightingAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FinalLightingAtlas)
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

	FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View, FLumenSceneFrameTemporaries& FrameTemporaries, bool bSurfaceCacheFeedback = true);

	FRDGTextureRef AlbedoAtlas;
	FRDGTextureRef OpacityAtlas;
	FRDGTextureRef NormalAtlas;
	FRDGTextureRef EmissiveAtlas;
	FRDGTextureRef DepthAtlas;

	FRDGTextureRef DirectLightingAtlas;
	FRDGTextureRef IndirectLightingAtlas;
	FRDGTextureRef RadiosityNumFramesAccumulatedAtlas;
	FRDGTextureRef FinalLightingAtlas;
	FRDGTextureRef VoxelLighting;

	// Feedback
	FRDGBufferUAVRef CardPageLastUsedBufferUAV;
	FRDGBufferUAVRef CardPageHighResLastUsedBufferUAV;
	FRDGBufferUAVRef SurfaceCacheFeedbackBufferAllocatorUAV;
	FRDGBufferUAVRef SurfaceCacheFeedbackBufferUAV;
	uint32 SurfaceCacheFeedbackBufferSize;
	uint32 SurfaceCacheFeedbackBufferTileWrapMask;
	FIntPoint SurfaceCacheFeedbackBufferTileJitter;

	// Voxel clipmaps
	FIntVector VoxelGridResolution;
	int32 NumClipmapLevels;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVScale;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVBias;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldCenter;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldExtent;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldSamplingExtent;
	TStaticArray<FVector4f, MaxVoxelClipmapLevels> ClipmapVoxelSizeAndRadius;

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
	// Heightfield data
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumCulledHeightfieldObjects)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledHeightfieldObjectIndexBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumGridCulledHeightfieldObjects)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledHeightfieldObjectStartOffsetArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledHeightfieldObjectIndicesArray)
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
	SHADER_PARAMETER(int32, HeightfieldMaxTracingSteps)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenDiffuseTracingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(HybridIndirectLighting::FCommonParameters, CommonDiffuseParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
	SHADER_PARAMETER(float, SampleWeight)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledNormal)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenHZBScreenTraceParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevSceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistorySceneDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ClosestHZBTexture)
	SHADER_PARAMETER(FVector4f, HZBUvFactorAndInvFactor)
	SHADER_PARAMETER(FVector2f, PrevSceneColorBilinearUVMin)
	SHADER_PARAMETER(FVector2f, PrevSceneColorBilinearUVMax)
	SHADER_PARAMETER(FVector4f, PrevScreenPositionScaleBias)
	SHADER_PARAMETER(FVector4f, PrevScreenPositionScaleBiasForDepth)
	SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
	SHADER_PARAMETER(FVector2f, HZBBaseTexelSize)
	SHADER_PARAMETER(FVector4f, HZBUVToScreenUVScaleBias)
END_SHADER_PARAMETER_STRUCT()

extern void CullHeightfieldObjectsForView(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	FRDGBufferRef& NumCulledObjects,
	FRDGBufferRef& CulledObjectIndexBuffer);

extern void CullMeshObjectsToViewGrid(
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

extern void SetupLumenDiffuseTracingParameters(const FViewInfo& View, FLumenIndirectTracingParameters& OutParameters);
extern void SetupLumenDiffuseTracingParametersForProbe(const FViewInfo& View, FLumenIndirectTracingParameters& OutParameters, float DiffuseConeAngle);

extern FLumenHZBScreenTraceParameters SetupHZBScreenTraceParameters(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FSceneTextures& SceneTextures);

extern FVector GetLumenSceneViewOrigin(const FViewInfo& View, int32 ClipmapIndex);
extern int32 GetNumLumenVoxelClipmaps(float LumenSceneViewDistance);
extern void UpdateDistantScene(FScene* Scene, FViewInfo& View);
extern float ComputeMaxCardUpdateDistanceFromCamera(float LumenSceneViewDistance);

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

