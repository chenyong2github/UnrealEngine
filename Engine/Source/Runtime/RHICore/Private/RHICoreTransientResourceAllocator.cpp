// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHICoreTransientResourceAllocator.h"

static int32 GRHITransientAllocatorMinimumHeapSize = 128;
static FAutoConsoleVariableRef CVarGRHITransientAllocatorMinimumHeapSize(
	TEXT("RHI.TransientAllocator.MinimumHeapSize"),
	GRHITransientAllocatorMinimumHeapSize,
	TEXT("Minimum size of an RHI transient heap in MB. Heaps will default to this size and grow to the maximum based on the first allocation (Default 128)."),
	ECVF_ReadOnly);

static int32 GRHITransientAllocatorMaximumHeapSize = 512;
static FAutoConsoleVariableRef CVarRHITransientAllocatorMaximumHeapSize(
	TEXT("RHI.TransientAllocator.MaximumHeapSize"),
	GRHITransientAllocatorMaximumHeapSize,
	TEXT("Maximum size of an RHI transient allocation in MB. Allocations larger than this will fail the transient allocator (Default 512)."),
	ECVF_ReadOnly);

static int32 GRHITransientAllocatorBufferCacheSize = 64;
static FAutoConsoleVariableRef CVarRHITransientAllocatorBufferCacheSize(
	TEXT("RHI.TransientAllocator.BufferCacheSize"),
	GRHITransientAllocatorBufferCacheSize,
	TEXT("The maximum number of RHI buffers to cache on each heap before garbage collecting."),
	ECVF_ReadOnly);

static int32 GRHITransientAllocatorTextureCacheSize = 64;
static FAutoConsoleVariableRef CVarRHITransientAllocatorTextureCacheSize(
	TEXT("RHI.TransientAllocator.TextureCacheSize"),
	GRHITransientAllocatorTextureCacheSize,
	TEXT("The maximum number of RHI textures to cache on each heap before garbage collecting."),
	ECVF_ReadOnly);

DECLARE_STATS_GROUP(TEXT("RHI: Transient Memory"), STATGROUP_RHITransientMemory, STATCAT_Advanced);

DECLARE_MEMORY_STAT(TEXT("Memory Allocated"), STAT_RHITransientMemoryAllocated, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Memory Requested"), STAT_RHITransientMemoryRequested, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Memory Used"), STAT_RHITransientMemoryUsed, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Buffer Memory Used"), STAT_RHITransientBufferMemoryUsed, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Buffer Memory Requested"), STAT_RHITransientBufferMemoryRequested, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Texture Memory Used"), STAT_RHITransientTextureMemoryUsed, STATGROUP_RHITransientMemory);
DECLARE_MEMORY_STAT(TEXT("Texture Memory Requested"), STAT_RHITransientTextureMemoryRequested, STATGROUP_RHITransientMemory);

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Resources"), STAT_RHITransientResources, STATGROUP_RHITransientMemory);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Textures"), STAT_RHITransientTextures, STATGROUP_RHITransientMemory);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Buffers"), STAT_RHITransientBuffers, STATGROUP_RHITransientMemory);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Heaps"), STAT_RHITransientHeaps, STATGROUP_RHITransientMemory);

inline uint64 ComputeHash(const FRHITextureCreateInfo& InCreateInfo, uint64 HeapOffset)
{
	// Make sure all padding is removed.
	FRHITextureCreateInfo NewInfo;
	FPlatformMemory::Memzero(&NewInfo, sizeof(FRHITextureCreateInfo));
	NewInfo = InCreateInfo;
	return CityHash64WithSeed((const char*)&NewInfo, sizeof(FRHITextureCreateInfo), HeapOffset);
}

inline uint64 ComputeHash(const FRHIBufferCreateInfo& InCreateInfo, uint64 HeapOffset)
{
	return CityHash64WithSeed((const char*)&InCreateInfo, sizeof(FRHIBufferCreateInfo), HeapOffset);
}

//////////////////////////////////////////////////////////////////////////

