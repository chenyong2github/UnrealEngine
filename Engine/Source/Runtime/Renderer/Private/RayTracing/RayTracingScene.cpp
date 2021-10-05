// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingScene.h"

#if RHI_RAYTRACING

#include "RenderCore.h"
#include "RayTracingDefinitions.h"
#include "RenderGraphBuilder.h"
#include "Experimental/Containers/SherwoodHashTable.h"

FRayTracingScene::FRayTracingScene()
{

}

FRayTracingScene::~FRayTracingScene()
{
	WaitForTasks();
}

FGraphEventRef FRayTracingScene::BeginCreate(FRDGBuilder& GraphBuilder)
{
	WaitForTasks();

	NumNativeInstances = 0;
	NumTotalSegments = 0;

	const uint32 NumSceneInstances = Instances.Num();

	FRayTracingSceneInitializer2 SceneInitializer;
	SceneInitializer.DebugName = FName(TEXT("FRayTracingScene"));
	SceneInitializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
	SceneInitializer.NumMissShaderSlots = RAY_TRACING_NUM_MISS_SHADER_SLOTS;
	SceneInitializer.Instances = Instances;
	SceneInitializer.PerInstanceGeometries.SetNumUninitialized(NumSceneInstances);
	SceneInitializer.PerInstanceNumTransforms.SetNumUninitialized(NumSceneInstances);
	SceneInitializer.BaseInstancePrefixSum.SetNumUninitialized(NumSceneInstances);
	SceneInitializer.SegmentPrefixSum.SetNumUninitialized(NumSceneInstances);

	Experimental::TSherwoodSet<FRHIRayTracingGeometry*> UniqueGeometries;

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
		SceneInitializer.SegmentPrefixSum[InstanceIndex] = NumTotalSegments;
		NumTotalSegments += InstanceDesc.GeometryRHI->GetNumSegments();

		bool bIsAlreadyInSet = false;
		UniqueGeometries.Add(InstanceDesc.GeometryRHI, &bIsAlreadyInSet);
		if (!bIsAlreadyInSet)
		{
			SceneInitializer.ReferencedGeometries.Add(InstanceDesc.GeometryRHI);
		}

		SceneInitializer.BaseInstancePrefixSum[InstanceIndex] = NumNativeInstances;
		NumNativeInstances += InstanceDesc.NumTransforms;

		SceneInitializer.PerInstanceNumTransforms[InstanceIndex] = InstanceDesc.NumTransforms;
	}

	SceneInitializer.NumNativeInstances = NumNativeInstances;
	SceneInitializer.NumTotalSegments = NumTotalSegments;

	// Round up number of instances to some multiple to avoid pathological growth reallocations.
	static constexpr uint32 AllocationGranularity = 8 * 1024;
	uint32 NumNativeInstancesAligned = FMath::DivideAndRoundUp(NumNativeInstances, AllocationGranularity) * AllocationGranularity;

	SizeInfo = RHICalcRayTracingSceneSize(NumNativeInstances, ERayTracingAccelerationStructureFlags::FastTrace);
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

	RayTracingSceneRHI.SafeRelease();

	CreateRayTracingSceneTask = FFunctionGraphTask::CreateAndDispatchWhenReady([
			&ResultScene = RayTracingSceneRHI,
			SceneInitializer = MoveTemp(SceneInitializer)]() mutable
		{
			FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);

			ResultScene = RHICreateRayTracingScene(MoveTemp(SceneInitializer));

		}, TStatId(), nullptr, ENamedThreads::AnyThread);

	const uint64 ScratchAlignment = GRHIRayTracingAccelerationStructureAlignment;
	FRDGBufferDesc ScratchBufferDesc;
	ScratchBufferDesc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::StructuredBuffer;
	ScratchBufferDesc.Usage = BUF_UnorderedAccess;
	ScratchBufferDesc.BytesPerElement = uint32(ScratchAlignment);
	ScratchBufferDesc.NumElements = uint32(FMath::DivideAndRoundUp(SizeInfo.BuildScratchSize, ScratchAlignment));

	BuildScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("FRayTracingScene::BuildScratchBuffer"));

	return CreateRayTracingSceneTask;
}

void FRayTracingScene::WaitForTasks() const
{
	if (CreateRayTracingSceneTask.IsValid())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WaitForCreateRayTracingScene);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(CreateRayTracingSceneTask, ENamedThreads::GetRenderThread_Local());

		CreateRayTracingSceneTask = {};
	}
}

bool FRayTracingScene::IsCreated() const
{
	// Either scene creation task has been started (CreateRayTracingSceneTask != null) or we already have a valid RayTracingSceneRHI.
	return CreateRayTracingSceneTask.IsValid() || RayTracingSceneRHI.IsValid();
}

FRHIRayTracingScene* FRayTracingScene::GetRHIRayTracingScene() const
{
	WaitForTasks();

	checkf(!CreateRayTracingSceneTask.IsValid() || CreateRayTracingSceneTask->IsComplete(),
		TEXT("Ray tracing scene creation task is expected to be waited upon and ready when we get here."));

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
