// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
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

	// Struct definitions much match those in LumenHardwareRayTracingCommon.ush 
	struct FHitGroupRootConstants
	{
		uint32 BaseInstanceIndex;
		uint32 UserData;
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
		SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)

		// Surface cache
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
	END_SHADER_PARAMETER_STRUCT()


	FLumenHardwareRayTracingRGS() = default;
	FLumenHardwareRayTracingRGS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FGlobalShader(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ESurfaceCacheSampling SurfaceCacheSampling, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_FEEDBACK"), SurfaceCacheSampling == Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback ? 0 : 1);
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_HIGH_RES_PAGES"), SurfaceCacheSampling == Lumen::ESurfaceCacheSampling::HighResPages ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_RAYTRACING"), 1);

		// GPU Scene definitions
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

class FLumenHardwareRayTracingCS : public FGlobalShader
{
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FInlineParameters, )		
		SHADER_PARAMETER_SRV(StructuredBuffer<Lumen::FHitGroupRootConstants>, HitGroupData)
	END_SHADER_PARAMETER_STRUCT()


	FLumenHardwareRayTracingCS() = default;
	FLumenHardwareRayTracingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ESurfaceCacheSampling SurfaceCacheSampling, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_FEEDBACK"), SurfaceCacheSampling == Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback ? 0 : 1);
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_HIGH_RES_PAGES"), SurfaceCacheSampling == Lumen::ESurfaceCacheSampling::HighResPages ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("LUMEN_HARDWARE_INLINE_RAYTRACING"), 1);

		// GPU Scene definitions
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		// Current inline ray tracing implementation only supports wave32 mode.
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FLumenHardwareRayTracingRGS::ShouldCompilePermutation(Parameters) && FDataDrivenShaderPlatformInfo::GetSupportsInlineRayTracing(Parameters.Platform);
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

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ESurfaceCacheSampling SurfaceCacheSampling, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, SurfaceCacheSampling, OutEnvironment);
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

// Hardware ray-tracing pipeline
namespace LumenHWRTPipeline
{
	enum class ELightingMode
	{
		// Permutations for tracing modes
		SurfaceCache,
		HitLighting,
		MAX
	};

	enum class ECompactMode
	{
		// Permutations for compaction modes
		HitLightingRetrace,
		FarFieldRetrace,
		ForceHitLighting,
		AppendRays,

		MAX
	};

	// Struct definitions much match those in LumenHardwareRayTracingPipelineCommon.ush
	struct FTraceDataPacked
	{
		//uint32 PackedData[4];
		uint32 PackedData[5];
	};

} // namespace LumenHWRTPipeline

void LumenHWRTCompactRays(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 RayCount,
	LumenHWRTPipeline::ECompactMode CompactMode,
	const FRDGBufferRef& RayAllocatorBuffer,
	const FRDGBufferRef& TraceDataPackedBuffer,
	FRDGBufferRef& OutputRayAllocatorBuffer,
	FRDGBufferRef& OutputTraceDataPackedBuffer
);

void LumenHWRTBucketRaysByMaterialID(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 RayCount,
	FRDGBufferRef& RayAllocatorBuffer,
	FRDGBufferRef& TraceDataPackedBuffer
);

#endif // RHI_RAYTRACING