template <typename CreateFunctionType>
FRHITransientTexture* FRHITransientHeap::AcquireTexture(const FRHITextureCreateInfo& CreateInfo, uint64 HeapOffset, CreateFunctionType CreateFunction)
{
	const uint64 Hash = ComputeHash(CreateInfo, HeapOffset);
	FRHITransientTexture* Texture = Textures.Acquire(Hash, CreateFunction);
	AllocatedTextures.Emplace(Texture);
	return Texture;
}

template <typename CreateFunctionType>
FRHITransientBuffer* FRHITransientHeap::AcquireBuffer(const FRHIBufferCreateInfo& CreateInfo, uint64 HeapOffset, CreateFunctionType CreateFunction)
{
	const uint64 Hash = ComputeHash(CreateInfo, HeapOffset);
	FRHITransientBuffer* Buffer = Buffers.Acquire(Hash, CreateFunction);
	AllocatedBuffers.Emplace(Buffer);
	return Buffer;
}

void FRHITransientHeap::ForfeitResources()
{
	Textures.Forfeit(AllocatedTextures);
	AllocatedTextures.Reset();

	Buffers.Forfeit(AllocatedBuffers);
	AllocatedBuffers.Reset();
}

//////////////////////////////////////////////////////////////////////////

FRHITransientResourceSystemInitializer FRHITransientResourceSystemInitializer::CreateDefault()
{
	FRHITransientResourceSystemInitializer Initializer;
	Initializer.MinimumHeapSize = GRHITransientAllocatorMinimumHeapSize * 1024 * 1024;
	Initializer.MaximumHeapSize = GRHITransientAllocatorMaximumHeapSize * 1024 * 1024;
	Initializer.BufferCacheSize = GRHITransientAllocatorBufferCacheSize;
	Initializer.TextureCacheSize = GRHITransientAllocatorTextureCacheSize;
	return Initializer;
}

FRHITransientResourceSystem::~FRHITransientResourceSystem()
{
	ReleaseHeaps();
}

FRHITransientHeap* FRHITransientResourceSystem::AcquireHeap(uint64 FirstAllocationSize, ERHITransientHeapFlags FirstAllocationHeapFlags)
{
	{
		FScopeLock Lock(&HeapCriticalSection);

		for (int32 HeapIndex = 0; HeapIndex < Heaps.Num(); ++HeapIndex)
		{
			FRHITransientHeap* Heap = Heaps[HeapIndex];

			if (Heap->IsAllocationSupported(FirstAllocationSize, FirstAllocationHeapFlags))
			{
				Heaps.RemoveAt(HeapIndex);
				return Heap;
			}
		}
	}

	FRHITransientHeapInitializer HeapInitializer;
	HeapInitializer.Size = GetHeapSize(FirstAllocationSize);
	HeapInitializer.Alignment = GetHeapAlignment();
	HeapInitializer.Flags = (Initializer.bSupportsAllHeapFlags ? ERHITransientHeapFlags::AllowAll : FirstAllocationHeapFlags);
	HeapInitializer.TextureCacheSize = Initializer.TextureCacheSize;
	HeapInitializer.BufferCacheSize = Initializer.BufferCacheSize;

	return CreateHeap(HeapInitializer);
}

void FRHITransientResourceSystem::ForfeitHeaps(TArrayView<FRHITransientHeap* const> InForfeitedHeaps)
{
	FScopeLock Lock(&HeapCriticalSection);

	for (FRHITransientHeap* Heap : InForfeitedHeaps)
	{
		Heap->ForfeitResources();
		Heap->LastUsedGarbageCollectCycle = GarbageCollectCycle;
	}

	Heaps.Append(InForfeitedHeaps.GetData(), InForfeitedHeaps.Num());

	Algo::Sort(Heaps, [](const FRHITransientHeap* LHS, const FRHITransientHeap* RHS)
	{
		// Sort by smaller heap first.
		if (LHS->GetCapacity() != RHS->GetCapacity())
		{
			return LHS->GetCapacity() < RHS->GetCapacity();
		}

		// Sort next by most recently used first.
		return LHS->GetLastUsedGarbageCollectCycle() >= RHS->GetLastUsedGarbageCollectCycle();
	});
}

