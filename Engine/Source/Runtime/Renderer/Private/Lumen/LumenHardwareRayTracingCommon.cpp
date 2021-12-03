// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenHardwareRayTracingCommon.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"

static TAutoConsoleVariable<int32> CVarLumenUseHardwareRayTracing(
	TEXT("r.Lumen.HardwareRayTracing"),
	0,
	TEXT("Uses Hardware Ray Tracing for Lumen features, when available.\n")
	TEXT("Lumen will fall back to Software Ray Tracing otherwise.\n")
	TEXT("Note: Hardware ray tracing has significant scene update costs for\n")
	TEXT("scenes with more than 10k instances."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingPullbackBias(
	TEXT("r.Lumen.HardwareRayTracing.PullbackBias"),
	8.0,
	TEXT("Determines the pull-back bias when resuming a screen-trace ray (default = 8.0)"),
	ECVF_RenderThreadSafe
);

bool Lumen::UseHardwareRayTracing()
{
#if RHI_RAYTRACING
	// As of 2021-11-24, Lumen can only run in full RT pipeline mode.
	// It will be able to run using inline ray tracing mode in the future.
	return (IsRayTracingEnabled() && GRHISupportsRayTracingShaders && CVarLumenUseHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
	return false;
#endif
}

#if RHI_RAYTRACING

#include "LumenHardwareRayTracingCommon.h"

namespace Lumen
{
	const TCHAR* GetRayTracedNormalModeName(int NormalMode)
	{
		if (NormalMode == 0)
		{
			return TEXT("SDF");
		}

		return TEXT("Geometry");
	}

	float GetHardwareRayTracingPullbackBias()
	{
		return CVarLumenHardwareRayTracingPullbackBias.GetValueOnRenderThread();
	}
}

void SetLumenHardwareRayTracingSharedParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenHardwareRayTracingRGS::FSharedParameters* SharedParameters
)
{
	SharedParameters->SceneTextures = SceneTextures;
	//SharedParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	checkf(View.HasRayTracingScene(), TEXT("TLAS does not exist. Verify that the current pass is represented in Lumen::AnyLumenHardwareRayTracingPassEnabled()."));
	SharedParameters->TLAS = View.GetRayTracingSceneViewChecked();

	// Lighting data
	SharedParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	SharedParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
	SharedParameters->SSProfilesTexture = View.RayTracingSubSurfaceProfileTexture;

	// Use surface cache, instead
	GetLumenCardTracingParameters(View, TracingInputs, SharedParameters->TracingParameters);
}

class FLumenHWRTCompactRaysIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHWRTCompactRaysIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenHWRTCompactRaysIndirectArgsCS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactRaysIndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenHWRTCompactRaysIndirectArgsCS, "/Engine/Private/Lumen/LumenHardwareRayTracingPipeline.usf", "FLumenHWRTCompactRaysIndirectArgsCS", SF_Compute);

class FLumenHWRTCompactRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHWRTCompactRaysCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenHWRTCompactRaysCS, FGlobalShader);

	class FCompactModeDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_COMPACT_MODE", LumenHWRTPipeline::ECompactMode);
	using FPermutationDomain = TShaderPermutationDomain<FCompactModeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, TraceDataPacked)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWRayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWTraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, RWTraceDataPacked)

		// Indirect args
		RDG_BUFFER_ACCESS(CompactRaysIndirectArgs, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenHWRTCompactRaysCS, "/Engine/Private/Lumen/LumenHardwareRayTracingPipeline.usf", "FLumenHWRTCompactRaysCS", SF_Compute);


class FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketRaysByMaterialIdIndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 16; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS, "/Engine/Private/Lumen/LumenHardwareRayTracingPipeline.usf", "FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS", SF_Compute);

class FLumenHWRTBucketRaysByMaterialIdCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHWRTBucketRaysByMaterialIdCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenHWRTBucketRaysByMaterialIdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RayAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, TraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, TraceDataPacked)

		SHADER_PARAMETER(int, MaxRayAllocationCount)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, RWTraceTexelDataPacked)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<LumenHWRTPipeline::FTraceDataPacked>, RWTraceDataPacked)

		// Indirect args
		RDG_BUFFER_ACCESS(BucketRaysByMaterialIdIndirectArgs, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 16; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenHWRTBucketRaysByMaterialIdCS, "/Engine/Private/Lumen/LumenHardwareRayTracingPipeline.usf", "FLumenHWRTBucketRaysByMaterialIdCS", SF_Compute);

