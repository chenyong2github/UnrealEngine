// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "ShaderParameterMacros.h"
#include "SceneRendering.h"
#include "LightSceneInfo.h"
#include "RayTracingDefinitions.h"
#include "Containers/DynamicRHIResourceArray.h"

#if RHI_RAYTRACING

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRaytracingLightDataPacked, RENDERER_API)
	SHADER_PARAMETER(uint32, Count)
	SHADER_PARAMETER(float, IESLightProfileInvCount)
	SHADER_PARAMETER(uint32, CellCount)
	SHADER_PARAMETER(float, CellScale)
	SHADER_PARAMETER_SAMPLER(SamplerState, IESLightProfileTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, IESLightProfileTexture)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, LightDataBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, LightIndices)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, LightCullingVolume)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Must match struct definition in RayTacedLightingCommon.ush
struct FRTLightingData
{
	int32 Type;
	int32 LightProfileIndex;
	float RectLightAtlasMaxLevel;

	// Force alignment before next vector
	int32 Pad;

	FVector3f TranslatedLightPosition;
	float InvRadius;
	FVector3f Direction;
	float FalloffExponent;
	FVector3f LightColor;
	float SpecularScale;
	FVector3f Tangent;
	float SourceRadius;
	float SpotAngles[2];
	float SourceLength;
	float SoftSourceRadius;
	float DistanceFadeMAD[2];
	float RectLightBarnCosAngle;
	float RectLightBarnLength;
	float RectLightAtlasUVOffset[2];
	float RectLightAtlasUVScale[2];
	// Align struct to 128 bytes to better match cache lines
};

static_assert(sizeof(FRTLightingData) == 128, "Unexpected FRTLightingData size.");

FRayTracingLightData CreateRayTracingLightData(
	FRHICommandListImmediate& RHICmdList,
	const TSparseArray<FLightSceneInfoCompact, TAlignedSparseArrayAllocator<alignof(FLightSceneInfoCompact)>>& Lights,
	const FViewInfo& View, EUniformBufferUsage Usage);

#endif
