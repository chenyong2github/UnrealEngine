// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RHITransientResourceAllocator.h"

class FRHITransientResourceSystem;
class FRHITransientResourceAllocator;

#define RHICORE_TRANSIENT_ALLOCATOR_DEBUG (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	#define IF_RHICORE_TRANSIENT_ALLOCATOR_DEBUG(Op) Op
#else
	#define IF_RHICORE_TRANSIENT_ALLOCATOR_DEBUG(Op)
#endif

struct FRHITransientMemoryStats
{
	FRHITransientMemoryStats() = default;

	void Add(const FRHITransientMemoryStats& Other)
	{
		TotalSizeWithAliasing += Other.TotalSizeWithAliasing;
		TotalSize += Other.TotalSize;
		AllocationCount += Other.AllocationCount;
	}

	// Total allocated memory usage with aliasing.
	uint64 TotalSizeWithAliasing = 0;

	// Total allocated memory usage without aliasing.
	uint64 TotalSize = 0;

	// The number of allocations made from the transient allocator.
	uint32 AllocationCount = 0;
};

/** An RHI transient resource cache designed to optimize fetches for resources placed into a heap with an offset.
 *  The cache has a fixed capacity whereby no garbage collection will occur. Once that capacity is exceeded, garbage
 *  collection is invoked on resources older than a specified generation (where generation is incremented with each
 *  cycle of forfeiting acquired resources).
 */
template <typename TransientResourceType>
class TRHITransientResourceCache
{
public:
	static const uint32 kInfinity = ~0u;
	static const uint32 kDefaultCapacity = kInfinity;
	static const uint32 kDefaultGarbageCollectLatency = 32;

	TRHITransientResourceCache(uint32 InCapacity = kDefaultCapacity, uint32 InGarbageCollectLatency = kDefaultGarbageCollectLatency)
		: GarbageCollectLatency(InGarbageCollectLatency)
		, Capacity(InCapacity)
	{
		if (Capacity != kInfinity)
		{
			Cache.Reserve(Capacity);
		}
	}

	~TRHITransientResourceCache()
	{
		for (const FCacheItem& Item : Cache)
		{
			delete Item.Resource;
		}
	}

	template <typename CreateFunctionType>
	TransientResourceType* Acquire(uint64 Hash, CreateFunctionType CreateFunction)
	{
		for (int32 Index = 0; Index < Cache.Num(); ++Index)
		{
			const FCacheItem& CacheItem = Cache[Index];

			if (CacheItem.Hash == Hash)
			{
				TransientResourceType* Resource = CacheItem.Resource;
				Cache.RemoveAtSwap(Index);
				return Resource;
			}
		}

		return CreateFunction(Hash);
	}

	void Forfeit(TArrayView<TransientResourceType* const> Resources)
	{
		CurrentGeneration++;

		for (TransientResourceType* Resource : Resources)
		{
			Cache.Emplace(Resource, Resource->GetHash(), CurrentGeneration);
		}

		Algo::Sort(Cache, [](const FCacheItem& LHS, const FCacheItem& RHS)
		{
			return LHS.Generation > RHS.Generation;
		});

		while (uint32(Cache.Num()) > Capacity)
		{
			if (!TryReleaseItem())
			{
				break;
			}
		}
	}

	FORCEINLINE uint32 GetSize() const { return Cache.Num(); }
	FORCEINLINE uint32 GetCapacity() const { return Capacity; }

private:
	bool TryReleaseItem()
	{
		const FCacheItem& Item = Cache.Top();

		if (Item.Generation + GarbageCollectLatency < CurrentGeneration)
		{
			delete Item.Resource;
			Cache.Pop();
			return true;
		}

		return false;
	}

	struct FCacheItem
	{
		FCacheItem(TransientResourceType* InResource, uint64 InHash, uint64 InGeneration)
			: Resource(InResource)
			, Hash(InHash)
			, Generation(InGeneration)
		{}

		TransientResourceType* Resource;
		uint64 Hash{};
		uint64 Generation{};
	};

	TArray<FCacheItem> Cache;
	uint64 CurrentGeneration{};
	uint32 GarbageCollectLatency;
	uint32 Capacity;
};

enum class ERHITransientHeapFlags : uint8
{
	// Supports placing buffers onto the heap.
	AllowBuffers = 1 << 0,

	// Supports placing textures with UAV support onto the heap.
	AllowTextures = 1 << 1,

	// Supports placing render targets onto the heap.
	AllowRenderTargets = 1 << 2,

