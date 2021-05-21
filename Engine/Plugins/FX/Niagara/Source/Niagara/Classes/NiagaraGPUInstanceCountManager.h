// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraGPUInstanceCountManager.h: GPU particle count handling
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHIUtilities.h"
#include "NiagaraDrawIndirect.h"
#include "RHIGPUReadback.h"

class FRHIGPUMemoryReadback;
class NiagaraEmitterInstanceBatcher;

// The number of GPU renderers registered in the instance count manager.
// Shared between the manager and the renderers.
class FNiagaraGPURendererCount : public FRefCountedObject
{
public:
	int32 Value = 0;
};

FORCEINLINE uint32 GetTypeHash(const FNiagaraDrawIndirectArgGenTaskInfo& Info)
{
	return HashCombine(Info.InstanceCountBufferOffset, HashCombine(Info.NumIndicesPerInstance, HashCombine(Info.StartIndexLocation, Info.Flags)));
}

/**
 * A manager that handles the buffer containing the GPU particle count.
 * Also provides related functionalities like the generation of the draw indirect buffer.
 */
class FNiagaraGPUInstanceCountManager
{
public:
	struct FIndirectArgSlot
	{
		FVertexBufferRHIRef Buffer;
		FShaderResourceViewRHIRef SRV;
		uint32 Offset = INDEX_NONE;

		FIndirectArgSlot() {}
		FIndirectArgSlot(FVertexBufferRHIRef InBuffer, FShaderResourceViewRHIRef InSRV, uint32 InOffset) : Buffer(InBuffer), SRV(InSRV), Offset(InOffset) {}
		FORCEINLINE bool IsValid() const { return Offset != INDEX_NONE; }
	};

	FNiagaraGPUInstanceCountManager();
	~FNiagaraGPUInstanceCountManager();

	// Init resource for the first time.
	void InitRHI();
	// Free resources.
	void ReleaseRHI();

	FRWBuffer& GetInstanceCountBuffer()
	{
		check(UsedInstanceCounts <= AllocatedInstanceCounts); // Can't resize after after the buffer gets bound.
		return CountBuffer;
	}

	/** Free the entry and reset it to INDEX_NONE if valid. */
	void FreeEntry(uint32& BufferOffset);

	/** Free and array of entries, you are expected to reset or change to INDEX_NONE. */
	void FreeEntryArray(TConstArrayView<uint32> EntryArray);

	uint32 AcquireEntry();

	uint32 AcquireCulledEntry()
	{
		check(!bAcquiredCulledCounts);
		return RequiredCulledCounts++;
	}

	FRWBuffer* AcquireCulledCountsBuffer(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel);

	const uint32* GetGPUReadback();
	void ReleaseGPUReadback();
	void EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList);
	bool HasPendingGPUReadback() const;

	/** Add a draw indirect task to generate the draw indirect args. Returns the draw indirect arg buffer offset. */
	FIndirectArgSlot AddDrawIndirect(uint32 InstanceCountBufferOffset, uint32 NumIndicesPerInstance, uint32 StartIndexLocation,
		bool bIsInstancedStereoEnabled, bool bCulled);

	// Resize instance count and draw indirect buffers to ensure it is big enough to hold all draw indirect args.
	void ResizeBuffers(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, int32 ReservedInstanceCounts);

	void FlushIndirectArgsPool();

	// Generate the draw indirect buffers, and reset all release counts.
	void UpdateDrawIndirectBuffers(NiagaraEmitterInstanceBatcher& Batcher, FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel);

	static const ERHIAccess kCountBufferDefaultState;

	bool HasEntriesPendingFree() const { return InstanceCountClearTasks.Num() > 0; }

protected:
	struct FIndirectArgsPoolEntry
	{
		FRWBuffer Buffer;
		uint32 NumAllocated = 0;
		uint32 NumUsed = 0;
	};

	using FIndirectArgsPoolEntryPtr = TUniquePtr<FIndirectArgsPoolEntry>;

	void ReleaseCounts();

	/** The current used instance counts allocated from FNiagaraDataBuffer::AllocateGPU() */
	int32 UsedInstanceCounts = 0;
	/** The allocated instance counts in CountBuffer */
	int32 AllocatedInstanceCounts = 0;

	/** The number of culled instance counts needed from view culling */
	int32 RequiredCulledCounts = 0;
	/** The allocated instance counts in the culled count buffer*/
	int32 AllocatedCulledCounts = 0;
	/** Whether or not the culled counts were acquired this frame */
	bool bAcquiredCulledCounts = false;

	/** A buffer holding the each emitter particle count after a simulation tick. */
	FRWBuffer CountBuffer;
	FRWBuffer CulledCountBuffer;
	TArray<uint32> FreeEntries;
	FRHIGPUMemoryReadback* CountReadback = nullptr;
	int32 CountReadbackSize = 0;

	/** The list of all draw indirected tasks that are to be run in UpdateDrawIndirectBuffers() */
	using FArgGenTaskInfo = FNiagaraDrawIndirectArgGenTaskInfo;
	using FArgGenSlotInfo = TTuple<uint32, uint32>;

	TArray<FArgGenTaskInfo> DrawIndirectArgGenTasks;
	/** The map between each task FArgGenTaskInfo and entry offset from DrawIndirectArgGenTasks. Used to reuse entries. */
	TMap<FArgGenTaskInfo, FArgGenSlotInfo> DrawIndirectArgMap;
	/** The list of all instance count clear tasks that are to be run in UpdateDrawIndirectBuffers() */
	TArray<uint32> InstanceCountClearTasks;
	/** Buffers holding drawindirect data to render GPU emitter renderers. */
	TArray<FIndirectArgsPoolEntryPtr> DrawIndirectPool;
	uint32 DrawIndirectLowWaterFrames = 0;
};