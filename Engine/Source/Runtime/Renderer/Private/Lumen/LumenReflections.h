// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "BlueNoise.h"
#include "LumenRadianceCache.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "LumenSceneUtils.h"

const static int32 ReflectionThreadGroupSize2D = 8;

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionTracingParameters, )
	SHADER_PARAMETER(uint32, ReflectionDownsampleFactor)
	SHADER_PARAMETER(FIntPoint, ReflectionTracingViewSize)
	SHADER_PARAMETER(FIntPoint, ReflectionTracingBufferSize)
	SHADER_PARAMETER(float, MaxRayIntensity)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RayBuffer)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceHit)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadiance)

	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWTraceRadiance)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWTraceHit)

	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionTileParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ReflectionResolveTileData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ReflectionTracingTileData)
	RDG_BUFFER_ACCESS(ResolveIndirectArgs, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(TracingIndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCompactedReflectionTraceParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, CompactedTraceTexelData)
	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	RDG_BUFFER_ACCESS(RayTraceDispatchIndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

extern void TraceReflections(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	bool bTraceMeshSDFs,
	const FSceneTextures& SceneTextures,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters, 
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenMeshSDFGridParameters& InMeshSDFGridParameters);

class FLumenReflectionTracingParameters;
class FLumenReflectionTileParameters;
extern void RenderLumenHardwareRayTracingReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenReflectionTileParameters& ReflectionTileParameters,
	const FLumenCardTracingInputs& TracingInputs,
	const FCompactedReflectionTraceParameters& CompactedTraceParameters,
	float MaxTraceDistance);