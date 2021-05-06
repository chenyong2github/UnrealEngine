// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraStats.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "GPUSortManager.h" // CopyUIntBufferToTargets
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "NiagaraRenderer.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "ClearQuad.h"


int32 GNiagaraMinGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarNiagaraMinGPUInstanceCount(
	TEXT("Niagara.MinGPUInstanceCount"),
	GNiagaraMinGPUInstanceCount,
	TEXT("Minimum number of instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

int32 GNiagaraMinCulledGPUInstanceCount = 2048;
static FAutoConsoleVariableRef CVarNiagaraMinCulledGPUInstanceCount(
	TEXT("Niagara.MinCulledGPUInstanceCount"),
	GNiagaraMinCulledGPUInstanceCount,
	TEXT("Minimum number of culled (per-view) instance count entries allocated in the global buffer. (default=2048)"),
	ECVF_Default
);

float GNiagaraGPUCountBufferSlack = 1.5f;
static FAutoConsoleVariableRef CVarNiagaraGPUCountBufferSlack(
	TEXT("Niagara.GPUCountBufferSlack"),
	GNiagaraGPUCountBufferSlack,
	TEXT("Multiplier of the GPU count buffer size to prevent frequent re-allocation."),
	ECVF_Default
);

int32 GNiagaraIndirectArgsPoolMinSize = 256;
static FAutoConsoleVariableRef CVarNiagaraIndirectArgsPoolMinSize(
	TEXT("fx.Niagara.IndirectArgsPool.MinSize"),
	GNiagaraIndirectArgsPoolMinSize,
	TEXT("Minimum number of draw indirect args allocated into the pool. (default=256)"),
	ECVF_Default
);

float GNiagaraIndirectArgsPoolBlockSizeFactor = 2.0f;
static FAutoConsoleVariableRef CNiagaraIndirectArgsPoolBlockSizeFactor(
	TEXT("fx.Niagara.IndirectArgsPool.BlockSizeFactor"),
	GNiagaraIndirectArgsPoolBlockSizeFactor,
	TEXT("Multiplier on the indirect args pool size when needing to increase it from running out of space. (default=2.0)"),
	ECVF_Default
);

int32 GNiagaraIndirectArgsPoolAllowShrinking = 1;
static FAutoConsoleVariableRef CVarNiagaraIndirectArgsPoolAllowShrinking(
	TEXT("fx.Niagara.IndirectArgsPool.AllowShrinking"),
	GNiagaraIndirectArgsPoolAllowShrinking,
	TEXT("Allow the indirect args pool to shrink after a number of frames below a low water mark."),
	ECVF_Default
);

float GNiagaraIndirectArgsPoolLowWaterAmount = 0.5f;
static FAutoConsoleVariableRef CVarNiagaraIndirectArgsPoolLowWaterAmount(
	TEXT("fx.Niagara.IndirectArgsPool.LowWaterAmount"),
	GNiagaraIndirectArgsPoolLowWaterAmount,
	TEXT("Percentage (0-1) of the indirect args pool that is considered low and worthy of shrinking"),
	ECVF_Default
);

int32 GNiagaraIndirectArgsPoolLowWaterFrames = 150;
static FAutoConsoleVariableRef CVarNiagaraIndirectArgsPoolLowWaterFrames(
	TEXT("fx.Niagara.IndirectArgsPool.LowWaterFrames"),
	GNiagaraIndirectArgsPoolLowWaterFrames,
	TEXT("The number of frames to wait to shrink the indirect args pool for being below the low water mark. (default=150)"),
	ECVF_Default
);

DECLARE_DWORD_COUNTER_STAT(TEXT("Used GPU Instance Counters"), STAT_NiagaraUsedGPUInstanceCounters, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Indirect Draw Calls"), STAT_NiagaraIndirectDraws, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Readback Lock"), STAT_NiagaraGPUReadbackLock, STATGROUP_Niagara);

#ifndef ENABLE_NIAGARA_INDIRECT_ARG_POOL_LOG
#define ENABLE_NIAGARA_INDIRECT_ARG_POOL_LOG 0
#endif

#if ENABLE_NIAGARA_INDIRECT_ARG_POOL_LOG
#define INDIRECT_ARG_POOL_LOG(Format, ...) UE_LOG(LogNiagara, Log, TEXT("NIAGARA INDIRECT ARG POOL: ") TEXT(Format), __VA_ARGS__)
#else
#define INDIRECT_ARG_POOL_LOG(Format, ...) do {} while(0)
#endif

//*****************************************************************************

const ERHIAccess FNiagaraGPUInstanceCountManager::kCountBufferDefaultState = ERHIAccess::SRVMask | ERHIAccess::CopySrc;

FNiagaraGPUInstanceCountManager::FNiagaraGPUInstanceCountManager()
{
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
	ReleaseCounts();

	for (auto& PoolEntry : DrawIndirectPool)
	{
		PoolEntry->Buffer.Release();
	}
	DrawIndirectPool.Empty();
}

void FNiagaraGPUInstanceCountManager::ReleaseCounts()
{
	CountBuffer.Release();
	CulledCountBuffer.Release();

	AllocatedInstanceCounts = 0;
	AllocatedCulledCounts = 0;

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
		//UE_LOG(LogNiagara, Error, TEXT("Niagara.MinGPUInstanceCount too small. UsedInstanceCounts: %d < AllocatedInstanceCounts: %d"), UsedInstanceCounts, AllocatedInstanceCounts);
		return INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::FreeEntry(uint32& BufferOffset)
{
	checkSlow(IsInRenderingThread());

	if (BufferOffset != INDEX_NONE)
	{
		checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
		checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);

		//UE_LOG(LogNiagara, Warning, TEXT("FNiagaraGPUInstanceCountManager::FreeEntry %d"), BufferOffset);
		// Add a reset to 0 task.
		// The entry will only become available/reusable after being reset to 0 in UpdateDrawIndirectBuffer()
		//if (InstanceCountClearTasks.Find(BufferOffset) != -1)
		//{
		//	UE_LOG(LogNiagara, Warning, TEXT("FNiagaraGPUInstanceCountManager::FreeEntry DUPLICATED!!!!!!!!!!!!!! %d"), BufferOffset);
		//}
		InstanceCountClearTasks.Add(BufferOffset);
		BufferOffset = INDEX_NONE;
	}
}

void FNiagaraGPUInstanceCountManager::FreeEntryArray(TConstArrayView<uint32> EntryArray)
{
	checkSlow(IsInRenderingThread());

	const int32 NumToFree = EntryArray.Num();
	if (NumToFree > 0)
	{
#if DO_CHECK
		for (uint32 BufferOffset : EntryArray)
		{
			checkf(!FreeEntries.Contains(BufferOffset), TEXT("BufferOffset %u exists in FreeEntries"), BufferOffset);
			checkf(!InstanceCountClearTasks.Contains(BufferOffset), TEXT("BufferOffset %u exists in InstanceCountClearTasks"), BufferOffset);
		}
#endif
		InstanceCountClearTasks.Append(EntryArray.GetData(), NumToFree);
	}
}

FRWBuffer* FNiagaraGPUInstanceCountManager::AcquireCulledCountsBuffer(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	if (RequiredCulledCounts > 0)
	{
		if (!bAcquiredCulledCounts)
		{
			const int32 RecommendedCulledCounts = FMath::Max(GNiagaraMinCulledGPUInstanceCount, (int32)(RequiredCulledCounts * GNiagaraGPUCountBufferSlack));
			ERHIAccess BeforeState = ERHIAccess::SRVCompute;
			if (RecommendedCulledCounts > AllocatedCulledCounts)
			{
				// We need a bigger buffer
				CulledCountBuffer.Release();

				AllocatedCulledCounts = RecommendedCulledCounts;
				CulledCountBuffer.Initialize(sizeof(uint32), AllocatedCulledCounts, EPixelFormat::PF_R32_UINT, BUF_Transient, TEXT("NiagaraCulledGPUInstanceCounts"));
				BeforeState = ERHIAccess::Unknown;
			}

			CulledCountBuffer.AcquireTransientResource();

			// Initialize the buffer by clearing it to zero then transition it to be ready to write to
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, BeforeState, ERHIAccess::UAVCompute));
			RHICmdList.ClearUAVUint(CulledCountBuffer.UAV, FUintVector4(EForceInit::ForceInitToZero));
			RHICmdList.Transition(FRHITransitionInfo(CulledCountBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

			bAcquiredCulledCounts = true;
		}

		return &CulledCountBuffer;
	}

	return nullptr;
}

void FNiagaraGPUInstanceCountManager::ResizeBuffers(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 ReservedInstanceCounts)
{
	const int32 RequiredInstanceCounts = UsedInstanceCounts + FMath::Max<int32>(ReservedInstanceCounts - FreeEntries.Num(), 0);
	if (RequiredInstanceCounts > 0)
	{
		const int32 RecommendedInstanceCounts = FMath::Max(GNiagaraMinGPUInstanceCount, (int32)(RequiredInstanceCounts * GNiagaraGPUCountBufferSlack));
		// If the buffer is not allocated, allocate it to the recommended size.
		if (!AllocatedInstanceCounts)
		{
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(AllocatedInstanceCounts);
			CountBuffer.Initialize(sizeof(uint32), AllocatedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_SourceCopy, TEXT("NiagaraGPUInstanceCounts"), &InitData);
			// NiagaraEmitterInstanceBatcher expects the count buffer to be readable and copyable before running the sim.
			RHICmdList.Transition(FRHITransitionInfo(CountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));
			//UE_LOG(LogNiagara, Log, TEXT("FNiagaraGPUInstanceCountManager::ResizeBuffers Alloc AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to increase the buffer size to RecommendedInstanceCounts because the buffer is too small.
		else if (RequiredInstanceCounts > AllocatedInstanceCounts)
		{
			SCOPED_DRAW_EVENT(RHICmdList, ResizeNiagaraGPUCounts);

			// Init a bigger buffer filled with 0.
			TResourceArray<uint32> InitData;
			InitData.AddZeroed(RecommendedInstanceCounts);
			FRWBuffer NextCountBuffer;
			NextCountBuffer.Initialize(sizeof(uint32), RecommendedInstanceCounts, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_SourceCopy, TEXT("NiagaraGPUInstanceCounts"), &InitData);

			// Copy the current buffer in the next buffer. We don't need to transition any of the buffers, because the current buffer is transitioned to readable after
			// the simulation, and the new buffer is created in the UAVCompute state.
			FRHIUnorderedAccessView* UAVs[] = { NextCountBuffer.UAV };
			int32 UsedIndexCounts[] = { AllocatedInstanceCounts };
			CopyUIntBufferToTargets(RHICmdList, FeatureLevel, CountBuffer.SRV, UAVs, UsedIndexCounts, 0, UE_ARRAY_COUNT(UAVs));

			// NiagaraEmitterInstanceBatcher expects the count buffer to be readable and copyable before running the sim.
			RHICmdList.Transition(FRHITransitionInfo(NextCountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState));

			// Swap the buffers
			AllocatedInstanceCounts = RecommendedInstanceCounts;
			FMemory::Memswap(&NextCountBuffer, &CountBuffer, sizeof(NextCountBuffer));
			//UE_LOG(LogNiagara, Log, TEXT("FNiagaraGPUInstanceCountManager::ResizeBuffers Resize AllocatedInstanceCounts: %d ReservedInstanceCounts: %d"), AllocatedInstanceCounts, ReservedInstanceCounts);
		}
		// If we need to shrink the buffer size because use way to much buffer size.
		else if ((int32)(RecommendedInstanceCounts * GNiagaraGPUCountBufferSlack) < AllocatedInstanceCounts)
		{
			// possibly shrink but hard to do because of sparse array allocation.
		}
	}
	else
	{
		ReleaseCounts();
	}

	INC_DWORD_STAT_BY(STAT_NiagaraUsedGPUInstanceCounters, RequiredInstanceCounts);
}

void FNiagaraGPUInstanceCountManager::FlushIndirectArgsPool()
{
	// Cull indirect draw pool entries so that we only keep the last pool
	while (DrawIndirectPool.Num() > 1)
	{
		FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[0];
		PoolEntry->Buffer.Release();

		DrawIndirectPool.RemoveAt(0, 1, false);
	}

	// If shrinking is allowed and we've been under the low water mark
	if (GNiagaraIndirectArgsPoolAllowShrinking && DrawIndirectPool.Num() > 0 && DrawIndirectLowWaterFrames >= uint32(GNiagaraIndirectArgsPoolLowWaterFrames))
	{
		FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[0];
		const uint32 NewSize = FMath::Max<uint32>(GNiagaraIndirectArgsPoolMinSize, PoolEntry->NumAllocated / GNiagaraIndirectArgsPoolBlockSizeFactor);

		INDIRECT_ARG_POOL_LOG("Shrinking pool from size %d to %d", PoolEntry->NumAllocated, NewSize);

		PoolEntry->Buffer.Release();
		PoolEntry->NumAllocated = NewSize;

		TResourceArray<uint32> InitData;
		InitData.AddZeroed(PoolEntry->NumAllocated * NIAGARA_DRAW_INDIRECT_ARGS_SIZE);
		PoolEntry->Buffer.Initialize(sizeof(uint32), PoolEntry->NumAllocated * NIAGARA_DRAW_INDIRECT_ARGS_SIZE, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("NiagaraGPUDrawIndirectArgs"), &InitData);

		// Reset the timer
		DrawIndirectLowWaterFrames = 0;
	}
}

FNiagaraGPUInstanceCountManager::FIndirectArgSlot FNiagaraGPUInstanceCountManager::AddDrawIndirect(uint32 InstanceCountBufferOffset, uint32 NumIndicesPerInstance,
	uint32 StartIndexLocation, bool bIsInstancedStereoEnabled, bool bCulled)
{
	checkSlow(IsInRenderingThread());

	const FArgGenTaskInfo Info(InstanceCountBufferOffset, NumIndicesPerInstance, StartIndexLocation, bIsInstancedStereoEnabled, bCulled);

	static const FArgGenSlotInfo InvalidSlot(INDEX_NONE, INDEX_NONE);
	FArgGenSlotInfo& CachedSlot = DrawIndirectArgMap.FindOrAdd(Info, InvalidSlot);
	if (CachedSlot == InvalidSlot)
	{
		// Attempt to allocate a new slot from the pool, or add to the pool if it's full
		FIndirectArgsPoolEntry* PoolEntry = DrawIndirectPool.Num() > 0 ? DrawIndirectPool.Last().Get() : nullptr;
		if (PoolEntry == nullptr || PoolEntry->NumUsed >= PoolEntry->NumAllocated)
		{
			FIndirectArgsPoolEntryPtr NewEntry = MakeUnique<FIndirectArgsPoolEntry>();
			NewEntry->NumAllocated = PoolEntry ? uint32(PoolEntry->NumAllocated * GNiagaraIndirectArgsPoolBlockSizeFactor) : uint32(GNiagaraIndirectArgsPoolMinSize);

			INDIRECT_ARG_POOL_LOG("Increasing pool from size %d to %d", PoolEntry ? PoolEntry->NumAllocated : 0, NewEntry->NumAllocated);

			TResourceArray<uint32> InitData;
			InitData.AddZeroed(NewEntry->NumAllocated * NIAGARA_DRAW_INDIRECT_ARGS_SIZE);
			NewEntry->Buffer.Initialize(sizeof(uint32), NewEntry->NumAllocated * NIAGARA_DRAW_INDIRECT_ARGS_SIZE, EPixelFormat::PF_R32_UINT, BUF_Static | BUF_DrawIndirect, TEXT("NiagaraGPUDrawIndirectArgs"), &InitData);

			PoolEntry = NewEntry.Get();
			DrawIndirectPool.Emplace(MoveTemp(NewEntry));
		}

		DrawIndirectArgGenTasks.Add(Info);
		CachedSlot.Key = DrawIndirectPool.Num() - 1;
		CachedSlot.Value = PoolEntry->NumUsed * NIAGARA_DRAW_INDIRECT_ARGS_SIZE * sizeof(uint32);
		++PoolEntry->NumUsed;
	}

	return FIndirectArgSlot(DrawIndirectPool[CachedSlot.Key]->Buffer.Buffer, DrawIndirectPool[CachedSlot.Key]->Buffer.SRV, CachedSlot.Value);
}

void FNiagaraGPUInstanceCountManager::UpdateDrawIndirectBuffers(NiagaraEmitterInstanceBatcher& Batcher, FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel)
{
	INC_DWORD_STAT_BY(STAT_NiagaraIndirectDraws, DrawIndirectArgGenTasks.Num());

	if (DrawIndirectArgGenTasks.Num() || InstanceCountClearTasks.Num())
	{
		if (FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[FeatureLevel]))
		{
			SCOPED_DRAW_EVENT(RHICmdList, NiagaraUpdateDrawIndirectBuffers);
			FReadBuffer TaskInfosBuffer;

			//for (int32 i = 0; i < InstanceCountClearTasks.Num(); i++)
			//{
			//		UE_LOG(LogNiagara, Log, TEXT("InstanceCountClearTasks[%d] = %d"), i, InstanceCountClearTasks[i]);
			//}
			{
				// All draw indirect args task are run first because of the binding between the task ID and arg write offset.
				const uint32 ArgGenSize = DrawIndirectArgGenTasks.Num() * sizeof(FArgGenTaskInfo);
				const uint32 InstanceCountClearSize = InstanceCountClearTasks.Num() * sizeof(uint32);
				const uint32 TaskBufferSize = ArgGenSize + InstanceCountClearSize;
				TaskInfosBuffer.Initialize(sizeof(uint32), TaskBufferSize / sizeof(uint32), EPixelFormat::PF_R32_UINT, BUF_Volatile, TEXT("NiagaraTaskInfosBuffer"));
				uint8* TaskBufferData = (uint8*)RHILockVertexBuffer(TaskInfosBuffer.Buffer, 0, TaskBufferSize, RLM_WriteOnly);
				FMemory::Memcpy(TaskBufferData, DrawIndirectArgGenTasks.GetData(), ArgGenSize);
				FMemory::Memcpy(TaskBufferData + ArgGenSize, InstanceCountClearTasks.GetData(), InstanceCountClearSize);
				RHIUnlockVertexBuffer(TaskInfosBuffer.Buffer);
			}
			FNiagaraUAVPoolAccessScope UAVPoolAccessScope(Batcher);

			FUnorderedAccessViewRHIRef CountsUAV = nullptr;
			FShaderResourceViewRHIRef CulledCountsSRV = nullptr;
			TArray<FRHITransitionInfo, TInlineAllocator<10>> Transitions;
			for (auto& PoolEntry : DrawIndirectPool)
			{
				Transitions.Emplace(PoolEntry->Buffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute);
			}

			if (CountBuffer.UAV.IsValid())
			{
				Transitions.Emplace(CountBuffer.UAV, kCountBufferDefaultState, ERHIAccess::UAVCompute);
				CountsUAV = CountBuffer.UAV;
			}
			else
			{
				// This can happen if there are no InstanceCountClearTasks and all DrawIndirectArgGenTasks are using culled counts
				CountsUAV = Batcher.GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer);
			}

			if (CulledCountBuffer.SRV.IsValid())
			{
				if (bAcquiredCulledCounts)
				{
					Transitions.Emplace(CulledCountBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);
				}
				CulledCountsSRV = CulledCountBuffer.SRV.GetReference();
			}
			else
			{
				CulledCountsSRV = FNiagaraRenderer::GetDummyUIntBuffer();
			}

			RHICmdList.Transition(Transitions);

			FNiagaraDrawIndirectArgsGenCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FNiagaraDrawIndirectArgsGenCS::FSupportsTextureRW>(GRHISupportsRWTextureBuffers ? 1 : 0);
			TShaderMapRef<FNiagaraDrawIndirectArgsGenCS> DrawIndirectArgsGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);

			const int32 NumDispatches = FMath::Max(DrawIndirectPool.Num(), 1);
			uint32 ArgGenTaskOffset = 0;
			for (int32 DispatchIdx = 0; DispatchIdx < NumDispatches; ++DispatchIdx)
			{
				int32 NumArgGenTasks = 0;
				FUnorderedAccessViewRHIRef ArgsUAV;
				if (DrawIndirectPool.IsValidIndex(DispatchIdx))
				{
					FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[DispatchIdx];
					ArgsUAV = PoolEntry->Buffer.UAV;
					NumArgGenTasks = PoolEntry->NumUsed;
				}
				else
				{
					ArgsUAV = Batcher.GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer);
				}

				const bool bIsLastDispatch = DispatchIdx == (NumDispatches - 1);
				const int32 NumInstanceCountClearTasks = bIsLastDispatch ? InstanceCountClearTasks.Num() : 0;

				RHICmdList.SetComputeShader(DrawIndirectArgsGenCS.GetComputeShader());
				DrawIndirectArgsGenCS->SetOutput(RHICmdList, ArgsUAV, CountsUAV);
				DrawIndirectArgsGenCS->SetParameters(RHICmdList, TaskInfosBuffer.SRV, CulledCountsSRV, ArgGenTaskOffset, NumArgGenTasks, NumInstanceCountClearTasks);

				// If the device supports RW Texture buffers then we can use a single compute pass, otherwise we need to split into two passes
				if (GRHISupportsRWTextureBuffers)
				{
					DispatchComputeShader(RHICmdList, DrawIndirectArgsGenCS.GetShader(), FMath::DivideAndRoundUp(NumArgGenTasks + NumInstanceCountClearTasks, NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
					DrawIndirectArgsGenCS->UnbindBuffers(RHICmdList);
				}
				else
				{
					if (NumArgGenTasks > 0)
					{
						DispatchComputeShader(RHICmdList, DrawIndirectArgsGenCS.GetShader(), FMath::DivideAndRoundUp(NumArgGenTasks, NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
						DrawIndirectArgsGenCS->UnbindBuffers(RHICmdList);
					}

					if (NumInstanceCountClearTasks > 0)
					{
						FNiagaraDrawIndirectResetCountsCS::FPermutationDomain PermutationVectorResetCounts;
						TShaderMapRef<FNiagaraDrawIndirectResetCountsCS> DrawIndirectResetCountsArgsGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVectorResetCounts);
						RHICmdList.SetComputeShader(DrawIndirectResetCountsArgsGenCS.GetComputeShader());
						DrawIndirectResetCountsArgsGenCS->SetOutput(RHICmdList, CountBuffer.UAV);
						DrawIndirectResetCountsArgsGenCS->SetParameters(RHICmdList, TaskInfosBuffer.SRV, DrawIndirectArgGenTasks.Num(), NumInstanceCountClearTasks);
						DispatchComputeShader(RHICmdList, DrawIndirectResetCountsArgsGenCS.GetShader(), FMath::DivideAndRoundUp(NumInstanceCountClearTasks, NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT), 1, 1);
						DrawIndirectResetCountsArgsGenCS->UnbindBuffers(RHICmdList);
					}
				}

				ArgGenTaskOffset += NumArgGenTasks;
			}

			Transitions.Reset();
			for (auto& PoolEntry : DrawIndirectPool)
			{
				Transitions.Emplace(PoolEntry->Buffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs);
			}
			Transitions.Emplace(CountBuffer.UAV, ERHIAccess::UAVCompute, kCountBufferDefaultState);
			RHICmdList.Transition(Transitions);
		}
		// Once cleared to 0, the count are reusable.
		FreeEntries.Append(InstanceCountClearTasks);

		//for (int32 i = 0; i < FreeEntries.Num(); i++)
		//{
		//	UE_LOG(LogNiagara, Log, TEXT("FreeEntries[%d] = %d"), i, FreeEntries[i]);
		//}

		DrawIndirectArgGenTasks.Empty();
		DrawIndirectArgMap.Empty();
		InstanceCountClearTasks.Empty();

	}

	// Release culled count buffers
	// This is done outside of the above if as a mesh renderer could request a culled count but never add any indirect draws
	if (bAcquiredCulledCounts && RequiredCulledCounts > 0)
	{
		CulledCountBuffer.DiscardTransientResource();
	}
	bAcquiredCulledCounts = false;
	RequiredCulledCounts = 0; // reset this counter now that we're done with them

	if (GNiagaraIndirectArgsPoolAllowShrinking)
	{
		if (DrawIndirectPool.Num() == 1 && DrawIndirectPool[0]->NumAllocated > uint32(GNiagaraIndirectArgsPoolMinSize))
		{
			// See if this was a low water mark frame
			FIndirectArgsPoolEntryPtr& PoolEntry = DrawIndirectPool[0];
			const uint32 LowWaterCount = FMath::Max<uint32>(GNiagaraIndirectArgsPoolMinSize, PoolEntry->NumAllocated * GNiagaraIndirectArgsPoolLowWaterAmount);
			if (PoolEntry->NumUsed < LowWaterCount)
			{
				++DrawIndirectLowWaterFrames;
			}
			else
			{
				// We've allocated above the low water amount, reset the timer
				DrawIndirectLowWaterFrames = 0;
			}
		}
		else
		{
			// Either the pool is empty, at the min size, or we had to increase the pool size this frame. Either way, reset the shrink timer
			DrawIndirectLowWaterFrames = 0;
		}
	}

	// Clear indirect args pool counts
	for (auto& Pool : DrawIndirectPool)
	{
		Pool->NumUsed = 0;
	}
}

const uint32* FNiagaraGPUInstanceCountManager::GetGPUReadback()
{
	if (CountReadback && CountReadbackSize && CountReadback->IsReady())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadbackLock);
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
	if (UsedInstanceCounts > 0 && (UsedInstanceCounts != FreeEntries.Num()))
	{
		if (!CountReadback)
		{
			CountReadback = new FRHIGPUBufferReadback(TEXT("Niagara GPU Instance Count Readback"));
		}
		CountReadbackSize = UsedInstanceCounts;
		// No need for a transition, NiagaraEmitterInstanceBatcher ensures that the buffer is left in the
		// correct state after the sim.
		CountReadback->EnqueueCopy(RHICmdList, CountBuffer.Buffer);
	}
}

bool FNiagaraGPUInstanceCountManager::HasPendingGPUReadback() const
{
	return CountReadback && CountReadbackSize;
}

