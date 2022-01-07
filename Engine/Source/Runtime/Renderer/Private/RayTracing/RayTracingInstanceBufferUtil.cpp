// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceBufferUtil.h"

#include "RayTracingDefinitions.h"
#include "GPUScene.h"

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
	Output.NumNativeGPUSceneInstances = 0;
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

		const bool bGpuSceneInstance = !InstanceDesc.InstanceSceneDataOffsets.IsEmpty();
		const bool bGpuInstance = InstanceDesc.GPUTransformsSRV != nullptr;
		const bool bCpuInstance = !bGpuSceneInstance && !bGpuInstance;

		checkf(!bGpuSceneInstance || InstanceDesc.NumTransforms <= uint32(InstanceDesc.InstanceSceneDataOffsets.Num()),
			TEXT("Expected at least %d ray tracing geometry instance scene data offsets, but got %d."),
			InstanceDesc.NumTransforms, InstanceDesc.InstanceSceneDataOffsets.Num());
		checkf(!bCpuInstance || InstanceDesc.NumTransforms <= uint32(InstanceDesc.Transforms.Num()),
			TEXT("Expected at least %d ray tracing geometry instance transforms, but got %d."),
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

		if (bGpuSceneInstance)
		{
			check(InstanceDesc.GPUTransformsSRV == nullptr && InstanceDesc.Transforms.IsEmpty());
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeGPUSceneInstances;
			Output.NumNativeGPUSceneInstances += InstanceDesc.NumTransforms;
		}
		else if (bCpuInstance)
		{
			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeCPUInstances;
			Output.NumNativeCPUInstances += InstanceDesc.NumTransforms;
		}
		else
		{
			if (InstanceDesc.NumTransforms)
			{
				FRayTracingGPUInstance GPUInstance;
				GPUInstance.TransformSRV = InstanceDesc.GPUTransformsSRV;
				GPUInstance.DescBufferOffset = Output.NumNativeGPUInstances;
				GPUInstance.NumInstances = InstanceDesc.NumTransforms;
				Output.GPUInstances.Add(GPUInstance);
			}

			Output.BaseUploadBufferOffsets[InstanceIndex] = Output.NumNativeGPUInstances;
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
	uint32 NumNativeGPUSceneInstances,
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
			NumNativeGPUSceneInstances,
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

			const bool bGpuSceneInstance = !SceneInstance.InstanceSceneDataOffsets.IsEmpty();
			const bool bGpuInstance = SceneInstance.GPUTransformsSRV != nullptr;
			const bool bCpuInstance = !bGpuSceneInstance && !bGpuInstance;

			checkf(bGpuSceneInstance + bGpuInstance + bCpuInstance == 1, TEXT("Instance can only get transforms from one of GPUScene, GPUTransformsSRV, or Transforms array."));

			const uint32 AccelerationStructureIndex = InstanceGeometryIndices[SceneInstanceIndex];
			const uint32 BaseInstanceIndex = SceneInitializer.BaseInstancePrefixSum[SceneInstanceIndex];
			const uint32 BaseTransformIndex = bCpuInstance ? BaseUploadBufferOffsets[SceneInstanceIndex] : 0;

			uint32 BaseDescriptorIndex = BaseUploadBufferOffsets[SceneInstanceIndex];

			// Upload buffer is split into 3 sections [GPUSceneInstances][CPUInstances][GPUInstances]
			if (!bGpuSceneInstance)
			{
				BaseDescriptorIndex += NumNativeGPUSceneInstances;
				
				if (!bCpuInstance)
				{
					BaseDescriptorIndex += NumNativeCPUInstances;
				}
			}

			int32 NumInactiveNativeInstancesThisSceneInstance = 0;
			for (uint32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				FRayTracingInstanceDescriptorInput& InstanceDesc = OutInstanceUploadData[BaseDescriptorIndex + TransformIndex];

				InstanceDesc.InstanceMaskAndFlags = SceneInstance.Mask | ((uint32)SceneInstance.Flags << 8);
				InstanceDesc.InstanceContributionToHitGroupIndex = SceneInitializer.SegmentPrefixSum[SceneInstanceIndex] * SceneInitializer.ShaderSlotsPerGeometrySegment;
				InstanceDesc.InstanceId = bUseUniqueUserData ? SceneInstance.UserData[TransformIndex] : SceneInstance.DefaultUserData;
				InstanceDesc.OutputDescriptorIndex = BaseInstanceIndex + TransformIndex;

				if (bGpuSceneInstance)
				{
					InstanceDesc.GPUSceneInstanceOrTransformIndex = SceneInstance.InstanceSceneDataOffsets[TransformIndex];
				}
				else
				{
					InstanceDesc.GPUSceneInstanceOrTransformIndex = BaseTransformIndex + TransformIndex;
				}

				InstanceDesc.AccelerationStructureIndex = AccelerationStructureIndex;

				if (!SceneInstance.ActivationMask.IsEmpty() && (SceneInstance.ActivationMask[TransformIndex / 32] & (1 << (TransformIndex % 32))) == 0)
				{
					// Set flag for deactivated instances
					InstanceDesc.AccelerationStructureIndex = 0xFFFFFFFF;
					NumInactiveNativeInstancesThisSceneInstance++;
					continue;
				}

				if (bCpuInstance)
				{
					const uint32 TransformDataOffset = InstanceDesc.GPUSceneInstanceOrTransformIndex * 3;
					const FMatrix44f LocalToWorld = SceneInstance.Transforms[TransformIndex].GetTransposed();
					OutTransformData[TransformDataOffset + 0] = *(FVector4f*)&LocalToWorld.M[0];
					OutTransformData[TransformDataOffset + 1] = *(FVector4f*)&LocalToWorld.M[1];
					OutTransformData[TransformDataOffset + 2] = *(FVector4f*)&LocalToWorld.M[2];
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
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)

		SHADER_PARAMETER_UAV(RWStructuredBuffer, InstancesDescriptors)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRayTracingInstanceDescriptorInput>, InputInstanceDescriptors)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, AccelerationStructureAddresses)
		SHADER_PARAMETER_SRV(StructuredBuffer, InstanceTransforms)

		SHADER_PARAMETER(uint32, NumInstances)
		SHADER_PARAMETER(uint32, InputDescOffset)

		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)
	END_SHADER_PARAMETER_STRUCT()

	class FUseGPUSceneDim : SHADER_PERMUTATION_BOOL("USE_GPUSCENE");
	using FPermutationDomain = TShaderPermutationDomain<FUseGPUSceneDim>;
		
	static constexpr uint32 ThreadGroupSize = 64;

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsRayTracingEnabledForProject(Parameters.Platform) && RHISupportsComputeShaders(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingBuildInstanceBufferCS, "/Engine/Private/Raytracing/RayTracingInstanceBufferUtil.usf", "RayTracingBuildInstanceBufferCS", SF_Compute);

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
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

	if (GPUScene)
	{
		PassParams.InstanceSceneDataSOAStride = GPUScene->InstanceSceneDataSOAStride;
		PassParams.GPUSceneInstanceSceneData = GPUScene->InstanceSceneDataBuffer.SRV;
		PassParams.GPUSceneInstancePayloadData = GPUScene->InstancePayloadDataBuffer.SRV;
		PassParams.GPUScenePrimitiveSceneData = GPUScene->PrimitiveBuffer.SRV;
	}

	FRayTracingBuildInstanceBufferCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingBuildInstanceBufferCS::FUseGPUSceneDim>(InstanceTransformSRV == nullptr);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FRayTracingBuildInstanceBufferCS>(PermutationVector);
	const int32 GroupSize = FMath::DivideAndRoundUp(PassParams.NumInstances, FRayTracingBuildInstanceBufferCS::ThreadGroupSize);

	//ClearUnusedGraphResources(ComputeShader, PassParams);

	RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

	SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), PassParams);

	DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupSize, 1, 1);

	UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader.GetComputeShader());
}

