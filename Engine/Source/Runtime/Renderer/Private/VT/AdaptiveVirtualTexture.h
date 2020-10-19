// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BinaryHeap.h"
#include "VirtualTexturing.h"

class FAllocatedVirtualTexture;
class FRHICommandList;
class FVirtualTextureSystem;

/**
 * Concrete implementation of an adaptive virtual texture.
 * This allocates multiple virtual textures within the same space: one each for a grid of UV ranges, and an additional persistent one for the low mips.
 * We then use an additional page table indirection texture in the shader to select the correct page table address range for our sampled UV.
 * We use the virtual texture feedback to decide when to increase or decrease the resolution of each UV range.
 * When we change resolution for a range we directly remap the page table entires. This removes the cost and any visual glitch from reproducing the pages.
 */
class FAdaptiveVirtualTexture final : public IAdaptiveVirtualTexture
{
public:
	FAdaptiveVirtualTexture(
		FAdaptiveVTDescription const& InAdaptiveDesc,
		FAllocatedVTDescription const& InAllocatedDesc);

	/** Initialize the object. This creates the persistent low mips allocated VT. */
	void Init(FVirtualTextureSystem* InSystem);
	
	/** Get a packed allocation key based on a virtual texture feedback request. The virtual texture system collects these opaque keys before queuing them for processing. */
	uint32 GetPackedAllocationRequest(uint32 vAddress, uint32 vLevelPlusOne, uint32 Frame) const;
	/** Queue a batch of allocation requests. These will be used to reallocate any virtual textures during the next call to UpdateAllocations(). */
	void QueuePackedAllocationRequests(uint32 const* Requests, uint32 NumRequests, uint32 Frame);
	/** Quee a batch of allocation requests. This static function relays all the requests to the individual object queues. */
	static void QueuePackedAllocationRequests(FVirtualTextureSystem* InSystem, uint32 const* Requests, uint32 NumRequests, uint32 Frame);
	/** Update any allocations based on recent requests. */
	void UpdateAllocations(FVirtualTextureSystem* InSystem, uint32 Frame);

	//~ Begin UTexture Interface.
	virtual IAllocatedVirtualTexture* GetAllocatedVirtualTexture() override;
	virtual int32 GetSpaceID() const override;
	//~ End UTexture Interface.

protected:
	void Allocate(FVirtualTextureSystem* InSystem, uint32 Request, uint32 Frame);
	void Reallocate(FVirtualTextureSystem* InSystem, int32 GridIndex, int32 NewLevel, uint32 Frame);
	bool FreeLRU(FVirtualTextureSystem* InSystem, uint32 Frame, uint32 FrameUnusedThreshold);
	void Free(FVirtualTextureSystem* InSystem, int32 GridIndex, uint32 Frame);

	//~ Begin UTexture Interface.
	virtual void Destroy(class FVirtualTextureSystem* InSystem) override;
	//~ End UTexture Interface.

private:
	FAdaptiveVTDescription AdaptiveDesc;
	FAllocatedVTDescription AllocatedDesc;
	
	int32 MaxLevel;
	FIntPoint GridSize;

	FAllocatedVirtualTexture* AllocatedVirtualTextureLowMips;
	TArray<FAllocatedVirtualTexture*> AllocatedVirtualTextureGrid;

	int32 NumAllocated;
	FBinaryHeap<uint32, uint32> FreeHeap;
	TArray<uint32> RequestsToMap;
};
