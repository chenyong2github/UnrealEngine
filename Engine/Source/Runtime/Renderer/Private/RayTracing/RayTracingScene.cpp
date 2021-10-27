// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingScene.h"

#if RHI_RAYTRACING

#include "RayTracingInstanceBufferUtil.h"
#include "RenderCore.h"
#include "RayTracingDefinitions.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "Experimental/Containers/SherwoodHashTable.h"

BEGIN_SHADER_PARAMETER_STRUCT(FBuildInstanceBufferPassParams, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, InstanceBuffer)
END_SHADER_PARAMETER_STRUCT()

FRayTracingScene::FRayTracingScene()
{

}

FRayTracingScene::~FRayTracingScene()
{
	WaitForTasks();
}

void FRayTracingScene::Create(FRDGBuilder& GraphBuilder)
{
	QUICK_SCOPE_CYCLE_COUNTER(FRayTracingScene_BeginCreate);

	WaitForTasks();

	TArray<uint32> InstancesGeometryIndex;

	{
		const uint32 NumSceneInstances = Instances.Num();

		FRayTracingSceneInitializer2 SceneInitializer;
		SceneInitializer.DebugName = FName(TEXT("FRayTracingScene"));
		SceneInitializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
		SceneInitializer.NumMissShaderSlots = RAY_TRACING_NUM_MISS_SHADER_SLOTS;
		SceneInitializer.PerInstanceGeometries.SetNumUninitialized(NumSceneInstances);
		SceneInitializer.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
		SceneInitializer.SegmentPrefixSum.SetNumUninitialized(NumSceneInstances);
		SceneInitializer.NumNativeInstances = 0;
		SceneInitializer.NumTotalSegments = 0;

		Experimental::TSherwoodMap<FRHIRayTracingGeometry*, uint32> UniqueGeometries;
		InstancesGeometryIndex.SetNumUninitialized(NumSceneInstances);

		// Compute geometry segment and instance count prefix sums.
		// These are later used by GetHitRecordBaseIndex() during resource binding
		// and by GetBaseInstanceIndex() in shaders to emulate SV_InstanceIndex.

		for (uint32 InstanceIndex = 0; InstanceIndex < NumSceneInstances; ++InstanceIndex)
		{
			const FRayTracingGeometryInstance& InstanceDesc = Instances[InstanceIndex];

			checkf(InstanceDesc.GPUTransformsSRV || InstanceDesc.NumTransforms <= uint32(InstanceDesc.Transforms.Num()),
				TEXT("Expected at most %d ray tracing geometry instance transforms, but got %d."),
				InstanceDesc.NumTransforms, InstanceDesc.Transforms.Num());

			checkf(InstanceDesc.GeometryRHI, TEXT("Ray tracing instance must have a valid geometry."));

			SceneInitializer.PerInstanceGeometries[InstanceIndex] = InstanceDesc.GeometryRHI;

			// Compute geometry segment count prefix sum to be later used in GetHitRecordBaseIndex()
			SceneInitializer.SegmentPrefixSum[InstanceIndex] = SceneInitializer.NumTotalSegments;
			SceneInitializer.NumTotalSegments += InstanceDesc.GeometryRHI->GetNumSegments();

			uint32 GeometryIndex = UniqueGeometries.FindOrAdd(InstanceDesc.GeometryRHI, SceneInitializer.ReferencedGeometries.Num());
			InstancesGeometryIndex[InstanceIndex] = GeometryIndex;
			if (GeometryIndex == SceneInitializer.ReferencedGeometries.Num())
			{
				SceneInitializer.ReferencedGeometries.Add(InstanceDesc.GeometryRHI);
			}

			SceneInitializer.BaseInstancePrefixSum[InstanceIndex] = SceneInitializer.NumNativeInstances;
			SceneInitializer.NumNativeInstances += InstanceDesc.NumTransforms;
		}

		RayTracingSceneRHI = RHICreateRayTracingScene(MoveTemp(SceneInitializer));
	}

	const FRayTracingSceneInitializer2& SceneInitializer = RayTracingSceneRHI->GetInitializer();

	// Round up number of instances to some multiple to avoid pathological growth reallocations.
	static constexpr uint32 AllocationGranularity = 8 * 1024;
	uint32 NumNativeInstancesAligned = FMath::DivideAndRoundUp(FMath::Max(SceneInitializer.NumNativeInstances, 1U), AllocationGranularity) * AllocationGranularity;

	SizeInfo = RHICalcRayTracingSceneSize(SceneInitializer.NumNativeInstances, ERayTracingAccelerationStructureFlags::FastTrace);
	FRayTracingAccelerationStructureSize SizeInfoAligned = RHICalcRayTracingSceneSize(NumNativeInstancesAligned, ERayTracingAccelerationStructureFlags::FastTrace);
	SizeInfo.ResultSize = FMath::Max(SizeInfo.ResultSize, SizeInfoAligned.ResultSize);
	SizeInfo.BuildScratchSize = FMath::Max(SizeInfo.BuildScratchSize, SizeInfoAligned.BuildScratchSize);

	check(SizeInfo.ResultSize < ~0u);

	// Allocate GPU buffer if current one is too small or significantly larger than what we need.
	if (!RayTracingSceneBuffer.IsValid() 
		|| SizeInfo.ResultSize > RayTracingSceneBuffer->GetSize() 
		|| SizeInfo.ResultSize < RayTracingSceneBuffer->GetSize() / 2)
	{
		RayTracingSceneSRV = nullptr;
		RayTracingSceneBuffer = nullptr;

		FRHIResourceCreateInfo CreateInfo(TEXT("RayTracingSceneBuffer"));
		RayTracingSceneBuffer = RHICreateBuffer(uint32(SizeInfo.ResultSize), BUF_AccelerationStructure, 0, ERHIAccess::BVHWrite, CreateInfo);
		RayTracingSceneSRV = RHICreateShaderResourceView(RayTracingSceneBuffer);
	}

	{
		const uint64 ScratchAlignment = GRHIRayTracingAccelerationStructureAlignment;
		FRDGBufferDesc ScratchBufferDesc;
		ScratchBufferDesc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::StructuredBuffer;
		ScratchBufferDesc.Usage = BUF_UnorderedAccess;
		ScratchBufferDesc.BytesPerElement = uint32(ScratchAlignment);
		ScratchBufferDesc.NumElements = uint32(FMath::DivideAndRoundUp(SizeInfo.BuildScratchSize, ScratchAlignment));

		BuildScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("RayTracingScratchBuffer"));
	}

	{
		FRDGBufferDesc InstanceBufferDesc;
		InstanceBufferDesc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::StructuredBuffer;
		InstanceBufferDesc.Usage = BUF_UnorderedAccess | BUF_ShaderResource;
		InstanceBufferDesc.BytesPerElement = GRHIRayTracingInstanceDescriptorSize;
		InstanceBufferDesc.NumElements = NumNativeInstancesAligned;

		InstanceBuffer = GraphBuilder.CreateBuffer(InstanceBufferDesc, TEXT("FRayTracingScene::InstanceBuffer"));
	}

	{
		// Round to PoT to avoid resizing too often
		const uint32 NumGeometries = FMath::RoundUpToPowerOfTwo(SceneInitializer.ReferencedGeometries.Num());
		const uint32 AccelerationStructureAddressesBufferSize = NumGeometries * sizeof(FRayTracingAccelerationStructureAddress);

		if (AccelerationStructureAddressesBuffer.NumBytes < AccelerationStructureAddressesBufferSize)
		{
			AccelerationStructureAddressesBuffer.Initialize(TEXT("RayTracingAccelerationStructureAddressesBuffer"), AccelerationStructureAddressesBufferSize, BUF_Volatile);
		}
	}

	{
		// Lock instance upload buffer (and resize if necessary)
		const uint32 UploadBufferSize = NumNativeInstancesAligned * sizeof(FRayTracingInstanceDescriptorInput);

		if (!InstanceUploadBuffer.IsValid()
			|| UploadBufferSize > InstanceUploadBuffer->GetSize()
			|| UploadBufferSize < InstanceUploadBuffer->GetSize() / 2)
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("RayTracingSceneInstanceUploadBuffer"));
			InstanceUploadBuffer = RHICreateStructuredBuffer(sizeof(FRayTracingInstanceDescriptorInput), UploadBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
			InstanceUploadSRV = RHICreateShaderResourceView(InstanceUploadBuffer);
		}
	}

	if (SceneInitializer.NumNativeInstances > 0)
	{
		const uint32 UploadBytes = SceneInitializer.NumNativeInstances * sizeof(FRayTracingInstanceDescriptorInput);

		FRayTracingInstanceDescriptorInput* InstanceUploadData = (FRayTracingInstanceDescriptorInput*)RHILockBuffer(InstanceUploadBuffer, 0, UploadBytes, RLM_WriteOnly);

		// Fill instance upload buffer on separate thread since results are only needed in RHI thread
		FillInstanceUploadBufferTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[InstanceUploadData,
			NumNativeInstances = SceneInitializer.NumNativeInstances,
			Instances = MakeArrayView(Instances),
			InstancesGeometryIndex = MoveTemp(InstancesGeometryIndex),
			RayTracingSceneRHI = RayTracingSceneRHI]()
		{
			FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);

			FillInstanceUploadBuffer(Instances, InstancesGeometryIndex, RayTracingSceneRHI, MakeArrayView(InstanceUploadData, NumNativeInstances));
		}, TStatId(), nullptr, ENamedThreads::AnyThread);

		FBuildInstanceBufferPassParams* PassParams = GraphBuilder.AllocParameters<FBuildInstanceBufferPassParams>();
		PassParams->InstanceBuffer = GraphBuilder.CreateUAV(InstanceBuffer);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("BuildTLASInstanceBuffer"),
			PassParams,
			ERDGPassFlags::Compute,
			[PassParams, this, &SceneInitializer](FRHICommandListImmediate& RHICmdList)
			{
				WaitForTasks();
				RHICmdList.UnlockBuffer(InstanceUploadBuffer);

				RHICmdList.EnqueueLambda([this, &SceneInitializer](FRHICommandListImmediate& RHICmdList)
					{
						QUICK_SCOPE_CYCLE_COUNTER(GetAccelerationStructuresAddresses);

						FRayTracingAccelerationStructureAddress* AddressesPtr = (FRayTracingAccelerationStructureAddress*)RHICmdList.LockBuffer(
							AccelerationStructureAddressesBuffer.Buffer, 
							0, 
							SceneInitializer.ReferencedGeometries.Num() * sizeof(FRayTracingAccelerationStructureAddress), RLM_WriteOnly);

						const uint32 NumGeometries = SceneInitializer.ReferencedGeometries.Num();
						for (uint32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
						{
							AddressesPtr[GeometryIndex] = SceneInitializer.ReferencedGeometries[GeometryIndex]->GetAccelerationStructureAddress(RHICmdList.GetGPUMask().ToIndex());
						}

						RHICmdList.UnlockBuffer(AccelerationStructureAddressesBuffer.Buffer);
					});

				::BuildRayTracingInstanceBuffer(
					RHICmdList,
					SceneInitializer.NumNativeInstances,
					PassParams->InstanceBuffer->GetRHI(),
					InstanceUploadSRV,
					AccelerationStructureAddressesBuffer.SRV);
			});
	}
}

