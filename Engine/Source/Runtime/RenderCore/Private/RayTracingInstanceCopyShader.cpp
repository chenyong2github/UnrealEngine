// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceCopyShader.h"

#include "RayTracingDefinitions.h"

#include "RenderGraphBuilder.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#include "Async/ParallelFor.h"

#if RHI_RAYTRACING

void FillInstanceUploadBuffer(
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstancesGeometryIndex,
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData)
{
	const FRayTracingSceneInitializer2& SceneInitializer = RayTracingSceneRHI->GetInitializer();

	int32 NumInactiveNativeInstances = 0;

	const int32 NumSceneInstances = Instances.Num();
	ParallelFor(NumSceneInstances, [OutInstanceUploadData, Instances, InstancesGeometryIndex, &SceneInitializer, &NumInactiveNativeInstances](int32 SceneInstanceIndex)
		{
			const FRayTracingGeometryInstance& SceneInstance = Instances[SceneInstanceIndex];

			const uint32 NumTransforms = SceneInstance.NumTransforms;

			checkf(SceneInstance.UserData.Num() == 0 || SceneInstance.UserData.Num() >= int32(NumTransforms),
				TEXT("User data array must be either be empty (Instance.DefaultUserData is used), or contain one entry per entry in Transforms array."));

			check(SceneInstance.ActivationMask.IsEmpty() || SceneInstance.ActivationMask.Num() * 32 >= int32(NumTransforms));

			const bool bUseUniqueUserData = SceneInstance.UserData.Num() != 0;

			const bool bGpuInstance = SceneInstance.GPUTransformsSRV != nullptr;
			const bool bCpuInstance = !bGpuInstance;

			const uint32 AccelerationStructureIndex = InstancesGeometryIndex[SceneInstanceIndex];

			uint32 BaseInstanceIndex = SceneInitializer.BaseInstancePrefixSum[SceneInstanceIndex];

			int32 NumInactiveNativeInstancesThisSceneInstance = 0;
			for (uint32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				FRayTracingInstanceDescriptorInput& InstanceDesc = OutInstanceUploadData[BaseInstanceIndex + TransformIndex];
				InstanceDesc.InstanceMaskAndFlags = SceneInstance.Mask | ((uint32)SceneInstance.Flags << 8);
				InstanceDesc.InstanceContributionToHitGroupIndex = SceneInitializer.SegmentPrefixSum[SceneInstanceIndex] * RAY_TRACING_NUM_SHADER_SLOTS;
				InstanceDesc.InstanceId = bUseUniqueUserData ? SceneInstance.UserData[TransformIndex] : SceneInstance.DefaultUserData;
				//InstanceDesc.GPUSceneInstanceIndex = 0; // TODO

				InstanceDesc.AccelerationStructureIndex = AccelerationStructureIndex;

				if (!SceneInstance.ActivationMask.IsEmpty() && (SceneInstance.ActivationMask[TransformIndex / 32] & (1 << (TransformIndex % 32))) == 0)
				{
					// Set flag for deactivated instances
					InstanceDesc.AccelerationStructureIndex = 0xFFFFFFFF;
					NumInactiveNativeInstancesThisSceneInstance++;
					continue;
				}

				if (LIKELY(bCpuInstance))
				{
					const FMatrix44f LocalToWorld = SceneInstance.Transforms[TransformIndex].GetTransposed();
					InstanceDesc.LocalToWorld[0] = *(FVector4f*)&LocalToWorld.M[0];
					InstanceDesc.LocalToWorld[1] = *(FVector4f*)&LocalToWorld.M[1];
					InstanceDesc.LocalToWorld[2] = *(FVector4f*)&LocalToWorld.M[2];
				}
				else
				{
					// #todo: GPU-based instances transform should be copied from GPUTransformsSRV
					FMemory::Memset(&InstanceDesc.LocalToWorld, 0, sizeof(InstanceDesc.LocalToWorld));
				}
			}

#if STATS
			FPlatformAtomics::InterlockedAdd(&NumInactiveNativeInstances, NumInactiveNativeInstancesThisSceneInstance);
#endif
		});

	SET_DWORD_STAT(STAT_RayTracingInstances, SceneInitializer.NumNativeInstances - NumInactiveNativeInstances);
}

struct FRayTracingInstanceCopyCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingInstanceCopyCS);
	SHADER_USE_PARAMETER_STRUCT(FRayTracingInstanceCopyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, InstancesDescriptors)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, InstancesTransforms)
		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER(uint32, DescBufferOffset)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr uint32 ThreadGroupSize = 64;
	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return (ShouldCompileRayTracingShadersForProject(Parameters.Platform) && RHISupportsComputeShaders(Parameters.Platform) && !(Parameters.Platform == EShaderPlatform::SP_METAL || Parameters.Platform == EShaderPlatform::SP_METAL_TVOS || IsMobilePlatform(Parameters.Platform)));
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingInstanceCopyCS, "/Engine/Private/Raytracing/RayTracingInstanceCopy.usf", "RayTracingInstanceCopyShaderCS", SF_Compute);

struct FRayTracingInstanceBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingInstanceBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FRayTracingInstanceBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(RWStructuredBuffer, InstancesDescriptors)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRayTracingInstanceDescriptorInput>, InputInstanceDescriptors)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, AccelerationStructureAddresses)
		SHADER_PARAMETER(uint32, NumInstances)
	END_SHADER_PARAMETER_STRUCT()
		
	static constexpr uint32 ThreadGroupSize = 64;

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return (ShouldCompileRayTracingShadersForProject(Parameters.Platform) && RHISupportsComputeShaders(Parameters.Platform) && !(Parameters.Platform == EShaderPlatform::SP_METAL || Parameters.Platform == EShaderPlatform::SP_METAL_TVOS || IsMobilePlatform(Parameters.Platform)));
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingInstanceBufferCS, "/Engine/Private/Raytracing/RayTracingInstanceCopy.usf", "RayTracingBuildInstanceBufferCS", SF_Compute);

void BuildRayTracingInstanceBuffer(FRHICommandList& RHICmdList, uint32 NumInstances, FUnorderedAccessViewRHIRef InstancesUAV, FShaderResourceViewRHIRef InstanceUploadSRV, FShaderResourceViewRHIRef AccelerationStructureAddressesSRV)
{
	FRayTracingInstanceBufferCS::FParameters PassParams;
	PassParams.InstancesDescriptors = InstancesUAV;
	PassParams.InputInstanceDescriptors = InstanceUploadSRV;
	PassParams.AccelerationStructureAddresses = AccelerationStructureAddressesSRV;
	PassParams.NumInstances = NumInstances;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FRayTracingInstanceBufferCS>();
	const int32 GroupSize = FMath::DivideAndRoundUp(PassParams.NumInstances, FRayTracingInstanceBufferCS::ThreadGroupSize);

	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), PassParams);

	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize, 1, 1);

	UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
}

#endif //RHI_RAYTRACING
