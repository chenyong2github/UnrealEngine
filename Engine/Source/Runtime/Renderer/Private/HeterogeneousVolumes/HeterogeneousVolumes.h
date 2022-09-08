// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelShaderUtils.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "RayTracingInstanceBufferUtil.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneManagement.h"

//
// External API
//

bool ShouldRenderHeterogeneousVolumes(const FScene* Scene);
bool ShouldRenderHeterogeneousVolumesForView(const FSceneView& View);
bool DoesPlatformSupportHeterogeneousVolumes(EShaderPlatform Platform);

//
// Internal API
//

namespace HeterogeneousVolumes
{
	// CVars
	FIntVector GetVolumeResolution();
	FIntVector GetTransmittanceVolumeResolution();

	float GetShadowStepFactor();
	float GetMaxTraceDistance();
	float GetMaxShadowTraceDistance();
	float GetStepSize();
	float GetMaxStepCount();

	int32 GetMipLevel();
	int32 GetDebugMode();
	uint32 GetSparseVoxelMipBias();

	bool ShouldJitter();
	bool ShouldRefineSparseVoxels();
	bool UseHardwareRayTracing();
	bool UseSparseVoxelPipeline();
	bool UseSparseVoxelPerTileCulling();
	bool UseTransmittanceVolume();

	// Convenience Utils
	int GetVoxelCount(FIntVector VolumeResolution);
	int GetVoxelCount(FRDGTextureDesc TextureDesc);
	FIntVector GetMipVolumeResolution(FIntVector VolumeResolution, uint32 MipLevel);
}

struct FVoxelDataPacked
{
	uint32 LinearIndex;
	uint32 MipLevel;
};

BEGIN_UNIFORM_BUFFER_STRUCT(FSparseVoxelUniformBufferParameters, )
	// Object data
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER(FMatrix44f, WorldToLocal)
	SHADER_PARAMETER(FVector3f, LocalBoundsOrigin)
	SHADER_PARAMETER(FVector3f, LocalBoundsExtent)

	// Volume data
	SHADER_PARAMETER(FIntVector, VolumeResolution)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ExtinctionTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, EmissionTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, AlbedoTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)

	// Resolution
	SHADER_PARAMETER(FIntVector, TransmittanceVolumeResolution)

	// Sparse voxel data
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumVoxelsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelDataPacked>, VoxelBuffer)
	SHADER_PARAMETER(int, MipLevel)

	// Traversal hints
	SHADER_PARAMETER(float, MaxTraceDistance)
	SHADER_PARAMETER(float, MaxShadowTraceDistance)
	SHADER_PARAMETER(float, StepSize)
	SHADER_PARAMETER(float, ShadowStepFactor)
END_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTransmittanceVolumeParameters, )
	SHADER_PARAMETER(FIntVector, TransmittanceVolumeResolution)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TransmittanceVolumeTexture)
END_SHADER_PARAMETER_STRUCT()

// Render specializations

void RenderWithLiveShading(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	// Shadow data
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Transmittance acceleration
	FRDGTextureRef TransmittanceVolumeTexture,
	// Output
	FRDGTextureRef& HeterogeneousVolumeRadiance
);

void RenderWithPreshading(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FSceneTextures& SceneTextures,
	FScene* Scene,
	const FSceneViewFamily& ViewFamily,
	FViewInfo& View,
	// Shadow data
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Transmittance acceleration
	FRDGTextureRef TransmittanceVolumeTexture,
	// Output
	FRDGTextureRef& HeterogeneousVolumeRadiance
);

// Preshading Pipeline
void ComputeHeterogeneousVolumeBakeMaterial(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Volume data
	FIntVector VolumeResolution,
	// Output
	FRDGTextureRef& HeterogeneousVolumeExtinctionTexture,
	FRDGTextureRef& HeterogeneousVolumeEmissionTexture,
	FRDGTextureRef& HeterogeneousVolumeAlbedoTexture
);

// Sparse Voxel Pipeline

void CopyTexture3D(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef Texture,
	uint32 InputMipLevel,
	FRDGTextureRef& OutputTexture
);

void GenerateSparseVoxels(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef VoxelMinTexture,
	FIntVector VolumeResolution,
	uint32 MipLevel,
	FRDGBufferRef& NumVoxelsBuffer,
	FRDGBufferRef& VoxelBuffer
);

#if RHI_RAYTRACING

void GenerateRayTracingGeometryInstance(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Sparse voxel data
	FRDGBufferRef NumVoxelsBuffer,
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	// Output
	TArray<FRayTracingGeometryRHIRef>& RayTracingGeometries,
	TArray<FMatrix>& RayTracingTransforms
);

void GenerateRayTracingScene(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Ray tracing data
	TArray<FRayTracingGeometryRHIRef>& RayTracingGeometries,
	TArray<FMatrix>& RayTracingTransforms,
	// Output
	FRayTracingScene& RayTracingScene
);

void RenderTransmittanceVolumeWithPreshadingHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	// Light data
	bool bApplyEmissionAndTransmittance,
	bool bApplyDirectLighting,
	bool bApplyShadowTransmittance,
	uint32 LightType,
	const FLightSceneInfo* LightSceneInfo,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Sparse voxel data
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	// Ray tracing data
	FRayTracingScene& RayTracingScene,
	// Output
	FRDGTextureRef& TransmittanceVolumeTexture
);

void RenderSingleScatteringWithPreshadingHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	// Light data
	bool bApplyEmissionAndTransmittance,
	bool bApplyDirectLighting,
	bool bApplyShadowTransmittance,
	uint32 LightType,
	const FLightSceneInfo* LightSceneInfo,
	// Shadow data
	const FVisibleLightInfo* VisibleLightInfo,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Sparse voxel data
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	// Ray tracing data
	FRayTracingScene& RayTracingScene,
	// Transmittance volume
	FRDGTextureRef TransmittanceVolumeTexture,
	// Output
	FRDGTextureRef& HeterogeneousVolumeTexture
);

#endif // RHI_RAYTRACING