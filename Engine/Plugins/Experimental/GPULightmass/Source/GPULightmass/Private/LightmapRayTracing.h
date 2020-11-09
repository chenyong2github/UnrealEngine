// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GPULightmassCommon.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "PathTracingUniformBuffers.h"
#include "RayTracing/RayTracingSkyLight.h"
#include "SceneView.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "IrradianceCaching.h"

#if RHI_RAYTRACING

class FLightmapRayTracingMeshProcessor : public FRayTracingMeshProcessor
{
public:
	FLightmapRayTracingMeshProcessor(FRayTracingMeshCommandContext* InCommandContext, FMeshPassProcessorRenderState InPassDrawRenderState)
		: FRayTracingMeshProcessor(InCommandContext, nullptr, nullptr, InPassDrawRenderState)
	{}

	virtual void Process(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		FMaterialShadingModelField ShadingModels,
		const FUniformLightMapPolicy& RESTRICT LightMapPolicy,
		const typename FUniformLightMapPolicy::ElementDataType& RESTRICT LightMapElementData) override;
};

class FLightmapPathTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLightmapPathTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLightmapPathTracingRGS, FGlobalShader)

	class FUseIrradianceCaching : SHADER_PERMUTATION_BOOL("USE_IRRADIANCE_CACHING");
	class FUseFirstBounceRayGuiding : SHADER_PERMUTATION_BOOL("USE_FIRST_BOUNCE_RAY_GUIDING");

	using FPermutationDomain = TShaderPermutationDomain<FUseIrradianceCaching, FUseFirstBounceRayGuiding>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.SetDefine(TEXT("LIGHTMAP_PATH_TRACING_MAIN_RG"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int, LastInvalidationFrame)
		SHADER_PARAMETER(int, NumTotalSamples)
		SHADER_PARAMETER(int, NumRayGuidingTrialSamples)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferWorldPosition)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferWorldNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferShadingNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, IrradianceAndSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHDirectionality)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SHCorrectionAndStationarySkyLightBentNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RayGuidingLuminance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RayGuidingCDFX)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RayGuidingCDFY)
		SHADER_PARAMETER_SRV(StructuredBuffer<FGPUTileDescription>, BatchedTiles)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, LightParameters)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLight)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FIrradianceCachingParameters, IrradianceCachingParameters)
	END_SHADER_PARAMETER_STRUCT()
};

class FVolumetricLightmapPathTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVolumetricLightmapPathTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FVolumetricLightmapPathTracingRGS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, FrameNumber)
		SHADER_PARAMETER(FVector4, VolumeMin)
		SHADER_PARAMETER(FVector4, VolumeSize)
		SHADER_PARAMETER(FIntVector, IndirectionTextureDim)
		SHADER_PARAMETER(int32, NumTotalBricks)
		SHADER_PARAMETER(int32, BrickBatchOffset)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(Buffer<uint4>, BrickRequests)
		SHADER_PARAMETER_UAV(RWTexture3D<float3>, AmbientVector)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients0R)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients1R)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients0G)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients1G)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients0B)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, SHCoefficients1B)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutAmbientVector)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients0R)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients1R)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients0G)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients1G)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients0B)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, OutSHCoefficients1B)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, LightParameters)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLight)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FIrradianceCachingParameters, IrradianceCachingParameters)
	END_SHADER_PARAMETER_STRUCT()
};

class FStationaryLightShadowTracingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStationaryLightShadowTracingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FStationaryLightShadowTracingRGS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(Buffer<int>, LightTypeArray)
		SHADER_PARAMETER_SRV(Buffer<int>, ChannelIndexArray)
		SHADER_PARAMETER_SRV(Buffer<int>, LightSampleIndexArray)
		SHADER_PARAMETER_SRV(StructuredBuffer<FLightShaderParameters>, LightShaderParametersArray)
		SHADER_PARAMETER_SRV(StructuredBuffer<FGPUTileDescription>, BatchedTiles)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferWorldPosition)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferWorldNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GBufferShadingNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ShadowMaskSampleCount)
	END_SHADER_PARAMETER_STRUCT()
};

class FFirstBounceRayGuidingCDFBuildCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFirstBounceRayGuidingCDFBuildCS);
	SHADER_USE_PARAMETER_STRUCT(FFirstBounceRayGuidingCDFBuildCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsRayTracingShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int, NumRayGuidingTrialSamples)
		SHADER_PARAMETER_SRV(StructuredBuffer<FGPUTileDescription>, BatchedTiles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RayGuidingLuminance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayGuidingCDFX)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayGuidingCDFY)
	END_SHADER_PARAMETER_STRUCT()
};

#endif