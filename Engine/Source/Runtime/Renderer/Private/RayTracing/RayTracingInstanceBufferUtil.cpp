// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceBufferUtil.h"

#include "RayTracingDefinitions.h"

#include "RenderGraphBuilder.h"
#include "ShaderParameterUtils.h"
#include "RendererInterface.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#include "Async/ParallelFor.h"

#include "Experimental/Containers/SherwoodHashTable.h"

#if RHI_RAYTRACING

FRayTracingSceneWithGeometryInstances CreateRayTracingSceneWithGeometryInstances(
	TArrayView<FRayTracingGeometryInstance> Instances,
	uint32 NumShaderSlotsPerGeometrySegment,
	uint32 NumMissShaderSlots)
{
	const uint32 NumSceneInstances = Instances.Num();

	FRayTracingSceneWithGeometryInstances Output;
	Output.NumNativeCPUInstances = 0;
	Output.NumNativeGPUInstances = 0;
	Output.InstanceGeometryIndices.SetNumUninitialized(NumSceneInstances);
	Output.BaseUploadBufferOffsets.SetNumUninitialized(NumSceneInstances);

	FRayTracingSceneInitializer2 Initializer;
	Initializer.DebugName = FName(TEXT("FRayTracingScene"));
	Initializer.ShaderSlotsPerGeometrySegment = NumShaderSlotsPerGeometrySegment;
	Initializer.NumMissShaderSlots = NumMissShaderSlots;
	Initializer.PerInstanceGeometries.SetNumUninitialized(NumSceneInstances);
	Initializer.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
	Initializer.SegmentPrefixSum.SetNumUninitialized(NumSceneInstances);
	Initializer.NumNativeInstances = 0;
	Initializer.NumTotalSegments = 0;

	Experimental::TSherwoodMap<FRHIRayTracingGeometry*, uint32> UniqueGeometries;

	// Compute geometry segment and instance count prefix sums.
	// These are later used by GetHitRecordBaseIndex() during resource binding
	// and by GetBaseInstanceIndex() in shaders to emulate SV_InstanceIndex.

	for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
	{
		const FRayTracingGeometryInstance& InstanceDesc = Instances[InstanceIndex];

		const bool bGpuInstance = InstanceDesc.GPUTransformsSRV != nullptr;
		const bool bCpuInstance = !bGpuInstance;

		checkf(bGpuInstance || InstanceDesc.NumTransforms <= uint32(InstanceDesc.Transforms.Num()),
			TEXT("Expected at most %d ray tracing geometry instance transforms, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.Transforms.Num());

		checkf(InstanceDesc.GeometryRHI, TEXT("Ray tracing instance must have a valid geometry."));

		Initializer.PerInstanceGeometries[InstanceIndex] = InstanceDesc.GeometryRHI;

		// Compute geometry segment count prefix sum to be later used in GetHitRecordBaseIndex()
		Initializer.SegmentPrefixSum[InstanceIndex] = Initializer.NumTotalSegments;
		Initializer.NumTotalSegments += InstanceDesc.GeometryRHI->GetNumSegments();

		uint32 GeometryIndex = UniqueGeometries.FindOrAdd(InstanceDesc.GeometryRHI, Initializer.ReferencedGeometries.Num());
		Output.InstanceGeometryIndices[InstanceIndex] = GeometryIndex;
		if (GeometryIndex == Initializer.ReferencedGeometries.Num())
		{
			Initializer.ReferencedGeometries.Add(InstanceDesc.GeometryRHI);
		}

		if (bCpuInstance)
		{
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeCPUInstances;

			Output.NumNativeCPUInstances += InstanceDesc.NumTransforms;
		}
		else
		{
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeGPUInstances;

			if (InstanceDesc.NumTransforms)
			{
				FRayTracingGPUInstance GPUInstance;
				GPUInstance.TransformSRV = InstanceDesc.GPUTransformsSRV;
				GPUInstance.DescBufferOffset = Output.NumNativeGPUInstances;
				GPUInstance.NumInstances = InstanceDesc.NumTransforms;
				Output.GPUInstances.Add(GPUInstance);
			}

			Output.NumNativeGPUInstances += InstanceDesc.NumTransforms;
		}

		Initializer.BaseInstancePrefixSum[InstanceIndex] = Initializer.NumNativeInstances;
		Initializer.NumNativeInstances += InstanceDesc.NumTransforms;
	}

	Output.Scene = RHICreateRayTracingScene(MoveTemp(Initializer));

	return MoveTemp(Output);
}

void FillRayTracingInstanceUploadBuffer(
	FRayTracingSceneRHIRef RayTracingSceneRHI,
	TConstArrayView<FRayTracingGeometryInstance> Instances,
	TConstArrayView<uint32> InstanceGeometryIndices,
	TConstArrayView<uint32> BaseUploadBufferOffsets,
	uint32 NumNativeCPUInstances,
	TArrayView<FRayTracingInstanceDescriptorInput> OutInstanceUploadData,
	TArrayView<FVector4f> OutTransformData)
{
	const FRayTracingSceneInitializer2& SceneInitializer = RayTracingSceneRHI->GetInitializer();

	int32 NumInactiveNativeInstances = 0;

	const int32 NumSceneInstances = Instances.Num();
	ParallelFor(NumSceneInstances, 
		[
			OutInstanceUploadData,
			OutTransformData,
			NumNativeCPUInstances,
			Instances,
			InstanceGeometryIndices,
			BaseUploadBufferOffsets,
			&SceneInitializer,
			&NumInactiveNativeInstances
		](int32 SceneInstanceIndex)
		{
			const FRayTracingGeometryInstance& SceneInstance = Instances[SceneInstanceIndex];

			const uint32 NumTransforms = SceneInstance.NumTransforms;

			checkf(SceneInstance.UserData.Num() == 0 || SceneInstance.UserData.Num() >= int32(NumTransforms),
				TEXT("User data array must be either be empty (Instance.DefaultUserData is used), or contain one entry per entry in Transforms array."));

			check(SceneInstance.ActivationMask.IsEmpty() || SceneInstance.ActivationMask.Num() * 32 >= int32(NumTransforms));

			const bool bUseUniqueUserData = SceneInstance.UserData.Num() != 0;

			const bool bGpuInstance = SceneInstance.GPUTransformsSRV != nullptr;
			const bool bCpuInstance = !bGpuInstance;

			const uint32 AccelerationStructureIndex = InstanceGeometryIndices[SceneInstanceIndex];
			const uint32 BaseInstanceIndex = SceneInitializer.BaseInstancePrefixSum[SceneInstanceIndex];
			// GPU instance descriptors are stored after CPU instances
			const uint32 BaseDescriptorIndex = BaseUploadBufferOffsets[SceneInstanceIndex] + (bGpuInstance ? NumNativeCPUInstances : 0);
			const uint32 BaseTransformIndex = bCpuInstance ? BaseDescriptorIndex : 0;

			int32 NumInactiveNativeInstancesThisSceneInstance = 0;
			for (uint32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				FRayTracingInstanceDescriptorInput& InstanceDesc = OutInstanceUploadData[BaseDescriptorIndex + TransformIndex];

				InstanceDesc.InstanceMaskAndFlags = SceneInstance.Mask | ((uint32)SceneInstance.Flags << 8);
				InstanceDesc.InstanceContributionToHitGroupIndex = SceneInitializer.SegmentPrefixSum[SceneInstanceIndex] * RAY_TRACING_NUM_SHADER_SLOTS;
				InstanceDesc.InstanceId = bUseUniqueUserData ? SceneInstance.UserData[TransformIndex] : SceneInstance.DefaultUserData;
				//InstanceDesc.GPUSceneInstanceIndex = 0; // TODO
				InstanceDesc.TransformIndex = BaseTransformIndex + TransformIndex;
				InstanceDesc.OutputDescriptorIndex = BaseInstanceIndex + TransformIndex;

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
					OutTransformData[InstanceDesc.TransformIndex * 3 + 0] = *(FVector4f*)&LocalToWorld.M[0];
					OutTransformData[InstanceDesc.TransformIndex * 3 + 1] = *(FVector4f*)&LocalToWorld.M[1];
					OutTransformData[InstanceDesc.TransformIndex * 3 + 2] = *(FVector4f*)&LocalToWorld.M[2];
				}
			}

#if STATS
			FPlatformAtomics::InterlockedAdd(&NumInactiveNativeInstances, NumInactiveNativeInstancesThisSceneInstance);
#endif
		});

	SET_DWORD_STAT(STAT_RayTracingInstances, SceneInitializer.NumNativeInstances - NumInactiveNativeInstances);
}

struct FRayTracingBuildInstanceBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBuildInstanceBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FRayTracingBuildInstanceBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(RWStructuredBuffer, InstancesDescriptors)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRayTracingInstanceDescriptorInput>, InputInstanceDescriptors)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, AccelerationStructureAddresses)
		SHADER_PARAMETER_SRV(StructuredBuffer, InstanceTransforms)
		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER(uint32, InputDescOffset)
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