	// Supports all resource types.
	AllowAll = AllowBuffers | AllowTextures | AllowRenderTargets
};

ENUM_CLASS_FLAGS(ERHITransientHeapFlags);

struct FRHITransientHeapInitializer
{
	// Size of the heap in bytes.
	uint64 Size{};

	// Alignment of the heap in bytes.
	uint32 Alignment{};

	// Flags used to filter resource allocations within the heap.
	ERHITransientHeapFlags Flags = ERHITransientHeapFlags::AllowAll;

	// Size of the texture cache before elements are evicted.
	uint32 TextureCacheSize{};

	// Size of the buffer cache before elements are evicted.
	uint32 BufferCacheSize{};
};

/** The base class for a platform heap implementation. Transient resources are placed on the heap at specific
 *  byte offsets. Each heap additionally contains a cache of RHI transient resources, each with its own RHI
 *  resource and cache of RHI views. The lifetime of the resource cache is tied to the heap.
 */
class RHICORE_API FRHITransientHeap
{
public:
	FRHITransientHeap(const FRHITransientHeapInitializer& InInitializer)
		: Initializer(InInitializer)
		, Textures(InInitializer.TextureCacheSize)
		, Buffers(InInitializer.BufferCacheSize)
	{}

	virtual ~FRHITransientHeap() = default;

	FORCEINLINE const FRHITransientHeapInitializer& GetInitializer() const { return Initializer; }

	FORCEINLINE uint64 GetCapacity() const { return Initializer.Size; }

	FORCEINLINE uint64 GetLastUsedGarbageCollectCycle() const { return LastUsedGarbageCollectCycle; }

	FORCEINLINE bool IsAllocationSupported(uint64 Size, ERHITransientHeapFlags InFlags) const
	{
		return Size <= Initializer.Size && EnumHasAnyFlags(Initializer.Flags, InFlags);
	}

private:
	template <typename CreateFunctionType>
	FRHITransientTexture* AcquireTexture(const FRHITextureCreateInfo& CreateInfo, uint64 HeapOffset, CreateFunctionType CreateFunction);

	template <typename CreateFunctionType>
	FRHITransientBuffer* AcquireBuffer(const FRHIBufferCreateInfo& CreateInfo, uint64 HeapOffset, CreateFunctionType CreateFunction);

	void ForfeitResources();

	FRHITransientHeapInitializer Initializer;
	uint64 LastUsedGarbageCollectCycle = 0;

	TRHITransientResourceCache<FRHITransientTexture> Textures;
	TRHITransientResourceCache<FRHITransientBuffer> Buffers;
	TArray<FRHITransientTexture*> AllocatedTextures;
	TArray<FRHITransientBuffer*> AllocatedBuffers;

	friend FRHITransientResourceSystem;
	friend FRHITransientResourceAllocator;
};

struct RHICORE_API FRHITransientResourceSystemInitializer
{
	// Creates a default initializer using common RHI CVars.
	static FRHITransientResourceSystemInitializer CreateDefault();

	static const uint32 kDefaultGarbageCollectLatency = 20;
	static const uint32 kDefaultResourceCacheSize = 256;

	// The minimum size to use when creating a heap. This is the default but can grow based on allocations.
	uint64 MinimumHeapSize{};

	// The maximum size of a pool. Allocations above this size will fail.
	uint64 MaximumHeapSize{};

	// The alignment of all heaps in the cache.
	uint32 HeapAlignment{};

	// The latency between the completed fence value and the used fence value to invoke garbage collection of the heap.
	uint32 GarbageCollectLatency = kDefaultGarbageCollectLatency;

	// Size of the texture cache before elements are evicted.
	uint32 TextureCacheSize = kDefaultResourceCacheSize;

	// Size of the buffer cache before elements are evicted.
	uint32 BufferCacheSize = kDefaultResourceCacheSize;

	// Whether all heaps should be created with the AllowAll heap flag.
	bool bSupportsAllHeapFlags = true;
};

/** The RHI transient resource system is a base class for the platform implementation. It has a persistent lifetime
 *  and contains a cache of transient heaps. The transient allocator acquires heaps from the system and forfeits them
 *  at the end of its lifetime. Garbage collection of heaps is done using an internal counter that increments with
 *  each GarbageCollect call. This should be done periodically. Heaps older than a platform-specified fence latency
 *  are destroyed. Additionally, statistics are gathered automatically and reported to the platform implementation for
 *  stats tracking. However, 'rhitransientmemory' stats are reported automatically by the implementation itself.
 */