void LumenHWRTCompactRays(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 RayCount,
	LumenHWRTPipeline::ECompactMode CompactMode,
	const FRDGBufferRef& RayAllocatorBuffer,
	const FRDGBufferRef& TraceTexelDataPackedBuffer,
	const FRDGBufferRef& TraceDataPackedBuffer,
	FRDGBufferRef& OutputRayAllocatorBuffer,
	FRDGBufferRef& OutputTraceTexelDataPackedBuffer,
	FRDGBufferRef& OutputTraceDataPackedBuffer
)
{
	FRDGBufferRef CompactRaysIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	{
		FLumenHWRTCompactRaysIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenHWRTCompactRaysIndirectArgsCS::FParameters>();
		{
			PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
			PassParameters->RWCompactRaysIndirectArgs = GraphBuilder.CreateUAV(CompactRaysIndirectArgsBuffer, PF_R32_UINT);
		}

		TShaderRef<FLumenHWRTCompactRaysIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenHWRTCompactRaysIndirectArgsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionCompactRaysIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FRDGBufferRef CompactedRayAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.Reflection.CompactedRayAllocator"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT), 0);

	FRDGBufferRef CompactedTexelTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflection.BucketedTexelTraceDataPackedBuffer"));
	FRDGBufferRef CompactedTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), RayCount), TEXT("Lumen.Reflection.CompactedTraceDataPacked"));
	{
		FLumenHWRTCompactRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenHWRTCompactRaysCS::FParameters>();
		{
			// Input
			PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
			PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceTexelDataPackedBuffer, PF_R32G32_UINT));
			PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceDataPackedBuffer));

			// Output
			PassParameters->RWRayAllocator = GraphBuilder.CreateUAV(CompactedRayAllocatorBuffer, PF_R32_UINT);
			PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTexelTraceDataPackedBuffer, PF_R32G32_UINT));
			PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CompactedTraceDataPackedBuffer));

			// Indirect args
			PassParameters->CompactRaysIndirectArgs = CompactRaysIndirectArgsBuffer;
		}

		FLumenHWRTCompactRaysCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenHWRTCompactRaysCS::FCompactModeDim>(CompactMode);
		TShaderRef<FLumenHWRTCompactRaysCS> ComputeShader = View.ShaderMap->GetShader<FLumenHWRTCompactRaysCS>(PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionCompactRays"),
			ComputeShader,
			PassParameters,
			PassParameters->CompactRaysIndirectArgs,
			0);
	}

	OutputRayAllocatorBuffer = CompactedRayAllocatorBuffer;
	OutputTraceTexelDataPackedBuffer = CompactedTexelTraceDataPackedBuffer;
	OutputTraceDataPackedBuffer = CompactedTraceDataPackedBuffer;
}

void LumenHWRTBucketRaysByMaterialID(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 RayCount,
	FRDGBufferRef& RayAllocatorBuffer,
	FRDGBufferRef& TraceTexelDataPackedBuffer,
	FRDGBufferRef& TraceDataPackedBuffer
)
{
	FRDGBufferRef BucketRaysByMaterialIdIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflections.BucketRaysByMaterialIdIndirectArgsBuffer"));
	{
		FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS::FParameters>();
		{
			PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
			PassParameters->RWBucketRaysByMaterialIdIndirectArgs = GraphBuilder.CreateUAV(BucketRaysByMaterialIdIndirectArgsBuffer, PF_R32_UINT);
		}

		TShaderRef<FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenHWRTBucketRaysByMaterialIdIndirectArgsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionBucketRaysByMaterialIdIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FRDGBufferRef BucketedTexelTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, RayCount), TEXT("Lumen.Reflections.BucketedTexelTraceDataPackedBuffer"));
	FRDGBufferRef BucketedTraceDataPackedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenHWRTPipeline::FTraceDataPacked), RayCount), TEXT("Lumen.Reflections.BucketedTraceDataPacked"));
	{
		FLumenHWRTBucketRaysByMaterialIdCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenHWRTBucketRaysByMaterialIdCS::FParameters>();
		{
			// Input
			PassParameters->RayAllocator = GraphBuilder.CreateSRV(RayAllocatorBuffer, PF_R32_UINT);
			PassParameters->TraceTexelDataPacked = GraphBuilder.CreateSRV(TraceTexelDataPackedBuffer, PF_R32G32_UINT);
			PassParameters->TraceDataPacked = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(TraceDataPackedBuffer));
			PassParameters->MaxRayAllocationCount = RayCount;

			// Output
			PassParameters->RWTraceTexelDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(BucketedTexelTraceDataPackedBuffer, PF_R32G32_UINT));
			PassParameters->RWTraceDataPacked = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(BucketedTraceDataPackedBuffer));

			// Indirect args
			PassParameters->BucketRaysByMaterialIdIndirectArgs = BucketRaysByMaterialIdIndirectArgsBuffer;
		}

		TShaderRef<FLumenHWRTBucketRaysByMaterialIdCS> ComputeShader = View.ShaderMap->GetShader<FLumenHWRTBucketRaysByMaterialIdCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionBucketRaysByMaterialId"),
			ComputeShader,
			PassParameters,
			PassParameters->BucketRaysByMaterialIdIndirectArgs,
			0);

		TraceTexelDataPackedBuffer = BucketedTexelTraceDataPackedBuffer;
		TraceDataPackedBuffer = BucketedTraceDataPackedBuffer;
	}
}

#endif // RHI_RAYTRACING