void FRHITransientResourceSystem::GarbageCollect()
{
	FScopeLock Lock(&HeapCriticalSection);

	for (int32 HeapIndex = 0; HeapIndex < Heaps.Num(); ++HeapIndex)
	{
		FRHITransientHeap* Heap = Heaps[HeapIndex];

		if (Heap->GetLastUsedGarbageCollectCycle() + Initializer.GarbageCollectLatency <= GarbageCollectCycle)
		{
			Heaps.RemoveAt(HeapIndex);
			HeapIndex--;

			delete Heap;
		}
	}

	GarbageCollectCycle++;
}

void FRHITransientResourceSystem::ReleaseHeaps()
{
	FScopeLock Lock(&HeapCriticalSection);
	for (FRHITransientHeap* Heap : Heaps)
	{
		delete Heap;
	}
	Heaps.Empty();
}

void FRHITransientResourceSystem::UpdateStats()
{
	FStats Stats;
	Stats.Textures = TextureStats;
	Stats.Buffers = BufferStats;

	uint32 HeapCount;

	{
		FScopeLock Lock(&HeapCriticalSection);
		for (FRHITransientHeap* Heap : Heaps)
		{
			Stats.TotalMemoryUsed += Heap->GetCapacity();
		}
		HeapCount = Heaps.Num();
	}

	ReportStats(Stats);

	SET_MEMORY_STAT(STAT_RHITransientMemoryAllocated, Stats.TotalMemoryUsed);
	SET_MEMORY_STAT(STAT_RHITransientMemoryRequested, Stats.Textures.TotalSize + Stats.Buffers.TotalSize);
	SET_MEMORY_STAT(STAT_RHITransientMemoryUsed, Stats.Textures.TotalSizeWithAliasing + Stats.Buffers.TotalSizeWithAliasing);
	SET_MEMORY_STAT(STAT_RHITransientBufferMemoryRequested, Stats.Buffers.TotalSize);
	SET_MEMORY_STAT(STAT_RHITransientBufferMemoryUsed, Stats.Buffers.TotalSizeWithAliasing);
	SET_MEMORY_STAT(STAT_RHITransientTextureMemoryRequested, Stats.Textures.TotalSize);
	SET_MEMORY_STAT(STAT_RHITransientTextureMemoryUsed, Stats.Textures.TotalSizeWithAliasing);

	SET_DWORD_STAT(STAT_RHITransientTextures, Stats.Textures.AllocationCount);
	SET_DWORD_STAT(STAT_RHITransientBuffers, Stats.Buffers.AllocationCount);
	SET_DWORD_STAT(STAT_RHITransientResources, Stats.Textures.AllocationCount + Stats.Buffers.AllocationCount);
	SET_DWORD_STAT(STAT_RHITransientHeaps, HeapCount);

	TextureStats = {};
	BufferStats = {};
}

//////////////////////////////////////////////////////////////////////////

FRHITransientHeapAllocator::FRHITransientHeapAllocator(const FRHITransientHeapInitializer& InInitializer, uint32 InHeapIndex)
	: Initializer(InInitializer)
	, HeapIndex(InHeapIndex)
{
	HeadHandle = CreateRange();
	InsertRange(HeadHandle, 0, Initializer.Size);
}

FRHITransientHeapAllocator::~FRHITransientHeapAllocator()
{
#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	checkf(AllocationCount == 0, TEXT("%d allocations remain on heap."), AllocationCount);
	check(HeadHandle != InvalidRangeHandle);

	FRangeHandle FirstFreeHandle = GetFirstFreeRangeHandle();
	check(FirstFreeHandle != InvalidRangeHandle);

	const FRange& FirstFreeRange = Ranges[FirstFreeHandle];
	check(FirstFreeRange.NextFreeHandle == InvalidRangeHandle);
	check(FirstFreeRange.Offset == 0);
	check(FirstFreeRange.Size == GetCapacity());
#endif
}

