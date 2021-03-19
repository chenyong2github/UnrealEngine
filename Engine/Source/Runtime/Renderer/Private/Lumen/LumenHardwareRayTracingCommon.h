// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"

#include "RayTracing/RayTracingLighting.h"

// Actual screen-probe requirements..
#include "LumenRadianceCache.h"
#include "LumenScreenProbeGather.h"

namespace Lumen
{
	struct FHardwareRayTracingPermutationSettings
	{
		EHardwareRayTracingLightingMode LightingMode;
		bool bUseMinimalPayload;
		bool bUseDeferredMaterial;
	};
}

class FLumenHardwareRayTracingRGS : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FSharedParameters, )
		// Scene includes
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		// Lighting structures
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)

		// Surface cache
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
	END_SHADER_PARAMETER_STRUCT()


	FLumenHardwareRayTracingRGS() = default;
	FLumenHardwareRayTracingRGS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FGlobalShader(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 1);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

class FLumenHardwareRayTracingDeferredMaterialRGS : public FLumenHardwareRayTracingRGS
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FDeferredMaterialParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER(int, TileSize)
		SHADER_PARAMETER(FIntPoint, DeferredMaterialBufferResolution)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FDeferredMaterialPayload>, RWDeferredMaterialBuffer)
	END_SHADER_PARAMETER_STRUCT()

	FLumenHardwareRayTracingDeferredMaterialRGS() = default;
	FLumenHardwareRayTracingDeferredMaterialRGS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FLumenHardwareRayTracingRGS(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FLumenHardwareRayTracingRGS::ShouldCompilePermutation(Parameters);
	}
};

void SetLumenHardwareRayTracingSharedParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenHardwareRayTracingRGS::FSharedParameters* SharedParameters);

#endif // RHI_RAYTRACING