void FRayTracingScene::WaitForTasks() const
{
	if (FillInstanceUploadBufferTask.IsValid())
	{
		QUICK_SCOPE_CYCLE_COUNTER(WaitForRayTracingSceneFillInstanceUploadBuffer);
		TRACE_CPUPROFILER_EVENT_SCOPE(WaitForRayTracingSceneFillInstanceUploadBuffer);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(FillInstanceUploadBufferTask, ENamedThreads::GetRenderThread_Local());

		FillInstanceUploadBufferTask = {};
	}
}

bool FRayTracingScene::IsCreated() const
{
	return RayTracingSceneRHI.IsValid();
}

FRHIRayTracingScene* FRayTracingScene::GetRHIRayTracingScene() const
{
	return RayTracingSceneRHI.GetReference();
}

FRHIRayTracingScene* FRayTracingScene::GetRHIRayTracingSceneChecked() const
{
	FRHIRayTracingScene* Result = GetRHIRayTracingScene();
	checkf(Result, TEXT("Ray tracing scene was not created. Perhaps BeginCreate() was not called."));
	return Result;
}

FRHIShaderResourceView* FRayTracingScene::GetShaderResourceViewChecked() const
{
	checkf(RayTracingSceneSRV.IsValid(), TEXT("Ray tracing scene SRV was not created. Perhaps BeginCreate() was not called."));
	return RayTracingSceneSRV.GetReference();
}

FRHIBuffer* FRayTracingScene::GetBufferChecked() const
{
	checkf(RayTracingSceneBuffer.IsValid(), TEXT("Ray tracing scene buffer was not created. Perhaps BeginCreate() was not called."));
	return RayTracingSceneBuffer.GetReference();
}

void FRayTracingScene::Reset()
{
	WaitForTasks();

	Instances.Reset();
	GeometriesToBuild.Reset();
	UsedCoarseMeshStreamingHandles.Reset();

	Allocator.Flush();

	BuildScratchBuffer = nullptr;
}

void FRayTracingScene::ResetAndReleaseResources()
{
	Reset();

	Instances.Empty();
	RayTracingSceneSRV = nullptr;
	RayTracingSceneBuffer = nullptr;
	RayTracingSceneRHI = nullptr;
}

#endif // RHI_RAYTRACING
