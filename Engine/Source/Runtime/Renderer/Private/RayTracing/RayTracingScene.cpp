// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingScene.h"

#if RHI_RAYTRACING

#include "RenderCore.h"
#include "RayTracingDefinitions.h"

FRayTracingScene::FRayTracingScene()
{

}

FRayTracingScene::~FRayTracingScene()
{
	WaitForTasks();
}

FGraphEventRef FRayTracingScene::BeginCreate()
{
	WaitForTasks();

	uint32 NumTotalInstances = 0;
	for (const FRayTracingGeometryInstance& Instance : Instances)
	{
		ensure(Instance.NumTransforms >= uint32(Instance.GetTransforms().Num()));
		NumTotalInstances += Instance.NumTransforms;
	}
	SET_DWORD_STAT(STAT_RayTracingInstances, NumTotalInstances);

	// Round up number of instances to some multiple to avoid pathological growth reallocations.
	static constexpr uint32 AllocationGranularity = 8 * 1024;
	NumTotalInstances = FMath::DivideAndRoundUp(NumTotalInstances, AllocationGranularity) * AllocationGranularity;

	// #yuriy_todo: allocate these using RDG transient resources!
	FRayTracingAccelerationStructureSize SizeInfo = RHICalcRayTracingSceneSize(NumTotalInstances, ERayTracingAccelerationStructureFlags::FastTrace);
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

	CreateRayTracingSceneTask = FFunctionGraphTask::CreateAndDispatchWhenReady([
			&ResultScene = RayTracingSceneRHI,
			Instances = MakeArrayView(Instances)]()
		{
			FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);

			FRayTracingSceneInitializer SceneInitializer;
			SceneInitializer.DebugName = FName(TEXT("FRayTracingScene"));
			SceneInitializer.Instances = Instances;
			SceneInitializer.ShaderSlotsPerGeometrySegment = RAY_TRACING_NUM_SHADER_SLOTS;
			SceneInitializer.NumMissShaderSlots = RAY_TRACING_NUM_MISS_SHADER_SLOTS;

			ResultScene = RHICreateRayTracingScene(SceneInitializer);

		}, TStatId(), nullptr, ENamedThreads::AnyThread);

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
#ifdef DO_CHECK
	GeometriesToValidate.Reset();
#endif
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