FRHITransientHeapAllocation FRHITransientHeapAllocator::Allocate(uint64 Size, uint32 Alignment)
{
	check(Size > 0);

	Alignment = FMath::Max(Alignment, Initializer.Alignment);

	FFindResult FindResult = FindFreeRange(Size, Alignment);

	if (FindResult.FoundHandle == InvalidRangeHandle)
	{
		return {};
	}

	FRange& FoundRange = Ranges[FindResult.FoundHandle];

	const uint64 AlignedSize   = FoundRange.Size   - FindResult.LeftoverSize;
	const uint64 AlignmentPad  = AlignedSize       - Size;
	const uint64 AlignedOffset = FoundRange.Offset + AlignmentPad;
	const uint64 AllocationEnd = AlignedOffset     + Size;

	// Adjust the range if there is space left over.
	if (FindResult.LeftoverSize)
	{
		FoundRange.Offset = AllocationEnd;
		FoundRange.Size  = FindResult.LeftoverSize;
	}
	// Otherwise, remove it.
	else
	{
		RemoveRange(FindResult.PreviousHandle, FindResult.FoundHandle);
	}

	AllocationCount++;
	UsedSize       += AlignedSize;
	AlignmentWaste += AlignmentPad;

	FRHITransientHeapAllocation Allocation;
	Allocation.Size         = Size;
	Allocation.Offset       = AlignedOffset;
	Allocation.AlignmentPad = AlignmentPad;
	Allocation.HeapIndex    = HeapIndex;

	Validate();

	return Allocation;
}

void FRHITransientHeapAllocator::Deallocate(FRHITransientHeapAllocation Allocation)
{
	check(Allocation.Size > 0 && Allocation.Size <= UsedSize);

	// Reconstruct the original range offset by subtracting the alignment pad, and expand the size accordingly.
	const uint64 RangeToFreeOffset = Allocation.Offset - Allocation.AlignmentPad;
	const uint64 RangeToFreeSize   = Allocation.Size   + Allocation.AlignmentPad;
	const uint64 RangeToFreeEnd    = RangeToFreeOffset + RangeToFreeSize;

	FRangeHandle PreviousHandle = HeadHandle;
	FRangeHandle NextHandle     = InvalidRangeHandle;
	FRangeHandle Handle         = GetFirstFreeRangeHandle();

	while (Handle != InvalidRangeHandle)
	{
		const FRange& Range = Ranges[Handle];

		// Find the first free range after the one being freed.
		if (RangeToFreeOffset < Range.Offset)
		{
			NextHandle = Handle;
			break;
		}

		PreviousHandle = Handle;
		Handle         = Range.NextFreeHandle;
	}

	uint64 MergedFreeRangeStart = RangeToFreeOffset;
	uint64 MergedFreeRangeEnd   = RangeToFreeEnd;
	bool bMergedPrevious        = false;
	bool bMergedNext            = false;

	if (PreviousHandle != HeadHandle)
	{
		FRange& PreviousRange = Ranges[PreviousHandle];

		// Attempt to merge the previous range with the range being freed.
		if (PreviousRange.GetEnd() == RangeToFreeOffset)
		{
			PreviousRange.Size   += RangeToFreeSize;
			MergedFreeRangeStart  = PreviousRange.Offset;
			MergedFreeRangeEnd    = PreviousRange.GetEnd();
			bMergedPrevious       = true;
		}
	}

	if (NextHandle != InvalidRangeHandle)
	{
		FRange& NextRange = Ranges[NextHandle];

		// Attempt to merge the next range with the range being freed.
		if (RangeToFreeEnd == NextRange.Offset)
		{
			NextRange.Size      += RangeToFreeSize;
			NextRange.Offset     = RangeToFreeOffset;
			MergedFreeRangeStart = FMath::Min(MergedFreeRangeStart, RangeToFreeOffset);
			MergedFreeRangeEnd   = NextRange.GetEnd();
			bMergedNext          = true;
		}
	}

	// With both previous and next ranges merged with the freed range, they now overlap. Remove next and expand previous to cover all three.
	if (bMergedPrevious && bMergedNext)
	{
		FRange& PreviousRange = Ranges[PreviousHandle];
		FRange& NextRange     = Ranges[NextHandle];

		PreviousRange.Size = MergedFreeRangeEnd - MergedFreeRangeStart;
		RemoveRange(PreviousHandle, NextHandle);
	}
	// If neither previous or next were merged, insert a new range between them.
	else if (!bMergedPrevious && !bMergedNext)
	{
		InsertRange(PreviousHandle, RangeToFreeOffset, RangeToFreeSize);
	}

	UsedSize       -= RangeToFreeSize;
	AlignmentWaste -= Allocation.AlignmentPad;
	AllocationCount--;

	Validate();
}