class RHICORE_API FRHITransientResourceSystem
{
public:
	FRHITransientResourceSystem(const FRHITransientResourceSystemInitializer& InInitializer)
		: Initializer(InInitializer)
	{}

	FRHITransientResourceSystem(const FRHITransientResourceSystem&) = delete;

	virtual ~FRHITransientResourceSystem();

	void ReleaseHeaps();

	void GarbageCollect();

	void UpdateStats();

	FORCEINLINE uint64 GetHeapSize(uint64 RequestedHeapSize) const
	{
		check(RequestedHeapSize <= Initializer.MaximumHeapSize);
		return FMath::Clamp(FMath::RoundUpToPowerOfTwo64(RequestedHeapSize), Initializer.MinimumHeapSize, Initializer.MaximumHeapSize);
	}

	FORCEINLINE uint64 GetMinimumHeapSize() const { return Initializer.MinimumHeapSize; }
	FORCEINLINE uint64 GetMaximumHeapSize() const { return Initializer.MaximumHeapSize; }
	FORCEINLINE uint32 GetHeapAlignment() const { return Initializer.HeapAlignment; }
	FORCEINLINE uint32 GetHeapCount() const { return Heaps.Num(); }

	FORCEINLINE FRHITransientHeap* GetHeap(uint32 Index) { return Heaps[Index]; }

protected:
	struct FStats
	{
		FRHITransientMemoryStats Textures;
		FRHITransientMemoryStats Buffers;

		// Total memory used by the underlying heaps.
		uint64 TotalMemoryUsed{};
	};

private:
	// Called by the transient allocator to acquire a heap from the cache.
	FRHITransientHeap* AcquireHeap(uint64 FirstAllocationSize, ERHITransientHeapFlags FirstAllocationHeapFlags);

	// Called by the transient allocator to forfeit all acquired heaps back to the cache.
	void ForfeitHeaps(TArrayView<FRHITransientHeap* const> Heaps);

	//////////////////////////////////////////////////////////////////////////
	//! Platform API

	// Called when a new heap is being created and added to the pool.
	virtual FRHITransientHeap* CreateHeap(const FRHITransientHeapInitializer& Initializer) = 0;

	// Called for the platform to report stats.
	virtual void ReportStats(const FStats& Stats) {};

	//////////////////////////////////////////////////////////////////////////

	FRHITransientResourceSystemInitializer Initializer;

	uint64 GarbageCollectCycle{};

	FCriticalSection HeapCriticalSection;
	TArray<FRHITransientHeap*> Heaps;

	FRHITransientMemoryStats TextureStats;
	FRHITransientMemoryStats BufferStats;

	friend FRHITransientResourceAllocator;
};

/** Tracks resource allocations on the heap and adds overlap events to transient resources. */
class RHICORE_API FRHITransientResourceOverlapTracker final
{
public:
	void Track(FRHITransientResource* InResource, uint64 OffsetMin, uint64 OffsetMax);

private:
	struct FResourceRange
	{
		FRHITransientResource* Resource{};
		uint64 OffsetMin{};
		uint64 OffsetMax{};
	};

	TArray<FResourceRange, TMemStackAllocator<>> ResourceRanges;
};

/** Represents an allocation from the transient heap. */
struct FRHITransientHeapAllocation
{
	FRHITransientHeapAllocation() = default;

	FORCEINLINE bool IsValid() const { return Size != 0; }

	bool operator==(const FRHITransientHeapAllocation& Other) const
	{
		return Size == Other.Size && Offset == Other.Offset && AlignmentPad == Other.AlignmentPad && HeapIndex == Other.HeapIndex;
	}

	bool operator!=(const FRHITransientHeapAllocation& Other) const
	{
		return !(*this == Other);
	}

	// Size of the allocation made from the allocator (aligned).
	uint64 Size = 0;

	// Offset in the transient heap; front of the heap starts at 0.
	uint64 Offset = 0;

	// Number of bytes of padding were added to the offset.
	uint32 AlignmentPad = 0;

	// Index of the transient heap.
	uint32 HeapIndex = 0;
};

/** A simple first-fit allocator for placing resources onto a transient heap and tracking their aliasing overlap events. */
class RHICORE_API FRHITransientHeapAllocator final
{
public:
	FRHITransientHeapAllocator(const FRHITransientHeapInitializer& Initializer, uint32 HeapIndex);
	~FRHITransientHeapAllocator();