void BuildRayTracingInstanceBuffer(
	FRHICommandList& RHICmdList,
	const FGPUScene* GPUScene,
	FUnorderedAccessViewRHIRef InstancesUAV,
	FShaderResourceViewRHIRef InstanceUploadSRV,
	FShaderResourceViewRHIRef AccelerationStructureAddressesSRV,
	FShaderResourceViewRHIRef CPUInstanceTransformSRV,
	uint32 NumNativeGPUSceneInstances,
	uint32 NumNativeCPUInstances,
	TConstArrayView<FRayTracingGPUInstance> GPUInstances)
{
	if (NumNativeGPUSceneInstances > 0)
	{
		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUScene,
			NumNativeGPUSceneInstances,
			0,
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			nullptr);
	}

	if (NumNativeCPUInstances > 0)
	{
		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUScene,
			NumNativeCPUInstances,
			NumNativeGPUSceneInstances, // CPU instance input descriptors are stored after GPU Scene instances
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			CPUInstanceTransformSRV);
	}

	for (const auto& GPUInstance : GPUInstances)
	{
		// GPU instance input descriptors are stored after CPU instances
		const uint32 InputDescOffset = NumNativeGPUSceneInstances + NumNativeCPUInstances + GPUInstance.DescBufferOffset;

		BuildRayTracingInstanceBuffer(
			RHICmdList,
			GPUScene,
			GPUInstance.NumInstances,
			InputDescOffset,
			InstancesUAV,
			InstanceUploadSRV,
			AccelerationStructureAddressesSRV,
			GPUInstance.TransformSRV);
	}
}

#endif //RHI_RAYTRACING
