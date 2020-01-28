// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RaytracingOptions.h declares ray tracing options for use in rendering
=============================================================================*/

#pragma once

#include "UniformBuffer.h"
#include "RenderGraph.h"
#include "Halton.h"
#include "BlueNoise.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyLightData, )
	SHADER_PARAMETER(uint32, SamplesPerPixel)
	SHADER_PARAMETER(uint32, SamplingStopLevel)
	SHADER_PARAMETER(float, MaxRayDistance)
	SHADER_PARAMETER(FVector, Color)
	SHADER_PARAMETER(FIntVector, MipDimensions)
	SHADER_PARAMETER(float, MaxNormalBias)
	SHADER_PARAMETER(float, MaxShadowThickness)
	SHADER_PARAMETER_TEXTURE(TextureCube, Texture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
	SHADER_PARAMETER(FIntVector, TextureDimensions)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePosX)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreeNegX)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePosY)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreeNegY)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePosZ)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreeNegZ)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfPosX)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfNegX)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfPosY)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfNegY)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfPosZ)
	SHADER_PARAMETER_SRV(Buffer<float>, MipTreePdfNegZ)
	SHADER_PARAMETER_SRV(Buffer<float>, SolidAnglePdf)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSkyLightQuasiRandomData, )
	SHADER_PARAMETER_STRUCT_REF(FHaltonIteration, HaltonIteration)
	SHADER_PARAMETER_STRUCT_REF(FHaltonPrimes, HaltonPrimes)
	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FWritableSkyLightVisibilityRaysData, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<SkyLightVisibilityRays>, OutSkyLightVisibilityRays)
	SHADER_PARAMETER(FIntVector, SkyLightVisibilityRaysDimensions)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSkyLightVisibilityRaysData, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<SkyLightVisibilityRays>, SkyLightVisibilityRays)
	SHADER_PARAMETER(FIntVector, SkyLightVisibilityRaysDimensions)
END_SHADER_PARAMETER_STRUCT()

#if RHI_RAYTRACING

int32 GetRayTracingSkyLightDecoupleSampleGenerationCVarValue();

void SetupSkyLightParameters(const FScene& Scene, FSkyLightData* SkyLight);
void SetupSkyLightQuasiRandomParameters(const FScene& Scene, const FViewInfo& View, FIntVector& OutBlueNoiseDimensions, FSkyLightQuasiRandomData* OutSkyLightQuasiRandomData);
void SetupSkyLightVisibilityRaysParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FSkyLightVisibilityRaysData* OutSkyLightVisibilityRaysData);

#endif // RHI_RAYTRACING