	FRHITransientHeapAllocation Allocate(uint64 Size, uint32 Alignment);
	void Deallocate(FRHITransientHeapAllocation Allocation);

	FORCEINLINE void TrackOverlap(FRHITransientResource* Resource, const FRHITransientHeapAllocation& Allocation)
	{
		OverlapTracker.Track(Resource, Allocation.Offset, Allocation.Offset + Allocation.Size);
	}

	FORCEINLINE uint64 GetCapacity() const { return Initializer.Size; }
	FORCEINLINE uint64 GetUsedSize() const { return UsedSize; }
	FORCEINLINE uint64 GetFreeSize() const { return Initializer.Size - UsedSize; }
	FORCEINLINE uint64 GetAlignmentWaste() const { return AlignmentWaste; }
	FORCEINLINE uint32 GetAllocationCount() const { return AllocationCount; }

	FORCEINLINE bool IsFull() const { return UsedSize == Initializer.Size; }
	FORCEINLINE bool IsEmpty() const { return UsedSize == 0; }

	// Returns whether the requested allocation can succeed.
	FORCEINLINE bool IsAllocationSupported(uint64 Size, ERHITransientHeapFlags Flags) const
	{
		return Size <= GetFreeSize() && EnumHasAnyFlags(Initializer.Flags, Flags);
	}

private:
	using FRangeHandle = uint16;
	static const FRangeHandle InvalidRangeHandle = FRangeHandle(~0);

	struct FRange
	{
		uint64 Size{};
		uint64 Offset{};
		FRangeHandle NextFreeHandle = InvalidRangeHandle;

		FORCEINLINE uint64 GetStart() const { return Offset; }
		FORCEINLINE uint64 GetEnd() const { return Size + Offset; }
	};

	FORCEINLINE FRangeHandle GetFirstFreeRangeHandle()
	{
		return Ranges[HeadHandle].NextFreeHandle;
	}

	FRangeHandle CreateRange()
	{
		if (!RangeFreeList.IsEmpty())
		{
			return RangeFreeList.Pop();
		}
		Ranges.Emplace();
		return FRangeHandle(Ranges.Num() - 1);
	}

	FRangeHandle InsertRange(FRangeHandle PreviousHandle, uint64 Offset, uint64 Size)
	{
		FRangeHandle Handle = CreateRange();

		FRange& CurrentRange = Ranges[Handle];
		CurrentRange.Offset = Offset;
		CurrentRange.Size = Size;

		FRange& PreviousRange = Ranges[PreviousHandle];
		CurrentRange.NextFreeHandle = PreviousRange.NextFreeHandle;
		PreviousRange.NextFreeHandle = Handle;

		return Handle;
	}

	void RemoveRange(FRangeHandle PreviousHandle, FRangeHandle CurrentHandle)
	{
		FRange& PreviousRange = Ranges[PreviousHandle];
		FRange& CurrentRange = Ranges[CurrentHandle];

		PreviousRange.NextFreeHandle = CurrentRange.NextFreeHandle;
		CurrentRange.NextFreeHandle = InvalidRangeHandle;

		RangeFreeList.Add(CurrentHandle);
	}

	struct FFindResult
	{
		uint64 LeftoverSize = 0;
		FRangeHandle PreviousHandle = InvalidRangeHandle;
		FRangeHandle FoundHandle = InvalidRangeHandle;
	};

	FFindResult FindFreeRange(uint64 Size, uint32 Alignment);

	void Validate();

	FRHITransientHeapInitializer Initializer;

	uint64 UsedSize{};
	uint64 AlignmentWaste{};
	uint32 AllocationCount{};
	uint32 HeapIndex{};

	FRangeHandle HeadHandle = InvalidRangeHandle;
	TArray<FRangeHandle, TInlineAllocator<4, TMemStackAllocator<>>> RangeFreeList;
	TArray<FRange, TInlineAllocator<4, TMemStackAllocator<>>> Ranges;

	FRHITransientResourceOverlapTracker OverlapTracker;
};

/** A helper class for implementing IRHITransientResourceAllocator. This class is designed for composition instead of
 *  inheritance to keep the platform implementation clean. Its methods are not straight overrides of the user-facing
 *  class; rather, they are designed to streamline the platform implementation. This object is designed to match the
 *  lifecycle of IRHITransientResourceAllocator. Specifically, it must be short-lived and its API used on the render
 *  thread, as it utilizes MemStack-allocated containers for performance. Its state is built from scratch each allocation
 *  cycle and released at the end. Heaps are acquired from the parent transient resource system, and transient resources
 *  from respective heaps.
 */