FRHITransientHeapAllocator::FFindResult FRHITransientHeapAllocator::FindFreeRange(uint64 Size, uint32 Alignment)
{
	FFindResult FindResult;
	FindResult.PreviousHandle = HeadHandle;

	FRangeHandle Handle = GetFirstFreeRangeHandle();
	while (Handle != InvalidRangeHandle)
	{
		FRange& Range = Ranges[Handle];

		// Due to alignment we may have to shift the offset and expand the size accordingly.
		const uint64 AlignmentPad = Align(Range.Offset, Alignment) - Range.Offset;
		const uint64 RequiredSize = Size + AlignmentPad;

		if (RequiredSize <= Range.Size)
		{
			FindResult.FoundHandle  = Handle;
			FindResult.LeftoverSize = Range.Size - RequiredSize;
			return FindResult;
		}

		FindResult.PreviousHandle = Handle;
		Handle                    = Range.NextFreeHandle;
	}

	return {};
}

void FRHITransientHeapAllocator::Validate()
{
#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	uint64 DerivedFreeSize = 0;

	FRangeHandle PreviousHandle = HeadHandle;
	FRangeHandle NextHandle     = InvalidRangeHandle;
	FRangeHandle Handle         = GetFirstFreeRangeHandle();

	while (Handle != InvalidRangeHandle)
	{
		const FRange& Range = Ranges[Handle];
		DerivedFreeSize    += Range.Size;

		if (PreviousHandle != HeadHandle)
		{
			const FRange& PreviousRange = Ranges[PreviousHandle];

			// Checks that the ranges are sorted.
			check(PreviousRange.Offset + PreviousRange.Size < Range.Offset);
		}

		PreviousHandle = Handle;
		Handle         = Range.NextFreeHandle;
	}

	check(Initializer.Size == DerivedFreeSize + UsedSize);
#endif
}

//////////////////////////////////////////////////////////////////////////

