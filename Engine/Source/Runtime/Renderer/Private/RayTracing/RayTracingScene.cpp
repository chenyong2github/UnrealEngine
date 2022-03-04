// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingScene.h"

#if RHI_RAYTRACING

#include "RayTracingInstanceBufferUtil.h"
#include "RenderCore.h"
#include "RayTracingDefinitions.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

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

void FRayTracingScene::Create(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene, const FViewMatrices& ViewMatrices)
{
	QUICK_SCOPE_CYCLE_COUNTER(FRayTracingScene_BeginCreate);

	WaitForTasks();

	FRayTracingSceneWithGeometryInstances SceneWithGeometryInstances = CreateRayTracingSceneWithGeometryInstances(
		Instances,
		RAY_TRACING_NUM_SHADER_SLOTS,
		RAY_TRACING_NUM_MISS_SHADER_SLOTS);

	RayTracingSceneRHI = SceneWithGeometryInstances.Scene;

	const FRayTracingSceneInitializer2& SceneInitializer = RayTracingSceneRHI->GetInitializer();

	// Round up number of instances to some multiple to avoid pathological growth reallocations.
	static constexpr uint32 AllocationGranularity = 8 * 1024;
	const uint32 NumNativeInstancesAligned = FMath::DivideAndRoundUp(FMath::Max(SceneInitializer.NumNativeInstances, 1U), AllocationGranularity) * AllocationGranularity;
	const uint32 NumTransformsAligned = FMath::DivideAndRoundUp(FMath::Max(SceneWithGeometryInstances.NumNativeCPUInstances, 1U), AllocationGranularity) * AllocationGranularity;

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

		FRHIResourceCreateInfo CreateInfo(TEXT("FRayTracingScene::SceneBuffer"));
		RayTracingSceneBuffer = RHICreateBuffer(uint32(SizeInfo.ResultSize), BUF_AccelerationStructure, 0, ERHIAccess::BVHWrite, CreateInfo);
		RayTracingSceneSRV = RHICreateShaderResourceView(RayTracingSceneBuffer);
	}

	{
		const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
		FRDGBufferDesc ScratchBufferDesc;
		ScratchBufferDesc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::StructuredBuffer;
		ScratchBufferDesc.Usage = BUF_UnorderedAccess;
		ScratchBufferDesc.BytesPerElement = uint32(ScratchAlignment);
		ScratchBufferDesc.NumElements = uint32(FMath::DivideAndRoundUp(SizeInfo.BuildScratchSize, ScratchAlignment));

		BuildScratchBuffer = GraphBuilder.CreateBuffer(ScratchBufferDesc, TEXT("FRayTracingScene::ScratchBuffer"));
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
			// Need to pass "BUF_MultiGPUAllocate", as virtual addresses are different per GPU
			AccelerationStructureAddressesBuffer.Initialize(
				TEXT("FRayTracingScene::AccelerationStructureAddressesBuffer"), AccelerationStructureAddressesBufferSize, BUF_Volatile | BUF_MultiGPUAllocate);
		}
	}

	{
		// Create/resize instance upload buffer (if necessary)
		const uint32 UploadBufferSize = NumNativeInstancesAligned * sizeof(FRayTracingInstanceDescriptorInput);

		if (!InstanceUploadBuffer.IsValid()
			|| UploadBufferSize > InstanceUploadBuffer->GetSize()
			|| UploadBufferSize < InstanceUploadBuffer->GetSize() / 2)
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FRayTracingScene::InstanceUploadBuffer"));
			InstanceUploadBuffer = RHICreateStructuredBuffer(sizeof(FRayTracingInstanceDescriptorInput), UploadBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
			InstanceUploadSRV = RHICreateShaderResourceView(InstanceUploadBuffer);
		}
	}

	{
		// Create/resize transform upload buffer (if necessary)
		const uint32 UploadBufferSize = NumTransformsAligned * sizeof(FVector4f) * 3;

		if (!TransformUploadBuffer.IsValid()
			|| UploadBufferSize > TransformUploadBuffer->GetSize()
			|| UploadBufferSize < TransformUploadBuffer->GetSize() / 2)
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FRayTracingScene::TransformUploadBuffer"));
			TransformUploadBuffer = RHICreateStructuredBuffer(sizeof(FVector4f), UploadBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
			TransformUploadSRV = RHICreateShaderResourceView(TransformUploadBuffer);
		}
	}

	if (SceneInitializer.NumNativeInstances > 0)
	{
		const uint32 InstanceUploadBytes = SceneInitializer.NumNativeInstances * sizeof(FRayTracingInstanceDescriptorInput);
		const uint32 TransformUploadBytes = SceneWithGeometryInstances.NumNativeCPUInstances * 3 * sizeof(FVector4f);

		FRayTracingInstanceDescriptorInput* InstanceUploadData = (FRayTracingInstanceDescriptorInput*)RHILockBuffer(InstanceUploadBuffer, 0, InstanceUploadBytes, RLM_WriteOnly);
		FVector4f* TransformUploadData = (FVector4f*)RHILockBuffer(TransformUploadBuffer, 0, TransformUploadBytes, RLM_WriteOnly);

		// Fill instance upload buffer on separate thread since results are only needed in RHI thread
		FillInstanceUploadBufferTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[InstanceUploadData = MakeArrayView(InstanceUploadData, SceneInitializer.NumNativeInstances),
			TransformUploadData = MakeArrayView(TransformUploadData, SceneWithGeometryInstances.NumNativeCPUInstances * 3),
			NumNativeGPUSceneInstances = SceneWithGeometryInstances.NumNativeGPUSceneInstances,
			NumNativeCPUInstances = SceneWithGeometryInstances.NumNativeCPUInstances,
			Instances = MakeArrayView(Instances),
			InstanceGeometryIndices = MoveTemp(SceneWithGeometryInstances.InstanceGeometryIndices),
			BaseUploadBufferOffsets = MoveTemp(SceneWithGeometryInstances.BaseUploadBufferOffsets),
			RayTracingSceneRHI = RayTracingSceneRHI,
			PreViewTranslation = ViewMatrices.GetPreViewTranslation()]()
		{
			FTaskTagScope TaskTagScope(ETaskTag::EParallelRenderingThread);
			FillRayTracingInstanceUploadBuffer(
				RayTracingSceneRHI,
				PreViewTranslation,
				Instances,
				InstanceGeometryIndices,
				BaseUploadBufferOffsets,
				NumNativeGPUSceneInstances,
				NumNativeCPUInstances,
				InstanceUploadData,
				TransformUploadData);
		}, TStatId(), nullptr, ENamedThreads::AnyThread);

		FBuildInstanceBufferPassParams* PassParams = GraphBuilder.AllocParameters<FBuildInstanceBufferPassParams>();
		PassParams->InstanceBuffer = GraphBuilder.CreateUAV(InstanceBuffer);

		const FLargeWorldRenderPosition AbsoluteViewOrigin(ViewMatrices.GetViewOrigin());
		const FVector ViewTileOffset = AbsoluteViewOrigin.GetTileOffset();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("BuildTLASInstanceBuffer"),
			PassParams,
			ERDGPassFlags::Compute,
			[PassParams,
			this,
			GPUScene = &GPUScene,
			ViewTilePosition = AbsoluteViewOrigin.GetTile(),
			RelativePreViewTranslation = ViewMatrices.GetPreViewTranslation() + ViewTileOffset,
			&SceneInitializer,
			NumNativeGPUSceneInstances = SceneWithGeometryInstances.NumNativeGPUSceneInstances,
			NumNativeCPUInstances = SceneWithGeometryInstances.NumNativeCPUInstances,
			GPUInstances = MoveTemp(SceneWithGeometryInstances.GPUInstances)
			](FRHICommandListImmediate& RHICmdList)
			{
				WaitForTasks();
				RHICmdList.UnlockBuffer(InstanceUploadBuffer);
				RHICmdList.UnlockBuffer(TransformUploadBuffer);

				// Pull this out here, because command list playback (where the lambda is executed) doesn't update the GPU mask
				FRHIGPUMask IterateGPUMasks = RHICmdList.GetGPUMask();

				RHICmdList.EnqueueLambda([BufferRHIRef = AccelerationStructureAddressesBuffer.Buffer, &SceneInitializer, IterateGPUMasks](FRHICommandListImmediate& RHICmdList)
					{
						QUICK_SCOPE_CYCLE_COUNTER(GetAccelerationStructuresAddresses);

						for (uint32 GPUIndex : IterateGPUMasks)
						{
							FRayTracingAccelerationStructureAddress* AddressesPtr = (FRayTracingAccelerationStructureAddress*)RHICmdList.LockBufferMGPU(
								BufferRHIRef,
								GPUIndex,
								0,
								SceneInitializer.ReferencedGeometries.Num() * sizeof(FRayTracingAccelerationStructureAddress), RLM_WriteOnly);

							const uint32 NumGeometries = SceneInitializer.ReferencedGeometries.Num();
							for (uint32 GeometryIndex = 0; GeometryIndex < NumGeometries; ++GeometryIndex)
							{
								AddressesPtr[GeometryIndex] = SceneInitializer.ReferencedGeometries[GeometryIndex]->GetAccelerationStructureAddress(GPUIndex);
							}

							RHICmdList.UnlockBufferMGPU(BufferRHIRef, GPUIndex);
						}
					});

				BuildRayTracingInstanceBuffer(
					RHICmdList,
					GPUScene,
					ViewTilePosition,
					FVector3f(RelativePreViewTranslation),
					PassParams->InstanceBuffer->GetRHI(),
					InstanceUploadSRV,
					AccelerationStructureAddressesBuffer.SRV,
					TransformUploadSRV,
					NumNativeGPUSceneInstances,
					NumNativeCPUInstances,
					GPUInstances);
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