IMPLEMENT_GLOBAL_SHADER(FRayTracingBuildInstanceBufferCS, "/Engine/Private/Raytracing/RayTracingInstanceBufferUtil.usf", "RayTracingBuildInstanceBufferCS", SF_Compute);

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	uint32 NumInstances,
	uint32 InputDescOffset,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef InstanceTransformSRV)
{
	FRayTracingBuildInstanceBufferCS::FParameters PassParams;
	PassParams.InstancesDescriptors = InstancesUAV;
	PassParams.InputInstanceDescriptors = InstanceUploadSRV;
	PassParams.AccelerationStructureAddresses = AccelerationStructureAddressesSRV;
	PassParams.InstanceTransforms = InstanceTransformSRV;
	PassParams.NumInstances = NumInstances;
	PassParams.InputDescOffset = InputDescOffset;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FRayTracingBuildInstanceBufferCS>();
	const int32 GroupSize = FMath::DivideAndRoundUp(PassParams.NumInstances, FRayTracingBuildInstanceBufferCS::ThreadGroupSize);

	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), PassParams);

	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize, 1, 1);

	UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
}

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef CPUInstanceTransformSRV,
	uint32 NumNativeCPUInstances,
	TConstArrayView<FRayTracingGPUInstance> GPUInstances)
{
	RHICmdList.BeginUAVOverlap(InstancesUAV);

	BuildRayTracingInstanceBuffer(
		RHICmdList,
		NumNativeCPUInstances,
		0,
		InstancesUAV,
		InstanceUploadSRV,
		AccelerationStructureAddressesSRV,
		CPUInstanceTransformSRV);

	for (const auto& GPUInstance : GPUInstances)
	{
		// GPU instance input descriptors are stored after CPU instances
		uint32 InputDescOffset = NumNativeCPUInstances + GPUInstance.DescBufferOffset;

		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUInstance.NumInstances,
			InputDescOffset,
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			GPUInstance.TransformSRV);
	}

	RHICmdList.EndUAVOverlap(InstancesUAV);
}

#endif //RHI_RAYTRACING