void FRHITransientResourceOverlapTracker::Track(FRHITransientResource* TransientResource, uint64 OffsetMin, uint64 OffsetMax)
{
	check(TransientResource);

	FResourceRange ResourceRangeNew;
	ResourceRangeNew.Resource  = TransientResource;
	ResourceRangeNew.OffsetMin = OffsetMin;
	ResourceRangeNew.OffsetMax = OffsetMax;

	for (int32 Index = 0; Index < ResourceRanges.Num(); ++Index)
	{
		FResourceRange& ResourceRangeOld = ResourceRanges[Index];

		// If the old range starts later in the heap and doesn't overlap, the sort invariant guarantees no future range will overlap.
		if (ResourceRangeOld.OffsetMin >= ResourceRangeNew.OffsetMin)
		{
			break;
		}

		// If the old range starts earlier in the heap and doesn't overlap, we keep searching.
		if (ResourceRangeOld.OffsetMax <= ResourceRangeNew.OffsetMin)
		{
			continue;
		}

		TransientResource->AddAliasingOverlap(ResourceRangeOld.Resource);

		// Complete overlap.
		if (ResourceRangeOld.OffsetMin >= ResourceRangeNew.OffsetMin && ResourceRangeOld.OffsetMax <= ResourceRangeNew.OffsetMax)
		{
			ResourceRanges.RemoveAt(Index);
			Index--;
		}
		// Partial overlap, can manifest as three cases:
		else
		{
			bool bResizedOld = false;

			// 1) New:    ********
			//            |||        ->
			//    Old: ======             ===********
			if (ResourceRangeOld.OffsetMin < ResourceRangeNew.OffsetMin)
			{
				ResourceRangeOld.OffsetMax = ResourceRangeNew.OffsetMin;
				bResizedOld = true;
			}

			if (ResourceRangeOld.OffsetMax > ResourceRangeNew.OffsetMax)
			{
				// 2) New:    ********
				//                |||      ->
				//    Old:        ======         ********===
				if (!bResizedOld)
				{
					ResourceRangeOld.OffsetMin = ResourceRangeNew.OffsetMax;
				}

				// 3) New:    ********
				//            ||||||||      ->
				//    Old: ==============        ===********===
				else
				{
					// Lower bound has been resized already; add an upper bound.
					FResourceRange ResourceRangeOldUpper = ResourceRangeOld;
					ResourceRangeOldUpper.OffsetMin = ResourceRangeNew.OffsetMax;
					Index = ResourceRanges.Insert(ResourceRangeOldUpper, Index + 1);
				}
			}
		}
	}

	for (int32 Index = 0; Index < ResourceRanges.Num(); ++Index)
	{
		const FResourceRange& ResourceRangeOld = ResourceRanges[Index];

		// Insert new range while preserving order.
		if (ResourceRangeOld.OffsetMin > ResourceRangeNew.OffsetMin)
		{
			ResourceRanges.Insert(ResourceRangeNew, Index);
			return;
		}
	}

	ResourceRanges.Add(ResourceRangeNew);
}

//////////////////////////////////////////////////////////////////////////

FRHITransientResourceAllocator::FRHITransientResourceAllocator(FRHITransientResourceSystem& ParentInSystem)
	: ParentSystem(ParentInSystem)
{
	Heaps.Reserve(ParentSystem.GetHeapCount());
	HeapAllocators.Reserve(ParentSystem.GetHeapCount());
}

FRHITransientTexture* FRHITransientResourceAllocator::CreateTexture(
	const FRHITextureCreateInfo& CreateInfo,
	const TCHAR* DebugName,
	uint64 TextureSize,
	uint32 TextureAlignment,
	FCreateTextureFunction CreateTextureFunction)
{
	if (TextureSize > ParentSystem.GetMaximumHeapSize())
	{
		return {};
	}

	const ERHITransientHeapFlags TextureHeapFlags =
		EnumHasAnyFlags(CreateInfo.Flags, TexCreate_RenderTargetable | TexCreate_ResolveTargetable | TexCreate_DepthStencilTargetable | TexCreate_DepthStencilResolveTarget)
		? ERHITransientHeapFlags::AllowRenderTargets
		: ERHITransientHeapFlags::AllowTextures;

	const FRHITransientHeapAllocation Allocation = Allocate(TextureStats, TextureSize, TextureAlignment, TextureHeapFlags);

	FRHITransientHeap* Heap = Heaps[Allocation.HeapIndex];

	FRHITransientTexture* TransientTexture = Heap->AcquireTexture(CreateInfo, Allocation.Offset, [&] (uint64 Hash)
	{
		const FResourceInitializer ResourceInitializer(*Heap, Allocation, Hash);
		return CreateTextureFunction(ResourceInitializer);
	});

	InitResource(TransientTexture, Allocation, DebugName);

#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	DebugTextures.Emplace(TransientTexture);
	check(TransientTexture->GetCreateInfo() == CreateInfo && TransientTexture->GetName() == DebugName);
#endif

	return MoveTemp(TransientTexture);
}

