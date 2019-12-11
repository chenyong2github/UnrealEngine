// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraStats.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "NiagaraSortingGPU.h" // FNiagaraCopyIntBufferRegionCS

int32 GNiagaraMinGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarNiagaraMinGPUInstanceCount(
	TEXT("Niagara.MinGPUInstanceCount"),
	GNiagaraMinGPUInstanceCount,
	TEXT("Minimum number of instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

int32 GNiagaraMinGPUDrawIndirectArgs = 256;
static FAutoConsoleVariableRef CVarNiagaraMinGPUDrawIndirectArgs(
	TEXT("Niagara.MinGPUDrawIndirectArgs"),
	GNiagaraMinGPUDrawIndirectArgs,
	TEXT("Minimum number of draw indirect argsallocated in the global buffer. (default=256)"),
	ECVF_Default
);

DECLARE_DWORD_COUNTER_STAT(TEXT("Used GPU Instance Counters"), STAT_NiagaraUsedGPUInstanceCounters, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Max Num GPU Renderers"), STAT_NiagaraMaxNumGPURenderers, STATGROUP_Niagara);


//*****************************************************************************

FNiagaraGPUInstanceCountManager::FNiagaraGPUInstanceCountManager() 
{
	NumRegisteredGPURenderers = new FNiagaraGPURendererCount();
}

FNiagaraGPUInstanceCountManager::~FNiagaraGPUInstanceCountManager()
{
	ReleaseRHI();
}

void FNiagaraGPUInstanceCountManager::InitRHI()
{
}

void FNiagaraGPUInstanceCountManager::ReleaseRHI()
{
	CountBuffer.Release();
	DrawIndirectBuffer.Release();

	AllocatedInstanceCounts = 0;
	AllocatedDrawIndirectArgs = 0;

	if (CountReadback)
	{
		delete CountReadback;
		CountReadback = nullptr;
		CountReadbackSize = 0;
	}
}

uint32 FNiagaraGPUInstanceCountManager::AcquireEntry()
{
	checkSlow(IsInRenderingThread());

	if (FreeEntries.Num())
	{
		return FreeEntries.Pop();
	}
	else if (UsedInstanceCounts < AllocatedInstanceCounts)
	{
		// We can't reallocate on the fly, the buffer must be correctly resized before any tick gets scheduled.
		return UsedInstanceCounts++;
	}
	else
	{
		// @TODO : add realloc the buffer and copy the current content to it. Might require reallocating the readback in FNiagaraGPUInstanceCountManager::EnqueueGPUReadback()
		ensure(UsedInstanceCounts < AllocatedInstanceCounts);
		UE_LOG(LogNiagara, Error, TEXT("Niagara.MinGPUInstanceCount too small."));
		return INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::FreeEntry(uint32& BufferOffset)
{
	checkSlow(IsInRenderingThread());

	if (BufferOffset != INDEX_NONE)
	{
		// Add a reset to 0 task.
		// The entry will only become available/reusable after being reset to 0 in UpdateDrawIndirectBuffer()
		InstanceCountClearTasks.Add(BufferOffset);
		BufferOffset = INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::ResizeBuffers(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 ReservedInstanceCounts)
{
	const int32 RequiredInstanceCounts = UsedInstanceCounts + FMath::Max<int32>(ReservedInstanceCounts - FreeEntries.Num(), 0);
	const int32 MaxDrawIndirectArgs = NumRegisteredGPURenderers->Value;
	if (RequiredInstanceCounts > 0 || MaxDrawIndirectArgs > 0)
	{
		const int32 RecommendedInstanceCounts = FMath::Max(GNiagaraMinGPUInstanceCount, (int32)(RequiredInstanceCounts * BufferSlack));
		// If the buffer is not allocated, allocate it to the recommended size.
		if (!AllocatedInstanceCounts)
		{
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(AllocatedInstanceCounts);
			CountBuffer.Initialize(sizeof(uint32), AllocatedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static, TEXT("NiagaraGPUInstanceCounts"), &InitData);
		}
		// If we need to increase the buffer size to RecommendedInstanceCounts because the buffer is too small.
		else if (RequiredInstanceCounts > AllocatedInstanceCounts)
		{
			// Init a bigger buffer filled with 0.
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(RecommendedInstanceCounts);
			FRWBuffer NextCountBuffer;
			NextCountBuffer.Initialize(sizeof(uint32), RecommendedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static, TEXT("NiagaraGPUInstanceCounts"), &InitData);

			// Because the shader works with SINT, we need to temporarily create view for the shader to work. Those will be (deferred) deleted at end of the scope.
			FUnorderedAccessViewRHIRef NextCountBufferUAVAsInt = RHICreateUnorderedAccessView(NextCountBuffer.Buffer, EPixelFormat::PF_R32_SINT);
			FShaderResourceViewRHIRef CountBufferSRVAsInt = RHICreateShaderResourceView(CountBuffer.Buffer, sizeof(uint32), EPixelFormat::PF_R32_SINT);

			// Copy the current buffer in the next buffer.
			TShaderMapRef<FNiagaraCopyIntBufferRegionCS> CopyBufferCS(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(CopyBufferCS->GetComputeShader());
			FRHIUnorderedAccessView* UAVs[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};
			int32 UsedIndexCounts[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};
			UAVs[0] = NextCountBufferUAVAsInt;
			UsedIndexCounts[0] = AllocatedInstanceCounts;
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAVs, 1);
			CopyBufferCS->SetParameters(RHICmdList, CountBufferSRVAsInt, UAVs, UsedIndexCounts, 0, 1);
			DispatchComputeShader(RHICmdList, *CopyBufferCS, FMath::DivideAndRoundUp(AllocatedInstanceCounts, NIAGARA_COPY_BUFFER_THREAD_COUNT), 1, 1);
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, UAVs, 1);
			CopyBufferCS->UnbindBuffers(RHICmdList);

			// Swap the buffers
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			FMemory::Memswap(&NextCountBuffer, &CountBuffer, sizeof(NextCountBuffer));
		}
		// If we need to shrink the buffer size because use way to much buffer size.
		else if ((int32)(RecommendedInstanceCounts * BufferSlack) < AllocatedInstanceCounts)
		{
			// possibly shrink but hard to do because of sparse array allocation.
		}

		// We expect 2 niagara buffer per instance for simplicity
		int32 RecommendedDrawIndirectArgsCount = FMath::Max(GNiagaraMinGPUDrawIndirectArgs, (int32)(MaxDrawIndirectArgs * BufferSlack));
		if (MaxDrawIndirectArgs > AllocatedDrawIndirectArgs || (int32)(RecommendedDrawIndirectArgsCount * BufferSlack) < AllocatedDrawIndirectArgs)
		{
			DrawIndirectBuffer.Release();
			AllocatedDrawIndirectArgs = RecommendedDrawIndirectArgsCount;
			DrawIndirectBuffer.Initialize(sizeof(uint32), RecommendedDrawIndirectArgsCount * NIAGARA_DRAW_INDIRECT_ARGS_SIZE, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("NiagaraGPUDrawIndirectArgs"));
		}
	}
	else
	{
		ReleaseRHI();
	}

	INC_DWORD_STAT_BY(STAT_NiagaraUsedGPUInstanceCounters, RequiredInstanceCounts);
	INC_DWORD_STAT_BY(STAT_NiagaraMaxNumGPURenderers, MaxDrawIndirectArgs);
}

uint32 FNiagaraGPUInstanceCountManager::AddDrawIndirect(uint32 InstanceCountBufferOffset, uint32 NumIndicesPerInstance)
{
	checkSlow(IsInRenderingThread());

	const FArgGenTaskInfo Info(InstanceCountBufferOffset, NumIndicesPerInstance);

	uint32& CachedOffset = DrawIndirectArgMap.FindOrAdd(Info, INDEX_NONE);
	if (CachedOffset != INDEX_NONE)
	{
		return CachedOffset;
	}
	else if (DrawIndirectArgGenTasks.Num() < AllocatedDrawIndirectArgs)
	{
#if !UE_BUILD_SHIPPING
		const int32 MaxDrawIndirectArgs = NumRegisteredGPURenderers->Value;
		if (DrawIndirectArgGenTasks.Num() >= MaxDrawIndirectArgs)
		{
			UE_LOG(LogNiagara, Warning, TEXT("More draw indirect args then expected (%d / %d)"), DrawIndirectArgGenTasks.Num() + 1, MaxDrawIndirectArgs);
		}
#endif
		DrawIndirectArgGenTasks.Add(Info);
		CachedOffset = (DrawIndirectArgGenTasks.Num() - 1) * NIAGARA_DRAW_INDIRECT_ARGS_SIZE * sizeof(uint32);
		return CachedOffset;
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Too many draw indirect args. Some draw call will be corrupted"));
		return INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::UpdateDrawIndirectBuffer(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	if (DrawIndirectArgGenTasks.Num() || InstanceCountClearTasks.Num())
	{
		if (NiagaraSupportsComputeShaders(GShaderPlatformForFeatureLevel[FeatureLevel]))
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, CountBuffer.UAV);

			FReadBuffer TaskInfosBuffer;
			{
				// All draw indirect args task are run first because of the binding between the task ID and arg write offset.
				const uint32 ArgGenSize = DrawIndirectArgGenTasks.Num() * sizeof(FArgGenTaskInfo);
				const uint32 InstanceCountClearSize = InstanceCountClearTasks.Num() * sizeof(uint32);
				const uint32 TaskBufferSize = ArgGenSize + InstanceCountClearSize;
				TaskInfosBuffer.Initialize(sizeof(uint32), TaskBufferSize / sizeof(uint32), EPixelFormat::PF_R32_UINT, BUF_Volatile);
				uint8* TaskBufferData = (uint8*)RHILockVertexBuffer(TaskInfosBuffer.Buffer, 0, TaskBufferSize, RLM_WriteOnly);
				FMemory::Memcpy(TaskBufferData, DrawIndirectArgGenTasks.GetData(), ArgGenSize);
				FMemory::Memcpy(TaskBufferData + ArgGenSize, InstanceCountClearTasks.GetData(), InstanceCountClearSize);
				RHIUnlockVertexBuffer(TaskInfosBuffer.Buffer);
			}

			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, DrawIndirectBuffer.UAV);

			FNiagaraDrawIndirectArgsGenCS::FPermutationDomain PermutationVector;
			TShaderMapRef<FNiagaraDrawIndirectArgsGenCS> DrawIndirectArgsGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			RHICmdList.SetComputeShader(DrawIndirectArgsGenCS->GetComputeShader());
			DrawIndirectArgsGenCS->SetOutput(RHICmdList, DrawIndirectBuffer.UAV, CountBuffer.UAV);
			DrawIndirectArgsGenCS->SetParameters(RHICmdList, TaskInfosBuffer.SRV, DrawIndirectArgGenTasks.Num(), InstanceCountClearTasks.Num());
			DispatchComputeShader(RHICmdList, *DrawIndirectArgsGenCS, FMath::DivideAndRoundUp(DrawIndirectArgGenTasks.Num() + InstanceCountClearTasks.Num(), NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
			DrawIndirectArgsGenCS->UnbindBuffers(RHICmdList);

			// Sync after clear.
			RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, CountBuffer.UAV);
			// Transition draw indirect to readable for gfx draw indirect.
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DrawIndirectBuffer.UAV);
		}
		// Once cleared to 0, the count are reusable.
		FreeEntries.Append(InstanceCountClearTasks);

		DrawIndirectArgGenTasks.Empty();
		DrawIndirectArgMap.Empty();
		InstanceCountClearTasks.Empty();
	}
}

const uint32* FNiagaraGPUInstanceCountManager::GetGPUReadback() 
{
	if (CountReadback && CountReadbackSize && CountReadback->IsReady())
	{
		return (uint32*)(CountReadback->Lock(CountReadbackSize * sizeof(uint32)));
	}
	else
	{
		return nullptr; 
	}
}

void FNiagaraGPUInstanceCountManager::ReleaseGPUReadback() 
{
	check(CountReadback && CountReadbackSize);
	CountReadback->Unlock();
	// Readback can only ever be done once, to prevent misusage with index lifetime
	CountReadbackSize = 0; 
}

void FNiagaraGPUInstanceCountManager::EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList)
{
	if (UsedInstanceCounts)
	{
		if (!CountReadback)
		{
			CountReadback = new FRHIGPUBufferReadback(TEXT("Niagara GPU Instance Count Readback"));
		}
		CountReadbackSize = UsedInstanceCounts;
		CountReadback->EnqueueCopy(RHICmdList, CountBuffer.Buffer);
	}
}

bool FNiagaraGPUInstanceCountManager::HasPendingGPUReadback() const
{ 
	return CountReadback && CountReadbackSize;
}