class RHICORE_API FRHITransientResourceAllocator final
{
public:
	FRHITransientResourceAllocator(FRHITransientResourceSystem& InParentSystem);
	FRHITransientResourceAllocator(const FRHITransientResourceAllocator&) = delete;

	struct FResourceInitializer
	{
		FResourceInitializer(FRHITransientHeap& InHeap, const FRHITransientHeapAllocation& InAllocation, uint64 InHash)
			: Heap(InHeap)
			, Allocation(InAllocation)
			, Hash(InHash)
		{}

		// The heap on which to create the resource.
		FRHITransientHeap& Heap;

		// The allocation (offset / size) on the provided heap.
		const FRHITransientHeapAllocation& Allocation;

		// The unique hash computed from the create info and allocation offset.
		const uint64 Hash;
	};

	using FCreateTextureFunction = TFunction<FRHITransientTexture*(const FResourceInitializer&)>;

	/** Allocates a texture on a heap at a specific offset, returning a cached RHI transient texture pointer, or null
	 *  if the allocation failed. TextureSize and TextureAlignment are platform specific and must be derived from the
	 *  texture create info and passed in, along with a platform-specific texture creation function if no cached resource
	 *  if found.
	 */
	FRHITransientTexture* CreateTexture(
		const FRHITextureCreateInfo& CreateInfo,
		const TCHAR* DebugName,
		uint64 TextureSize,
		uint32 TextureAlignment,
		FCreateTextureFunction CreateTextureFunction);

	using FCreateBufferFunction = TFunction<FRHITransientBuffer*(const FResourceInitializer&)>;

	/** Allocates a buffer on a heap at a specific offset, returning a cached RHI transient buffer pointer, or null
	 *  if the allocation failed. BufferSize and BufferAlignment are platform specific and must be derived from the
	 *  buffer create info and passed in, along with a platform-specific buffer creation function if no cached resource
	 *  if found.
	 */
	FRHITransientBuffer* CreateBuffer(
		const FRHIBufferCreateInfo& CreateInfo,
		const TCHAR* DebugName,
		uint32 BufferSize,
		uint32 BufferAlignment,
		FCreateBufferFunction CreateBufferFunction);

	// Deallocates a texture from its parent heap. Provide the current platform fence value used to update the heap.
	FORCEINLINE void DeallocateMemory(FRHITransientTexture* Texture)
	{
		DeallocateMemoryInternal(Texture, TextureStats);
	}

	// Deallocates a buffer from its parent heap. Provide the current platform fence value used to update the heap.
	FORCEINLINE void DeallocateMemory(FRHITransientBuffer* Buffer)
	{
		DeallocateMemoryInternal(Buffer, BufferStats);
	}

	// Called to signify all allocations have completed. Forfeits all resources / heaps back to the parent system.
	void Freeze(FRHICommandListImmediate& RHICmdList);

private:
	struct FMemoryStats : FRHITransientMemoryStats
	{
		uint64 CurrentSizeWithAliasing{};
	};

	FRHITransientHeapAllocation Allocate(FMemoryStats& StatsToUpdate, uint64 Size, uint32 Alignment, ERHITransientHeapFlags ResourceHeapFlags);

	void DeallocateMemoryInternal(FRHITransientResource* InResource, FMemoryStats& StatsToUpdate);

	void InitResource(FRHITransientResource* TransientResource, const FRHITransientHeapAllocation& Allocation, const TCHAR* Name);

	FRHITransientResourceSystem& ParentSystem;

	// Tracks state on the rendering thread; must be cleared before the destructor.
	TArray<FRHITransientHeap*, TInlineAllocator<4, TMemStackAllocator<>>> Heaps;
	TArray<FRHITransientHeapAllocator, TInlineAllocator<4, TMemStackAllocator<>>> HeapAllocators;
	TArray<FRHITransientHeapAllocation, TInlineAllocator<4, TMemStackAllocator<>>> HeapAllocations;

#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	TArray<FRHITransientTexture*, TMemStackAllocator<>> DebugTextures;
	TArray<FRHITransientBuffer*, TMemStackAllocator<>> DebugBuffers;
#endif

	FMemoryStats TextureStats;
	FMemoryStats BufferStats;

	uint64 AllocatedTextureSize{};
	uint64 AllocatedBufferSize{};
};