FRHITransientBuffer* FRHITransientResourceAllocator::CreateBuffer(
	const FRHIBufferCreateInfo& CreateInfo,
	const TCHAR* DebugName,
	uint32 BufferSize,
	uint32 BufferAlignment,
	FCreateBufferFunction CreateBufferFunction)
{
	if (BufferSize > ParentSystem.GetMaximumHeapSize())
	{
		return {};
	}

	const FRHITransientHeapAllocation Allocation = Allocate(BufferStats, BufferSize, BufferAlignment, ERHITransientHeapFlags::AllowBuffers);

	FRHITransientHeap* Heap = Heaps[Allocation.HeapIndex];

	FRHITransientBuffer* TransientBuffer = Heap->AcquireBuffer(CreateInfo, Allocation.Offset, [&](uint64 Hash)
	{
		const FResourceInitializer ResourceInitializer(*Heap, Allocation, Hash);
		return CreateBufferFunction(ResourceInitializer);
	});

	InitResource(TransientBuffer, Allocation, DebugName);

#if RHICORE_TRANSIENT_ALLOCATOR_DEBUG
	DebugBuffers.Emplace(TransientBuffer);
	check(TransientBuffer->GetCreateInfo() == CreateInfo && TransientBuffer->GetName() == DebugName);
#endif

	return TransientBuffer;
}

void FRHITransientResourceAllocator::DeallocateMemoryInternal(FRHITransientResource* InResource, FMemoryStats& StatsToUpdate)
{
	const FRHITransientHeapAllocation& Allocation = HeapAllocations[InResource->GetAllocationIndex()];

	HeapAllocators[Allocation.HeapIndex].Deallocate(Allocation);

	StatsToUpdate.CurrentSizeWithAliasing -= Allocation.Size;
}

void FRHITransientResourceAllocator::Freeze(FRHICommandListImmediate& RHICmdList)
{
	ParentSystem.ForfeitHeaps(Heaps);

	RHICmdList.EnqueueLambda([&ParentSystem = ParentSystem, BufferStats = BufferStats, TextureStats = TextureStats](FRHICommandListImmediate&)
	{
		ParentSystem.TextureStats.Add(TextureStats);
		ParentSystem.BufferStats.Add(BufferStats);
	});
}

FRHITransientHeapAllocation FRHITransientResourceAllocator::Allocate(FMemoryStats& StatsToUpdate, uint64 Size, uint32 Alignment, ERHITransientHeapFlags ResourceHeapFlags)
{
	FRHITransientHeapAllocation Allocation;

	for (int32 Index = 0; Index < HeapAllocators.Num(); ++Index)
	{
		FRHITransientHeapAllocator& HeapAllocator = HeapAllocators[Index];

		if (!HeapAllocator.IsAllocationSupported(Size, ResourceHeapFlags))
		{
			continue;
		}

		Allocation = HeapAllocator.Allocate(Size, Alignment);

		if (Allocation.IsValid())
		{
			break;
		}
	}

	if (!Allocation.IsValid())
	{
		const uint32 HeapIndex = Heaps.Num();

		FRHITransientHeap* Heap = ParentSystem.AcquireHeap(Size, ResourceHeapFlags);
		Heaps.Emplace(Heap);

		FRHITransientHeapAllocator& HeapAllocator = HeapAllocators.Emplace_GetRef(Heap->GetInitializer(), HeapIndex);
		Allocation = HeapAllocator.Allocate(Size, Alignment);

		check(Allocation.IsValid());
	}

	StatsToUpdate.AllocationCount++;
	StatsToUpdate.TotalSize += Allocation.Size;
	StatsToUpdate.CurrentSizeWithAliasing += Allocation.Size;
	StatsToUpdate.TotalSizeWithAliasing = FMath::Max(StatsToUpdate.TotalSizeWithAliasing, StatsToUpdate.CurrentSizeWithAliasing);

	return Allocation;
}

void FRHITransientResourceAllocator::InitResource(FRHITransientResource* TransientResource, const FRHITransientHeapAllocation& Allocation, const TCHAR* Name)
{
	TransientResource->Init(Name, HeapAllocations.Emplace(Allocation));
	HeapAllocators[Allocation.HeapIndex].TrackOverlap(TransientResource, Allocation);
}