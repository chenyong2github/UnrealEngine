// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "ShaderParameterMacros.h"
#include "SceneRendering.h"
#include "LightSceneInfo.h"
#include "RayTracingDefinitions.h"
#include "Containers/DynamicRHIResourceArray.h"

#if RHI_RAYTRACING

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRaytracingLightDataPacked, )
	SHADER_PARAMETER(uint32, Count)
	SHADER_PARAMETER(float, IESLightProfileInvCount)
	SHADER_PARAMETER_TEXTURE(Texture2D, LTCMatTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, LTCMatSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, LTCAmpTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, LTCAmpSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture0)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture1)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture2)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture3)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture4)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture5)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture6)
	SHADER_PARAMETER_TEXTURE(Texture2D, RectLightTexture7)
	SHADER_PARAMETER_SAMPLER(SamplerState, IESLightProfileTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, IESLightProfileTexture)
	SHADER_PARAMETER_SRV(Texture2D, SSProfilesTexture)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, LightDataBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Must match struct definition in RayTacedLightingCommon.ush
struct FRTLightingData
{
	int32 Type;
	int32 LightProfileIndex;
	int32 RectLightTextureIndex;

	// Force alignment before next vector
	int32 Pad;

	float LightPosition[3];
	float InvRadius;
	float Direction[3];
	float FalloffExponent;
	float LightColor[3];
	float SpecularScale;
	float Tangent[3];
	float SourceRadius;
	float SpotAngles[2];
	float SourceLength;
	float SoftSourceRadius;
	float DistanceFadeMAD[2];
	float RectLightBarnCosAngle;
	float RectLightBarnLength;

	// Align struct to 128 bytes to better match cache lines
	float Dummy[4];
};

static_assert(sizeof(FRTLightingData) == 128, "Unexpected FRTLightingData size.");

void SetupRaytracingLightDataPacked(
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const FViewInfo& View,
	FRaytracingLightDataPacked* LightData,
	TResourceArray<FRTLightingData>& LightDataArray);

TUniformBufferRef<FRaytracingLightDataPacked> CreateLightDataPackedUniformBuffer(
	const TSparseArray<FLightSceneInfoCompact>& Lights,
	const class FViewInfo& View, EUniformBufferUsage Usage,
	FStructuredBufferRHIRef& OutLightDataBuffer,
	FShaderResourceViewRHIRef& OutLightDataSRV);

#endif
