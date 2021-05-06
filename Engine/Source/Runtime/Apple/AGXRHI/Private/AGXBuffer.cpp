// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXBuffer.h"
#include "Templates/AlignmentTemplates.h"
#include "AGXLLM.h"
#include <objc/runtime.h>
#include "AGXCommandBuffer.h"
#include "AGXProfiler.h"
#include "AGXRenderPass.h"

DECLARE_MEMORY_STAT(TEXT("Used Device Buffer Memory"), STAT_AGXDeviceBufferMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Used Pooled Buffer Memory"), STAT_AGXPooledBufferMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Used Magazine Buffer Memory"), STAT_AGXMagazineBufferMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Used Heap Buffer Memory"), STAT_AGXHeapBufferMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Used Linear Buffer Memory"), STAT_AGXLinearBufferMemory, STATGROUP_AGXRHI);

DECLARE_MEMORY_STAT(TEXT("Unused Pooled Buffer Memory"), STAT_AGXPooledBufferUnusedMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Unused Magazine Buffer Memory"), STAT_AGXMagazineBufferUnusedMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Unused Heap Buffer Memory"), STAT_AGXHeapBufferUnusedMemory, STATGROUP_AGXRHI);
DECLARE_MEMORY_STAT(TEXT("Unused Linear Buffer Memory"), STAT_AGXLinearBufferUnusedMemory, STATGROUP_AGXRHI);

static int32 GAGXHeapBufferBytesToCompact = 0;
static FAutoConsoleVariableRef CVarAGXHeapBufferBytesToCompact(
    TEXT("rhi.AGX.HeapBufferBytesToCompact"),
    GAGXHeapBufferBytesToCompact,
    TEXT("When enabled (> 0) this will force AGXRHI to compact the given number of bytes each frame into older buffer heaps from newer ones in order to defragment memory and reduce wastage.\n")
    TEXT("(Off by default (0))"));

static int32 GAGXResourcePurgeInPool = 0;
static FAutoConsoleVariableRef CVarAGXResourcePurgeInPool(
	TEXT("rhi.AGX.ResourcePurgeInPool"),
	GAGXResourcePurgeInPool,
	TEXT("Use the SetPurgeableState function to allow the OS to reclaim memory from resources while they are unused in the pools. (Default: 0, Off)"));

#if METAL_DEBUG_OPTIONS
extern int32 GAGXBufferScribble;
#endif

FAGXBuffer::~FAGXBuffer()
{
}

FAGXBuffer::FAGXBuffer(ns::Protocol<id<MTLBuffer>>::type handle, ns::Ownership retain)
: mtlpp::Buffer(handle, nullptr, retain)
, Heap(nullptr)
, Linear(nullptr)
, Magazine(nullptr)
, bPooled(false)
, bSingleUse(false)
{
}

FAGXBuffer::FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferHeap* heap)
: mtlpp::Buffer((mtlpp::Buffer&&)rhs)
, Heap(heap)
, Linear(nullptr)
, Magazine(nullptr)
, bPooled(false)
, bSingleUse(false)
{
}


FAGXBuffer::FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferLinear* heap)
: mtlpp::Buffer((mtlpp::Buffer&&)rhs)
, Heap(nullptr)
, Linear(heap)
, Magazine(nullptr)
, bPooled(false)
, bSingleUse(false)
{
}

FAGXBuffer::FAGXBuffer(mtlpp::Buffer&& rhs, FAGXSubBufferMagazine* magazine)
: mtlpp::Buffer((mtlpp::Buffer&&)rhs)
, Heap(nullptr)
, Linear(nullptr)
, Magazine(magazine)
, bPooled(false)
, bSingleUse(false)
{
}

FAGXBuffer::FAGXBuffer(mtlpp::Buffer&& rhs, bool bInPooled)
: mtlpp::Buffer((mtlpp::Buffer&&)rhs)
, Heap(nullptr)
, Linear(nullptr)
, Magazine(nullptr)
, bPooled(bInPooled)
, bSingleUse(false)
{
}

FAGXBuffer::FAGXBuffer(const FAGXBuffer& rhs)
: mtlpp::Buffer(rhs)
, Heap(rhs.Heap)
, Linear(rhs.Linear)
, Magazine(rhs.Magazine)
, bPooled(rhs.bPooled)
, bSingleUse(false)
{
}

FAGXBuffer::FAGXBuffer(FAGXBuffer&& rhs)
: mtlpp::Buffer((mtlpp::Buffer&&)rhs)
, Heap(rhs.Heap)
, Linear(rhs.Linear)
, Magazine(rhs.Magazine)
, bPooled(rhs.bPooled)
, bSingleUse(false)
{
}

FAGXBuffer& FAGXBuffer::operator=(const FAGXBuffer& rhs)
{
	if(this != &rhs)
	{
		mtlpp::Buffer::operator=(rhs);
        Heap = rhs.Heap;
		Linear = rhs.Linear;
		Magazine = rhs.Magazine;
		bPooled = rhs.bPooled;
		bSingleUse = rhs.bSingleUse;
	}
	return *this;
}

FAGXBuffer& FAGXBuffer::operator=(FAGXBuffer&& rhs)
{
	mtlpp::Buffer::operator=((mtlpp::Buffer&&)rhs);
	Heap = rhs.Heap;
	Linear = rhs.Linear;
	Magazine = rhs.Magazine;
	bPooled = rhs.bPooled;
	bSingleUse = rhs.bSingleUse;
	return *this;
}

void FAGXBuffer::Release()
{
	if (Heap)
	{
		Heap->FreeRange(ns::Range(GetOffset(), GetLength()));
		Heap = nullptr;
	}
	else if (Linear)
	{
		Linear->FreeRange(ns::Range(GetOffset(), GetLength()));
		Linear = nullptr;
	}
	else if (Magazine)
	{
		Magazine->FreeRange(ns::Range(GetOffset(), GetLength()));
		Magazine = nullptr;
	}
}

void FAGXBuffer::SetOwner(class FAGXRHIBuffer* Owner, bool bIsSwap)
{
	check(Owner == nullptr);
	if (Heap)
	{
		Heap->SetOwner(ns::Range(GetOffset(), GetLength()), Owner, bIsSwap);
	}
}

FAGXSubBufferHeap::FAGXSubBufferHeap(NSUInteger Size, NSUInteger Alignment, mtlpp::ResourceOptions Options, FCriticalSection& InPoolMutex)
: PoolMutex(InPoolMutex)
, OutstandingAllocs(0)
, MinAlign(Alignment)
, UsedSize(0)
{
	Options = (mtlpp::ResourceOptions)FAGXCommandQueue::GetCompatibleResourceOptions(Options);
	static bool bSupportsHeaps = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesHeaps);
	NSUInteger FullSize = Align(Size, Alignment);
	METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), FullSize, Options)));
	
	mtlpp::StorageMode Storage = (mtlpp::StorageMode)((Options & mtlpp::ResourceStorageModeMask) >> mtlpp::ResourceStorageModeShift);
#if PLATFORM_MAC
	check(Storage != mtlpp::StorageMode::Managed /* Managed memory cannot be safely suballocated! When you overwrite existing data the GPU buffer is immediately disposed of! */);
#endif

	if (bSupportsHeaps && Storage == mtlpp::StorageMode::Private)
	{
		mtlpp::HeapDescriptor Desc;
		Desc.SetSize(FullSize);
		Desc.SetStorageMode(Storage);
		ParentHeap = GetAGXDeviceContext().GetDevice().NewHeap(Desc);
		check(ParentHeap.GetPtr());
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocHeap(GetAGXDeviceContext().GetDevice(), ParentHeap);
#endif
	}
	else
	{
		ParentBuffer = MTLPP_VALIDATE(mtlpp::Device, GetAGXDeviceContext().GetDevice(), AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(FullSize, Options));
		check(ParentBuffer.GetPtr());
		check(ParentBuffer.GetLength() >= FullSize);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocBuffer(GetAGXDeviceContext().GetDevice(), ParentBuffer);
#endif
		FreeRanges.Add(ns::Range(0, FullSize));
	}
	INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, FullSize);
	INC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, FullSize);
}

FAGXSubBufferHeap::~FAGXSubBufferHeap()
{
	if (ParentHeap)
	{
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, ParentHeap.GetSize());
		DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, ParentHeap.GetSize());
	}
	else
	{
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, ParentBuffer.GetLength());
		DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, ParentBuffer.GetLength());
	}
}

void FAGXSubBufferHeap::SetOwner(ns::Range const& Range, FAGXRHIBuffer* Owner, bool bIsSwap)
{
	check(Owner == nullptr);
	FScopeLock Lock(&PoolMutex);
	for (uint32 i = 0; i < AllocRanges.Num(); i++)
	{
		if (AllocRanges[i].Range.Location == Range.Location)
		{
			check(AllocRanges[i].Range.Length == Range.Length);
			check(AllocRanges[i].Owner == nullptr || Owner == nullptr || bIsSwap);
			AllocRanges[i].Owner = Owner;
			break;
		}
	}
}

void FAGXSubBufferHeap::FreeRange(ns::Range const& Range)
{
	FPlatformAtomics::InterlockedDecrement(&OutstandingAllocs);
	{
		FScopeLock Lock(&PoolMutex);
		for (uint32 i = 0; i < AllocRanges.Num(); i++)
		{
			if (AllocRanges[i].Range.Location == Range.Location)
			{
				check(AllocRanges[i].Range.Length == Range.Length);
				AllocRanges.RemoveAt(i);
				break;
			}
		}
	}
	if (ParentHeap)
	{
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
		INC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, Range.Length);
		DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferMemory, Range.Length);
	}
	else
	{
#if METAL_DEBUG_OPTIONS
		if (GIsRHIInitialized)
		{
			MTLPP_VALIDATE_ONLY(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ReleaseRange(Range));
			FAGXBuffer Buf(ParentBuffer.NewBuffer(Range), false);
			GetAGXDeviceContext().ValidateIsInactiveBuffer(Buf);
		}
#endif
    
		FScopeLock Lock(&PoolMutex);
		{
			ns::Range CompactRange = Range;
			for (uint32 i = 0; i < FreeRanges.Num(); )
			{
				if (FreeRanges[i].Location == (CompactRange.Location + CompactRange.Length))
				{
					ns::Range PrevRange = FreeRanges[i];
					FreeRanges.RemoveAt(i);
					
					CompactRange.Length += PrevRange.Length;
				}
				else if (CompactRange.Location == (FreeRanges[i].Location + FreeRanges[i].Length))
				{
					ns::Range PrevRange = FreeRanges[i];
					FreeRanges.RemoveAt(i);
					
					CompactRange.Location = PrevRange.Location;
					CompactRange.Length += PrevRange.Length;
				}
				else
				{
					i++;
				}
			}
		
			uint32 i = 0;
			for (; i < FreeRanges.Num(); i++)
			{
				if (FreeRanges[i].Length >= CompactRange.Length)
				{
					break;
				}
			}
			FreeRanges.Insert(CompactRange, i);
		
			UsedSize -= Range.Length;
			
			INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
			INC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, Range.Length);
			DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferMemory, Range.Length);
		
#if METAL_DEBUG_OPTIONS
			uint64 LostSize = GetSize() - UsedSize;
			for (ns::Range const& FreeRange : FreeRanges)
			{
				LostSize -= FreeRange.Length;
			}
			check(LostSize == 0);
#endif
		}
	}
}

ns::String FAGXSubBufferHeap::GetLabel() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetLabel();
	}
	else
	{
		return ParentBuffer.GetLabel();
	}
}

mtlpp::Device FAGXSubBufferHeap::GetDevice() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetDevice();
	}
	else
	{
		return ParentBuffer.GetDevice();
	}
}

mtlpp::StorageMode FAGXSubBufferHeap::GetStorageMode() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetStorageMode();
	}
	else
	{
		return ParentBuffer.GetStorageMode();
	}
}

mtlpp::CpuCacheMode FAGXSubBufferHeap::GetCpuCacheMode() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetCpuCacheMode();
	}
	else
	{
		return ParentBuffer.GetCpuCacheMode();
	}
}

NSUInteger FAGXSubBufferHeap::GetSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetSize();
	}
	else
	{
		return ParentBuffer.GetLength();
	}
}

NSUInteger FAGXSubBufferHeap::GetUsedSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetUsedSize();
	}
	else
	{
		return UsedSize;
	}
}

int64 FAGXSubBufferHeap::NumCurrentAllocations() const
{
	return OutstandingAllocs;
}

void FAGXSubBufferHeap::SetLabel(const ns::String& label)
{
	if (ParentHeap)
	{
		ParentHeap.SetLabel(label);
	}
	else
	{
		ParentBuffer.SetLabel(label);
	}
}

NSUInteger FAGXSubBufferHeap::MaxAvailableSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.MaxAvailableSizeWithAlignment(MinAlign);
	}
	else
	{
		if (UsedSize < GetSize())
		{
			return FreeRanges.Last().Length;
		}
		else
		{
			return 0;
		}
	}
}

bool FAGXSubBufferHeap::CanAllocateSize(NSUInteger Size) const
{
	if (ParentHeap)
	{
		NSUInteger Storage = (NSUInteger(GetStorageMode()) << mtlpp::ResourceStorageModeShift);
		NSUInteger Cache = (NSUInteger(GetCpuCacheMode()) << mtlpp::ResourceCpuCacheModeShift);
		mtlpp::ResourceOptions Opt = mtlpp::ResourceOptions(Storage | Cache);
		
		NSUInteger Align = ParentHeap.GetDevice().HeapBufferSizeAndAlign(Size, Opt).Align;
		return Size <= ParentHeap.MaxAvailableSizeWithAlignment(Align);
	}
	else
	{
		return Size <= MaxAvailableSize();
	}
}

FAGXBuffer FAGXSubBufferHeap::NewBuffer(NSUInteger length)
{
	NSUInteger Size = Align(length, MinAlign);
	FAGXBuffer Result;
	
	if (ParentHeap)
	{
		NSUInteger Storage = (NSUInteger(GetStorageMode()) << mtlpp::ResourceStorageModeShift);
		NSUInteger Cache = (NSUInteger(GetCpuCacheMode()) << mtlpp::ResourceCpuCacheModeShift);
		mtlpp::ResourceOptions Opt = mtlpp::ResourceOptions(Storage | Cache);
		
		Result = FAGXBuffer(ParentHeap.NewBuffer(Size, Opt), this);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocBuffer(GetAGXDeviceContext().GetDevice(), Result);
#endif
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Result.GetLength());
		DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, Result.GetLength());
		INC_MEMORY_STAT_BY(STAT_AGXHeapBufferMemory, Result.GetLength());
	}
	else
	{
		check(ParentBuffer && ParentBuffer.GetPtr());
	
		FScopeLock Lock(&PoolMutex);
		if (MaxAvailableSize() >= Size)
		{
			for (uint32 i = 0; i < FreeRanges.Num(); i++)
			{
				if (FreeRanges[i].Length >= Size)
				{
					ns::Range Range = FreeRanges[i];
					FreeRanges.RemoveAt(i);
					
					UsedSize += Range.Length;
					
					DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
					DEC_MEMORY_STAT_BY(STAT_AGXHeapBufferUnusedMemory, Range.Length);
					INC_MEMORY_STAT_BY(STAT_AGXHeapBufferMemory, Range.Length);
				
					if (Range.Length > Size)
					{
						ns::Range Split = ns::Range(Range.Location + Size, Range.Length - Size);
						FPlatformAtomics::InterlockedIncrement(&OutstandingAllocs);
						FreeRange(Split);
						
						Range.Length = Size;
					}
					
#if METAL_DEBUG_OPTIONS
					uint64 LostSize = GetSize() - UsedSize;
					for (ns::Range const& FreeRange : FreeRanges)
					{
						LostSize -= FreeRange.Length;
					}
					check(LostSize == 0);
#endif
					
					Result = FAGXBuffer(MTLPP_VALIDATE(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(Range)), this);
				
                    Allocation Alloc;
                    Alloc.Range = Range;
                    Alloc.Resource = Result.GetPtr();
                    Alloc.Owner = nullptr;
                    AllocRanges.Add(Alloc);

					break;
				}
			}
		}
	}
	FPlatformAtomics::InterlockedIncrement(&OutstandingAllocs);
	check(Result && Result.GetPtr());
	return Result;
}

mtlpp::PurgeableState FAGXSubBufferHeap::SetPurgeableState(mtlpp::PurgeableState state)
{
	if (ParentHeap)
	{
		return ParentHeap.SetPurgeableState(state);
	}
	else
	{
		return ParentBuffer.SetPurgeableState(state);
	}
}

#pragma mark --

FAGXSubBufferLinear::FAGXSubBufferLinear(NSUInteger Size, NSUInteger Alignment, mtlpp::ResourceOptions Options, FCriticalSection& InPoolMutex)
: PoolMutex(InPoolMutex)
, MinAlign(Alignment)
, WriteHead(0)
, UsedSize(0)
, FreedSize(0)
{
	Options = (mtlpp::ResourceOptions)FAGXCommandQueue::GetCompatibleResourceOptions(Options);
	NSUInteger FullSize = Align(Size, Alignment);
	METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), FullSize, Options)));
	
	mtlpp::StorageMode Storage = (mtlpp::StorageMode)((Options & mtlpp::ResourceStorageModeMask) >> mtlpp::ResourceStorageModeShift);
	ParentBuffer = MTLPP_VALIDATE(mtlpp::Device, GetAGXDeviceContext().GetDevice(), AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(FullSize, Options));
	check(ParentBuffer.GetPtr());
	check(ParentBuffer.GetLength() >= FullSize);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
	AGXLLM::LogAllocBuffer(GetAGXDeviceContext().GetDevice(), ParentBuffer);
#endif
	INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, FullSize);
	INC_MEMORY_STAT_BY(STAT_AGXLinearBufferUnusedMemory, FullSize);
}

FAGXSubBufferLinear::~FAGXSubBufferLinear()
{
	DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, ParentBuffer.GetLength());
	DEC_MEMORY_STAT_BY(STAT_AGXLinearBufferUnusedMemory, ParentBuffer.GetLength());
}

void FAGXSubBufferLinear::FreeRange(ns::Range const& Range)
{
#if METAL_DEBUG_OPTIONS
	if (GIsRHIInitialized)
	{
		MTLPP_VALIDATE_ONLY(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ReleaseRange(Range));
		FAGXBuffer Buf(ParentBuffer.NewBuffer(Range), false);
		GetAGXDeviceContext().ValidateIsInactiveBuffer(Buf);
	}
#endif
	
	FScopeLock Lock(&PoolMutex);
	{
		FreedSize += Range.Length;
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
		INC_MEMORY_STAT_BY(STAT_AGXLinearBufferUnusedMemory, Range.Length);
		DEC_MEMORY_STAT_BY(STAT_AGXLinearBufferMemory, Range.Length);
		if (FreedSize == UsedSize)
		{
			UsedSize = 0;
			FreedSize = 0;
			WriteHead = 0;
		}
	}
}

ns::String FAGXSubBufferLinear::GetLabel() const
{
	return ParentBuffer.GetLabel();
}

mtlpp::Device FAGXSubBufferLinear::GetDevice() const
{
	return ParentBuffer.GetDevice();
}

mtlpp::StorageMode FAGXSubBufferLinear::GetStorageMode() const
{
	return ParentBuffer.GetStorageMode();
}

mtlpp::CpuCacheMode FAGXSubBufferLinear::GetCpuCacheMode() const
{
	return ParentBuffer.GetCpuCacheMode();
}

NSUInteger FAGXSubBufferLinear::GetSize() const
{
	return ParentBuffer.GetLength();
}

NSUInteger FAGXSubBufferLinear::GetUsedSize() const
{
	return UsedSize;
}

void FAGXSubBufferLinear::SetLabel(const ns::String& label)
{
	ParentBuffer.SetLabel(label);
}

bool FAGXSubBufferLinear::CanAllocateSize(NSUInteger Size) const
{
	if (WriteHead < GetSize())
	{
		NSUInteger Alignment = FMath::Max(NSUInteger(MinAlign), NSUInteger(Size & ~(Size - 1llu)));
		NSUInteger NewWriteHead = Align(WriteHead, Alignment);
		return (GetSize() - NewWriteHead) > Size;
	}
	else
	{
		return 0;
	}
}

FAGXBuffer FAGXSubBufferLinear::NewBuffer(NSUInteger length)
{
	FScopeLock Lock(&PoolMutex);
	NSUInteger Alignment = FMath::Max(NSUInteger(MinAlign), NSUInteger(length & ~(length - 1llu)));
	NSUInteger Size = Align(length, Alignment);
	NSUInteger NewWriteHead = Align(WriteHead, Alignment);
	
	FAGXBuffer Result;
	if ((GetSize() - NewWriteHead) > Size)
	{
		ns::Range Range(NewWriteHead, Size);
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
		DEC_MEMORY_STAT_BY(STAT_AGXLinearBufferUnusedMemory, Range.Length);
		INC_MEMORY_STAT_BY(STAT_AGXLinearBufferMemory, Range.Length);
		Result = FAGXBuffer(MTLPP_VALIDATE(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(Range)), this);
		UsedSize += Size;
		WriteHead = NewWriteHead + Size;
	}
	
	return Result;
}

mtlpp::PurgeableState FAGXSubBufferLinear::SetPurgeableState(mtlpp::PurgeableState state)
{
	return ParentBuffer.SetPurgeableState(state);
}

#pragma mark --

FAGXSubBufferMagazine::FAGXSubBufferMagazine(NSUInteger Size, NSUInteger ChunkSize, mtlpp::ResourceOptions Options)
: MinAlign(ChunkSize)
, BlockSize(ChunkSize)
, OutstandingAllocs(0)
, UsedSize(0)
{
	Options = (mtlpp::ResourceOptions)FAGXCommandQueue::GetCompatibleResourceOptions(Options);
    static bool bSupportsHeaps = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesHeaps);
    mtlpp::StorageMode Storage = (mtlpp::StorageMode)((Options & mtlpp::ResourceStorageModeMask) >> mtlpp::ResourceStorageModeShift);
    if (PLATFORM_IOS && bSupportsHeaps && Storage == mtlpp::StorageMode::Private)
    {
        MinAlign = GetAGXDeviceContext().GetDevice().HeapBufferSizeAndAlign(BlockSize, Options).Align;
    }
    
    NSUInteger FullSize = Align(Size, MinAlign);
	METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), FullSize, Options)));
	
#if PLATFORM_MAC
	check(Storage != mtlpp::StorageMode::Managed /* Managed memory cannot be safely suballocated! When you overwrite existing data the GPU buffer is immediately disposed of! */);
#endif

	if (bSupportsHeaps && Storage == mtlpp::StorageMode::Private)
	{
		mtlpp::HeapDescriptor Desc;
		Desc.SetSize(FullSize);
		Desc.SetStorageMode(Storage);
		ParentHeap = GetAGXDeviceContext().GetDevice().NewHeap(Desc);
		check(ParentHeap.GetPtr());
		METAL_FATAL_ASSERT(ParentHeap, TEXT("Failed to create heap of size %u and resource options %u"), Size, (uint32)Options);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocHeap(GetAGXDeviceContext().GetDevice(), ParentHeap);
#endif
	}
	else
	{
		ParentBuffer = MTLPP_VALIDATE(mtlpp::Device, GetAGXDeviceContext().GetDevice(), AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(FullSize, Options));
		check(ParentBuffer.GetPtr());
		check(ParentBuffer.GetLength() >= FullSize);
		METAL_FATAL_ASSERT(ParentBuffer, TEXT("Failed to create heap of size %u and resource options %u"), Size, (uint32)Options);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocBuffer(GetAGXDeviceContext().GetDevice(), ParentBuffer);
#endif
		
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, FullSize);
		INC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, FullSize);
        uint32 BlockCount = FullSize / ChunkSize;
        Blocks.AddZeroed(BlockCount);
	}
}

FAGXSubBufferMagazine::~FAGXSubBufferMagazine()
{
	if (ParentHeap)
	{
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, ParentHeap.GetSize());
		DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, ParentHeap.GetSize());
	}
	else
	{
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, ParentBuffer.GetLength());
		DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, ParentBuffer.GetLength());
	}
}

void FAGXSubBufferMagazine::FreeRange(ns::Range const& Range)
{
	FPlatformAtomics::InterlockedDecrement(&OutstandingAllocs);
	if (ParentHeap)
	{
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
		INC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, Range.Length);
		DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferMemory, Range.Length);
	}
	else
	{
#if METAL_DEBUG_OPTIONS
		if (GIsRHIInitialized)
		{
			MTLPP_VALIDATE_ONLY(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ReleaseRange(Range));
			FAGXBuffer Buf(ParentBuffer.NewBuffer(Range), false);
			GetAGXDeviceContext().ValidateIsInactiveBuffer(Buf);
		}
#endif
	
		uint32 BlockIndex = Range.Location / Range.Length;
		FPlatformAtomics::AtomicStore(&Blocks[BlockIndex], 0);
		FPlatformAtomics::InterlockedAdd(&UsedSize, -((int64)Range.Length));
		
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
		INC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, Range.Length);
		DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferMemory, Range.Length);
	}
}

ns::String FAGXSubBufferMagazine::GetLabel() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetLabel();
	}
	else
	{
		return ParentBuffer.GetLabel();
	}
}

mtlpp::Device FAGXSubBufferMagazine::GetDevice() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetDevice();
	}
	else
	{
		return ParentBuffer.GetDevice();
	}
}

mtlpp::StorageMode FAGXSubBufferMagazine::GetStorageMode() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetStorageMode();
	}
	else
	{
		return ParentBuffer.GetStorageMode();
	}
}

mtlpp::CpuCacheMode FAGXSubBufferMagazine::GetCpuCacheMode() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetCpuCacheMode();
	}
	else
	{
		return ParentBuffer.GetCpuCacheMode();
	}
}

NSUInteger FAGXSubBufferMagazine::GetSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetSize();
	}
	else
	{
		return ParentBuffer.GetLength();
	}
}

NSUInteger FAGXSubBufferMagazine::GetUsedSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.GetUsedSize();
	}
	else
	{
		return (NSUInteger)FPlatformAtomics::AtomicRead(&UsedSize);
	}
}

NSUInteger FAGXSubBufferMagazine::GetFreeSize() const
{
	if (ParentHeap)
	{
		return ParentHeap.MaxAvailableSizeWithAlignment(MinAlign);
	}
	else
	{
		return GetSize() - GetUsedSize();
	}
}

int64 FAGXSubBufferMagazine::NumCurrentAllocations() const
{
	return OutstandingAllocs;
}

bool FAGXSubBufferMagazine::CanAllocateSize(NSUInteger Size) const
{
	return GetFreeSize() >= Size;
}

void FAGXSubBufferMagazine::SetLabel(const ns::String& label)
{
	if (ParentHeap)
	{
		ParentHeap.SetLabel(label);
	}
	else
	{
		ParentBuffer.SetLabel(label);
	}
}

FAGXBuffer FAGXSubBufferMagazine::NewBuffer()
{
	NSUInteger Size = BlockSize;
	FAGXBuffer Result;

	if (ParentHeap)
	{
		NSUInteger Storage = (NSUInteger(GetStorageMode()) << mtlpp::ResourceStorageModeShift);
		NSUInteger Cache = (NSUInteger(GetCpuCacheMode()) << mtlpp::ResourceCpuCacheModeShift);
		mtlpp::ResourceOptions Opt = mtlpp::ResourceOptions(Storage | Cache);
		
		Result = FAGXBuffer(ParentHeap.NewBuffer(Size, Opt), this);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocBuffer(GetAGXDeviceContext().GetDevice(), Result);
#endif
		DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Result.GetLength());
		DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, Result.GetLength());
		INC_MEMORY_STAT_BY(STAT_AGXMagazineBufferMemory, Result.GetLength());
	}
	else
	{
		check(ParentBuffer && ParentBuffer.GetPtr());
		
		for (uint32 i = 0; i < Blocks.Num(); i++)
		{
			if (FPlatformAtomics::InterlockedCompareExchange(&Blocks[i], 1, 0) == 0)
			{
				ns::Range Range(i * BlockSize, BlockSize);
				FPlatformAtomics::InterlockedAdd(&UsedSize, ((int64)Range.Length));
				DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Range.Length);
				DEC_MEMORY_STAT_BY(STAT_AGXMagazineBufferUnusedMemory, Range.Length);
				INC_MEMORY_STAT_BY(STAT_AGXMagazineBufferMemory, Range.Length);
				Result = FAGXBuffer(MTLPP_VALIDATE(mtlpp::Buffer, ParentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(Range)), this);
				break;
			}
		}
	}

	FPlatformAtomics::InterlockedIncrement(&OutstandingAllocs);
	check(Result && Result.GetPtr());
	return Result;
}

mtlpp::PurgeableState FAGXSubBufferMagazine::SetPurgeableState(mtlpp::PurgeableState state)
{
	if (ParentHeap)
	{
		return ParentHeap.SetPurgeableState(state);
	}
	else
	{
		return ParentBuffer.SetPurgeableState(state);
	}
}

FAGXRingBufferRef::FAGXRingBufferRef(FAGXBuffer Buf)
: Buffer(Buf)
, LastRead(Buf.GetLength())
{
	Buffer.SetLabel(@"Ring Buffer");
}

FAGXRingBufferRef::~FAGXRingBufferRef()
{
	MTLPP_VALIDATE_ONLY(mtlpp::Buffer, Buffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ReleaseAllRanges());
	AGXSafeReleaseMetalBuffer(Buffer);
}

FAGXSubBufferRing::FAGXSubBufferRing(NSUInteger Size, NSUInteger Alignment, mtlpp::ResourceOptions InOptions)
: LastFrameChange(0)
, InitialSize(Align(Size, Alignment))
, MinAlign(Alignment)
, CommitHead(0)
, SubmitHead(0)
, WriteHead(0)
, BufferSize(0)
, Options(InOptions)
, Storage((mtlpp::StorageMode)((Options & mtlpp::ResourceStorageModeMask) >> mtlpp::ResourceStorageModeShift))
{
	Options = (mtlpp::ResourceOptions)FAGXCommandQueue::GetCompatibleResourceOptions(Options);
	check(Storage != mtlpp::StorageMode::Private /* Private memory requires command-buffers and encoders to properly marshal! */);
	FMemory::Memzero(FrameSize);
}

FAGXSubBufferRing::~FAGXSubBufferRing()
{
}

mtlpp::Device FAGXSubBufferRing::GetDevice() const
{
	return Buffer.IsValid() ? Buffer->Buffer.GetDevice() : nil;
}
mtlpp::StorageMode FAGXSubBufferRing::GetStorageMode() const
{
	return Buffer.IsValid() ? Buffer->Buffer.GetStorageMode() : Storage;
}
mtlpp::CpuCacheMode FAGXSubBufferRing::GetCpuCacheMode() const
{
	return Buffer.IsValid() ? Buffer->Buffer.GetCpuCacheMode() : ((mtlpp::CpuCacheMode)((Options & mtlpp::ResourceCpuCacheModeMask) >> mtlpp::ResourceCpuCacheModeShift));
}
NSUInteger FAGXSubBufferRing::GetSize() const
{
	return Buffer.IsValid() ? Buffer->Buffer.GetLength() : InitialSize;
}

FAGXBuffer FAGXSubBufferRing::NewBuffer(NSUInteger Size, uint32 Alignment)
{
	if (Alignment == 0)
	{
		Alignment = MinAlign;
	}
	else
	{
		Alignment = Align(Alignment, MinAlign);
	}
	
	NSUInteger FullSize = Align(Size, Alignment);
	
	// Allocate on first use
	if(!Buffer.IsValid())
	{
		Buffer = MakeShared<FAGXRingBufferRef, ESPMode::ThreadSafe>(GetAGXDeviceContext().GetResourceHeap().CreateBuffer(InitialSize, MinAlign, BUF_Dynamic, Options, true));
		BufferSize = InitialSize;
	}
	
	if(Buffer->LastRead <= WriteHead)
	{
		if (WriteHead + FullSize <= Buffer->Buffer.GetLength())
		{
			FAGXBuffer NewBuffer(MTLPP_VALIDATE(mtlpp::Buffer, Buffer->Buffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(ns::Range(WriteHead, FullSize))), false);
			
			FMemory::Memset(((uint8*)NewBuffer.GetContents()), 0x0, FullSize);
			
			WriteHead += FullSize;
			// NewBuffer.MarkSingleUse();
			return NewBuffer;
		}
#if PLATFORM_MAC
		else if (Storage == mtlpp::StorageMode::Managed)
		{
			Submit();
			Buffer = MakeShared<FAGXRingBufferRef, ESPMode::ThreadSafe>(GetAGXDeviceContext().GetResourceHeap().CreateBuffer(BufferSize, MinAlign, BUF_Dynamic, Options, true));
			WriteHead = 0;
			CommitHead = 0;
			SubmitHead = 0;
		}
#endif
		else
		{
			WriteHead = 0;
		}
	}
	
	if(WriteHead + FullSize >= Buffer->LastRead || WriteHead + FullSize > BufferSize)
	{
		NSUInteger NewBufferSize = AlignArbitrary(BufferSize + Size, Align(BufferSize / 4, MinAlign));
		
		UE_LOG(LogAGX, Verbose, TEXT("Reallocating ring-buffer from %d to %d to avoid wrapping write at offset %d into outstanding buffer region %d at frame %lld]"), (uint32)BufferSize, (uint32)NewBufferSize, (uint32)WriteHead, (uint32)Buffer->LastRead, (uint64)GFrameCounter);
		
		Submit();
		
		Buffer = MakeShared<FAGXRingBufferRef, ESPMode::ThreadSafe>(GetAGXDeviceContext().GetResourceHeap().CreateBuffer(NewBufferSize, MinAlign, BUF_Dynamic, Options, true));
		BufferSize = NewBufferSize;
		WriteHead = 0;
		CommitHead = 0;
		SubmitHead = 0;
	}
	{
		FAGXBuffer NewBuffer(MTLPP_VALIDATE(mtlpp::Buffer, Buffer->Buffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(ns::Range(WriteHead, FullSize))), false);
		
		AllocatedRanges.Add(ns::Range(WriteHead, FullSize));
		
		FMemory::Memset(((uint8*)NewBuffer.GetContents()), 0x0, FullSize);
		
		WriteHead += FullSize;
		// NewBuffer.MarkSingleUse();
		return NewBuffer;
	}
}

void FAGXSubBufferRing::Shrink()
{
	if(Buffer.IsValid())
	{
		NSUInteger FrameMax = 0;
		for (uint32 i = 0; i < UE_ARRAY_COUNT(FrameSize); i++)
		{
			FrameMax = FMath::Max(FrameMax, FrameSize[i]);
		}
		
		NSUInteger NecessarySize = FMath::Max(FrameMax, InitialSize);
		NSUInteger ThreeQuarterSize = Align((BufferSize / 4) * 3, MinAlign);
		
		if ((GFrameNumberRenderThread - LastFrameChange) >= 120 && NecessarySize < ThreeQuarterSize && NecessarySize < BufferSize)
		{
			Submit();
			
			UE_LOG(LogAGX, Verbose, TEXT("Shrinking RingBuffer from %u to %u as max. usage is %u at frame %lld]"), (uint32)Buffer->Buffer.GetLength(), (uint32)ThreeQuarterSize, (uint32)FrameMax, GFrameNumberRenderThread);
			
			Buffer = MakeShared<FAGXRingBufferRef, ESPMode::ThreadSafe>(GetAGXDeviceContext().GetResourceHeap().CreateBuffer(ThreeQuarterSize, MinAlign, BUF_Dynamic, Options, true));
			BufferSize = ThreeQuarterSize;
			WriteHead = 0;
			CommitHead = 0;
			SubmitHead = 0;
			LastFrameChange = GFrameNumberRenderThread;
		}
		
		FrameSize[GFrameNumberRenderThread % UE_ARRAY_COUNT(FrameSize)] = 0;
	}
}

void FAGXSubBufferRing::Submit()
{
	if (Buffer.IsValid() && WriteHead != SubmitHead)
	{
#if PLATFORM_MAC
		if (Storage == mtlpp::StorageMode::Managed)
		{
			check(SubmitHead < WriteHead);
			ns::Range ModifiedRange(SubmitHead, Align(WriteHead - SubmitHead, MinAlign));
			Buffer->Buffer.DidModify(ModifiedRange);
		}
#endif

		SubmitHead = WriteHead;
	}
}

void FAGXSubBufferRing::Commit(mtlpp::CommandBuffer& CmdBuf)
{
	if (Buffer.IsValid() && WriteHead != CommitHead)
	{
#if PLATFORM_MAC
		check(Storage != mtlpp::StorageMode::Managed || CommitHead < WriteHead);
#endif
		Submit();
		
		NSUInteger BytesWritten = 0;
		if (CommitHead <= WriteHead)
		{
			BytesWritten = WriteHead - CommitHead;
		}
		else
		{
			NSUInteger TrailLen = GetSize() - CommitHead;
			BytesWritten = TrailLen + WriteHead;
		}
		
		FrameSize[GFrameNumberRenderThread % UE_ARRAY_COUNT(FrameSize)] += Align(BytesWritten, MinAlign);
		
		TSharedPtr<FAGXRingBufferRef, ESPMode::ThreadSafe> CmdBufferRingBuffer = Buffer;
		FPlatformMisc::MemoryBarrier();
		
		NSUInteger CommitOffset = CommitHead;
		NSUInteger WriteOffset = WriteHead;
		
		CommitHead = WriteHead;
		
		TArray<ns::Range> Ranges = MoveTemp(AllocatedRanges);
		
		mtlpp::CommandBufferHandler Handler = [CmdBufferRingBuffer, CommitOffset, WriteOffset, Ranges](mtlpp::CommandBuffer const& InBuffer)
		{
#if METAL_DEBUG_OPTIONS
			if (GAGXBufferScribble && CommitOffset != WriteOffset)
			{
				if (CommitOffset < WriteOffset)
				{
					FMemory::Memset(((uint8*)CmdBufferRingBuffer->Buffer.GetContents()) + CommitOffset, 0xCD, WriteOffset - CommitOffset);
				}
				else
				{
					uint32 TrailLen = CmdBufferRingBuffer->Buffer.GetLength() - CommitOffset;
					FMemory::Memset(((uint8*)CmdBufferRingBuffer->Buffer.GetContents()) + CommitOffset, 0xCD, TrailLen);
					FMemory::Memset(((uint8*)CmdBufferRingBuffer->Buffer.GetContents()), 0xCD, WriteOffset);
				}
			}
			
#if MTLPP_CONFIG_VALIDATE
			for (ns::Range const& Range : Ranges)
			{
				MTLPP_VALIDATE_ONLY(mtlpp::Buffer, CmdBufferRingBuffer->Buffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ReleaseRange(Range));
			}
#endif
#endif
			CmdBufferRingBuffer->SetLastRead(WriteOffset);
		};
		CmdBuf.AddCompletedHandler(Handler);
	}
}

uint32 FAGXBufferPoolPolicyData::GetPoolBucketIndex(CreationArguments Args)
{
	uint32 Size = Args.Size;
	
	unsigned long Lower = 0;
	unsigned long Upper = NumPoolBucketSizes;
	unsigned long Middle;
	
	do
	{
		Middle = ( Upper + Lower ) >> 1;
		if( Size <= BucketSizes[Middle-1] )
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while( Upper - Lower > 1 );
	
	check( Size <= BucketSizes[Lower] );
	check( (Lower == 0 ) || ( Size > BucketSizes[Lower-1] ) );
	
	return Lower;
}

uint32 FAGXBufferPoolPolicyData::GetPoolBucketSize(uint32 Bucket)
{
	check(Bucket < NumPoolBuckets);
	uint32 Index = Bucket;
	checkf(Index < NumPoolBucketSizes, TEXT("%d %d"), Index, NumPoolBucketSizes);
	return BucketSizes[Index];
}

FAGXBuffer FAGXBufferPoolPolicyData::CreateResource(CreationArguments Args)
{
	check(Args.Device);	
	uint32 BufferSize = GetPoolBucketSize(GetPoolBucketIndex(Args));
	METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), BufferSize, mtlpp::ResourceOptions(BUFFER_CACHE_MODE | ((NSUInteger)Args.Storage << mtlpp::ResourceStorageModeShift)))));
	FAGXBuffer NewBuf(MTLPP_VALIDATE(mtlpp::Device, Args.Device, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(BufferSize, FAGXCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions(BUFFER_CACHE_MODE | mtlpp::ResourceOptions::HazardTrackingModeUntracked | ((NSUInteger)Args.Storage << mtlpp::ResourceStorageModeShift))))), true);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
	AGXLLM::LogAllocBuffer(Args.Device, NewBuf);
#endif
	INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, NewBuf.GetLength());
	INC_MEMORY_STAT_BY(STAT_AGXPooledBufferUnusedMemory, NewBuf.GetLength());
	return NewBuf;
}

FAGXBufferPoolPolicyData::CreationArguments FAGXBufferPoolPolicyData::GetCreationArguments(FAGXBuffer const& Resource)
{
	return FAGXBufferPoolPolicyData::CreationArguments(Resource.GetDevice(), Resource.GetLength(), 0, Resource.GetStorageMode());
}

void FAGXBufferPoolPolicyData::FreeResource(FAGXBuffer& Resource)
{
	DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Resource.GetLength());
	DEC_MEMORY_STAT_BY(STAT_AGXPooledBufferUnusedMemory, Resource.GetLength());
	Resource = nil;
}

FAGXTexturePool::FAGXTexturePool(FCriticalSection& InPoolMutex)
: PoolMutex(InPoolMutex)
{
}

FAGXTexturePool::~FAGXTexturePool()
{
}

FAGXTexture FAGXTexturePool::CreateTexture(mtlpp::Device Device, mtlpp::TextureDescriptor Desc)
{
	FAGXTexturePool::Descriptor Descriptor;
	Descriptor.textureType = (NSUInteger)Desc.GetTextureType();
	Descriptor.pixelFormat = (NSUInteger)Desc.GetPixelFormat();
	Descriptor.width = Desc.GetWidth();
	Descriptor.height = Desc.GetHeight();
	Descriptor.depth = Desc.GetDepth();
	Descriptor.mipmapLevelCount = Desc.GetMipmapLevelCount();
	Descriptor.sampleCount = Desc.GetSampleCount();
	Descriptor.arrayLength = Desc.GetArrayLength();
	Descriptor.resourceOptions = Desc.GetResourceOptions();
	Descriptor.usage = Desc.GetUsage();
	if (Descriptor.usage == mtlpp::TextureUsage::Unknown)
	{
		Descriptor.usage = (mtlpp::TextureUsage)(mtlpp::TextureUsage::ShaderRead | mtlpp::TextureUsage::ShaderWrite | mtlpp::TextureUsage::RenderTarget | mtlpp::TextureUsage::PixelFormatView);
	}
	Descriptor.freedFrame = 0;

	FScopeLock Lock(&PoolMutex);
	FAGXTexture* Tex = Pool.Find(Descriptor);
	FAGXTexture Texture;
	if (Tex)
	{
		Texture = *Tex;
		Pool.Remove(Descriptor);
		if (GAGXResourcePurgeInPool)
		{
            Texture.SetPurgeableState(mtlpp::PurgeableState::NonVolatile);
        }
	}
	else
	{
		METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocTexture: %s"), TEXT("")/**FString([Desc.GetPtr() description])*/)));
		Texture = MTLPP_VALIDATE(mtlpp::Device, Device, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewTexture(Desc));
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocTexture(Device, Desc, Texture);
#endif
	}
	return Texture;
}

void FAGXTexturePool::ReleaseTexture(FAGXTexture& Texture)
{
	FAGXTexturePool::Descriptor Descriptor;
	Descriptor.textureType = (NSUInteger)Texture.GetTextureType();
	Descriptor.pixelFormat = (NSUInteger)Texture.GetPixelFormat();
	Descriptor.width = Texture.GetWidth();
	Descriptor.height = Texture.GetHeight();
	Descriptor.depth = Texture.GetDepth();
	Descriptor.mipmapLevelCount = Texture.GetMipmapLevelCount();
	Descriptor.sampleCount = Texture.GetSampleCount();
	Descriptor.arrayLength = Texture.GetArrayLength();
	Descriptor.resourceOptions = ((NSUInteger)Texture.GetStorageMode() << mtlpp::ResourceStorageModeShift) | ((NSUInteger)Texture.GetCpuCacheMode() << mtlpp::ResourceCpuCacheModeShift);
	Descriptor.usage = Texture.GetUsage();
	Descriptor.freedFrame = GFrameNumberRenderThread;
	
    if (GAGXResourcePurgeInPool && Texture.SetPurgeableState(mtlpp::PurgeableState::KeepCurrent) == mtlpp::PurgeableState::NonVolatile)
    {
        Texture.SetPurgeableState(mtlpp::PurgeableState::Volatile);
    }
	
	FScopeLock Lock(&PoolMutex);
	Pool.Add(Descriptor, Texture);
}
	
void FAGXTexturePool::Drain(bool const bForce)
{
	FScopeLock Lock(&PoolMutex);
	if (bForce)
	{
		Pool.Empty();
	}
	else
	{
		for (auto It = Pool.CreateIterator(); It; ++It)
		{
			if ((GFrameNumberRenderThread - It->Key.freedFrame) >= CullAfterNumFrames)
			{
				It.RemoveCurrent();
			}
            else
            {
				if (GAGXResourcePurgeInPool && (GFrameNumberRenderThread - It->Key.freedFrame) >= PurgeAfterNumFrames)
				{
					It->Value.SetPurgeableState(mtlpp::PurgeableState::Empty);
				}
            }
        }
    }
}

FAGXResourceHeap::FAGXResourceHeap(void)
: Queue(nullptr)
, TexturePool(Mutex)
, TargetPool(Mutex)
{
}

FAGXResourceHeap::~FAGXResourceHeap()
{
	Compact(nullptr, true);
}

void FAGXResourceHeap::Init(FAGXCommandQueue& InQueue)
{
	Queue = &InQueue;
}

uint32 FAGXResourceHeap::GetMagazineIndex(uint32 Size)
{
	unsigned long Lower = 0;
	unsigned long Upper = NumMagazineSizes;
	unsigned long Middle;
	
	do
	{
		Middle = ( Upper + Lower ) >> 1;
		if( Size <= MagazineSizes[Middle-1] )
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while( Upper - Lower > 1 );
	
	check( Size <= MagazineSizes[Lower] );
	check( (Lower == 0 ) || ( Size > MagazineSizes[Lower-1] ) );;
	
	return Lower;
}

uint32 FAGXResourceHeap::GetHeapIndex(uint32 Size)
{
	unsigned long Lower = 0;
	unsigned long Upper = NumHeapSizes;
	unsigned long Middle;
	
	do
	{
		Middle = ( Upper + Lower ) >> 1;
		if( Size <= HeapSizes[Middle-1] )
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while( Upper - Lower > 1 );
	
	check( Size <= HeapSizes[Lower] );
	check( (Lower == 0 ) || ( Size > HeapSizes[Lower-1] ) );;
	
	return Lower;
}

FAGXResourceHeap::TextureHeapSize FAGXResourceHeap::TextureSizeToIndex(uint32 Size)
{
	unsigned long Lower = 0;
	unsigned long Upper = NumTextureHeapSizes;
	unsigned long Middle;
	
	do
	{
		Middle = ( Upper + Lower ) >> 1;
		if( Size <= (HeapTextureHeapSizes[Middle-1] / MinTexturesPerHeap) )
		{
			Upper = Middle;
		}
		else
		{
			Lower = Middle;
		}
	}
	while( Upper - Lower > 1 );
	
	check( Size <= (HeapTextureHeapSizes[Lower] / MinTexturesPerHeap) );
	check( (Lower == 0 ) || ( Size > (HeapTextureHeapSizes[Lower-1] / MinTexturesPerHeap) ) );
	
	return (TextureHeapSize)Lower;
}

mtlpp::Heap FAGXResourceHeap::GetTextureHeap(mtlpp::TextureDescriptor Desc, mtlpp::SizeAndAlign Size)
{
	mtlpp::Heap Result;
	static bool bTextureHeaps = FParse::Param(FCommandLine::Get(),TEXT("agxtextureheaps"));
	if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesHeaps) && bTextureHeaps && Size.Size <= HeapTextureHeapSizes[MaxTextureSize])
	{
		FAGXResourceHeap::TextureHeapSize HeapIndex = TextureSizeToIndex(Size.Size);

		EAGXHeapTextureUsage UsageIndex = EAGXHeapTextureUsageNum;
		mtlpp::StorageMode StorageMode = Desc.GetStorageMode();
		mtlpp::CpuCacheMode CPUMode = Desc.GetCpuCacheMode();
		if ((Desc.GetUsage() & mtlpp::TextureUsage::RenderTarget) && StorageMode == mtlpp::StorageMode::Private && CPUMode == mtlpp::CpuCacheMode::DefaultCache)
		{
			UsageIndex = PLATFORM_MAC ? EAGXHeapTextureUsageNum : EAGXHeapTextureUsageRenderTarget;
		}
		else if (StorageMode == mtlpp::StorageMode::Private && CPUMode == mtlpp::CpuCacheMode::WriteCombined)
		{
			UsageIndex = EAGXHeapTextureUsageResource;
		}
		
		if (UsageIndex < EAGXHeapTextureUsageNum)
		{
			for (mtlpp::Heap& Heap : TextureHeaps[UsageIndex][HeapIndex])
			{
				if (Heap.MaxAvailableSizeWithAlignment(Size.Align) >= Size.Size)
				{
					Result = Heap;
					break;
				}
			}
			if (!Result)
			{
				mtlpp::HeapDescriptor HeapDesc;
				HeapDesc.SetSize(HeapTextureHeapSizes[HeapIndex]);
				HeapDesc.SetStorageMode(Desc.GetStorageMode());
				HeapDesc.SetCpuCacheMode(Desc.GetCpuCacheMode());
				Result = Queue->GetDevice().NewHeap(HeapDesc);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
				AGXLLM::LogAllocHeap(Queue->GetDevice(), Result);
#endif
				TextureHeaps[UsageIndex][HeapIndex].Add(Result);
			}
			check(Result);
		}
	}
	return Result;
}

FAGXBuffer FAGXResourceHeap::CreateBuffer(uint32 Size, uint32 Alignment, uint32 Flags, mtlpp::ResourceOptions Options, bool bForceUnique)
{
	LLM_SCOPE_METAL(ELLMTagAGX::Buffers);
	LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::Buffers);
	
	static bool bSupportsHeaps = GetAGXDeviceContext().SupportsFeature(EAGXFeaturesHeaps);
	static bool bSupportsBufferSubAllocation = FAGXCommandQueue::SupportsFeature(EAGXFeaturesBufferSubAllocation);
	bForceUnique |= (!bSupportsBufferSubAllocation && !bSupportsHeaps);
	
	uint32 Usage = (Flags & BUF_Static) ? UsageStatic : UsageDynamic;
	
	FAGXBuffer Buffer;
	uint32 BlockSize = Align(Size, Alignment);
	mtlpp::StorageMode StorageMode = (mtlpp::StorageMode)(((NSUInteger)Options & mtlpp::ResourceStorageModeMask) >> mtlpp::ResourceStorageModeShift);
	if (BlockSize <= 33554432)
	{
		switch (StorageMode)
		{
	#if PLATFORM_MAC
			case mtlpp::StorageMode::Managed:
			{
				// TextureBuffers must be 1024 aligned.
				check(Alignment == 256 || Alignment == 1024);
				FScopeLock Lock(&Mutex);

				// Disabled Managed sub-allocation as it seems inexplicably slow on the GPU				
				 if (!bForceUnique && BlockSize <= HeapSizes[NumHeapSizes - 1])
				 {
				 	FAGXSubBufferLinear* Found = nullptr;
				 	for (FAGXSubBufferLinear* Heap : ManagedSubHeaps)
				 	{
				 		if (Heap->CanAllocateSize(BlockSize))
				 		{
				 			Found = Heap;
				 			break;
				 		}
				 	}
				 	if (!Found)
				 	{
				 		Found = new FAGXSubBufferLinear(HeapAllocSizes[NumHeapSizes - 1], BufferOffsetAlignment, mtlpp::ResourceOptions((NSUInteger)Options & (mtlpp::ResourceStorageModeMask|mtlpp::ResourceHazardTrackingModeMask)), Mutex);
				 		ManagedSubHeaps.Add(Found);
				 	}
				 	check(Found);
					
				 	return Found->NewBuffer(BlockSize);
				 }
				 else
				 {
                    Buffer = ManagedBuffers.CreatePooledResource(FAGXPooledBufferArgs(Queue->GetDevice(), BlockSize, Flags, StorageMode));
                     if (GAGXResourcePurgeInPool)
                     {
                         Buffer.SetPurgeableState(mtlpp::PurgeableState::NonVolatile);
                     }
					DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Buffer.GetLength());
					DEC_MEMORY_STAT_BY(STAT_AGXPooledBufferUnusedMemory, Buffer.GetLength());
					INC_MEMORY_STAT_BY(STAT_AGXPooledBufferMemory, Buffer.GetLength());
				}
				break;
			}
	#endif
			case mtlpp::StorageMode::Private:
			case mtlpp::StorageMode::Shared:
			{
				AllocTypes Storage = StorageMode != mtlpp::StorageMode::Private ? AllocShared : AllocPrivate;
				check(Alignment == 16 || Alignment == 64 || Alignment == 256 || Alignment == 1024);

				static bool bSupportsPrivateBufferSubAllocation = FAGXCommandQueue::SupportsFeature(EAGXFeaturesPrivateBufferSubAllocation);
				if (!bForceUnique && BlockSize <= MagazineSizes[NumMagazineSizes - 1] && (Storage == AllocShared || bSupportsPrivateBufferSubAllocation))
				{
					FScopeLock Lock(&Mutex);
					
					uint32 i = GetMagazineIndex(BlockSize);
					TArray<FAGXSubBufferMagazine*>& Heaps = SmallBuffers[Usage][Storage][i];
					
					FAGXSubBufferMagazine* Found = nullptr;
					for (FAGXSubBufferMagazine* Heap : Heaps)
					{
						if (Heap->CanAllocateSize(BlockSize))
						{
							Found = Heap;
							break;
						}
					}
					
					if (!Found)
					{
						Found = new FAGXSubBufferMagazine(MagazineAllocSizes[i], MagazineSizes[i], mtlpp::ResourceOptions((NSUInteger)Options & (mtlpp::ResourceStorageModeMask|mtlpp::ResourceHazardTrackingModeMask)));
						SmallBuffers[Usage][Storage][i].Add(Found);
					}
					check(Found);
					
					Buffer = Found->NewBuffer();
					check(Buffer && Buffer.GetPtr());
				}
				else if (!bForceUnique && BlockSize <= HeapSizes[NumHeapSizes - 1] && (Storage == AllocShared || bSupportsPrivateBufferSubAllocation))
				{
					FScopeLock Lock(&Mutex);
					
					uint32 i = GetHeapIndex(BlockSize);
					TArray<FAGXSubBufferHeap*>& Heaps = BufferHeaps[Usage][Storage][i];
					
					FAGXSubBufferHeap* Found = nullptr;
					for (FAGXSubBufferHeap* Heap : Heaps)
					{
						if (Heap->CanAllocateSize(BlockSize))
						{
							Found = Heap;
							break;
						}
					}
					
					if (!Found)
					{
						uint32 MinAlign = PLATFORM_MAC ? 1024 : 64;
						Found = new FAGXSubBufferHeap(HeapAllocSizes[i], MinAlign, mtlpp::ResourceOptions((NSUInteger)Options & (mtlpp::ResourceStorageModeMask|mtlpp::ResourceHazardTrackingModeMask)), Mutex);
						BufferHeaps[Usage][Storage][i].Add(Found);
					}
					check(Found);
					
					Buffer = Found->NewBuffer(BlockSize);
					check(Buffer && Buffer.GetPtr());
				}
				else
				{
					FScopeLock Lock(&Mutex);
                    Buffer = Buffers[Storage].CreatePooledResource(FAGXPooledBufferArgs(Queue->GetDevice(), BlockSize, Flags, StorageMode));
					if (GAGXResourcePurgeInPool)
					{
                   		Buffer.SetPurgeableState(mtlpp::PurgeableState::NonVolatile);
					}
					DEC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Buffer.GetLength());
					DEC_MEMORY_STAT_BY(STAT_AGXPooledBufferUnusedMemory, Buffer.GetLength());
					INC_MEMORY_STAT_BY(STAT_AGXPooledBufferMemory, Buffer.GetLength());
				}
				break;
			}
			default:
			{
				check(false);
				break;
			}
		}
	}
	else
	{
		METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), BlockSize, Options)));
		Buffer = FAGXBuffer(MTLPP_VALIDATE(mtlpp::Device, Queue->GetDevice(), AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(BlockSize, Options)), false);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocBuffer(Queue->GetDevice(), Buffer);
#endif
		INC_MEMORY_STAT_BY(STAT_AGXDeviceBufferMemory, Buffer.GetLength());
	}
	
	if (GAGXBufferZeroFill && Buffer.GetStorageMode() != mtlpp::StorageMode::Private)
	{
		FMemory::Memset(((uint8*)Buffer.GetContents()), 0, Buffer.GetLength());
	}
	
    METAL_DEBUG_OPTION(GetAGXDeviceContext().ValidateIsInactiveBuffer(Buffer));
	METAL_FATAL_ASSERT(Buffer, TEXT("Failed to create buffer of size %u and resource options %u"), Size, (uint32)Options);
	return Buffer;
}

void FAGXResourceHeap::ReleaseBuffer(FAGXBuffer& Buffer)
{
	mtlpp::StorageMode StorageMode = Buffer.GetStorageMode();
	if (Buffer.IsPooled())
	{
		FScopeLock Lock(&Mutex);
		
		INC_MEMORY_STAT_BY(STAT_AGXBufferUnusedMemory, Buffer.GetLength());
		INC_MEMORY_STAT_BY(STAT_AGXPooledBufferUnusedMemory, Buffer.GetLength());
        DEC_MEMORY_STAT_BY(STAT_AGXPooledBufferMemory, Buffer.GetLength());
		
		if (GAGXResourcePurgeInPool)
		{
       		Buffer.SetPurgeableState(mtlpp::PurgeableState::Volatile);
		}
        
		switch (StorageMode)
		{
	#if PLATFORM_MAC
			case mtlpp::StorageMode::Managed:
			{
				ManagedBuffers.ReleasePooledResource(Buffer);
				break;
			}
	#endif
			case mtlpp::StorageMode::Private:
			{
				Buffers[AllocPrivate].ReleasePooledResource(Buffer);
				break;
			}
			case mtlpp::StorageMode::Shared:
			{
				Buffers[AllocShared].ReleasePooledResource(Buffer);
				break;
			}
			default:
			{
				check(false);
				break;
			}
		}
	}
	else
	{
		DEC_MEMORY_STAT_BY(STAT_AGXDeviceBufferMemory, Buffer.GetLength());
		Buffer.Release();
	}
}

FAGXTexture FAGXResourceHeap::CreateTexture(mtlpp::TextureDescriptor Desc, FAGXSurface* Surface)
{
	LLM_SCOPE_METAL(ELLMTagAGX::Textures);
	LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::Textures);
	
	mtlpp::SizeAndAlign Res = Queue->GetDevice().HeapTextureSizeAndAlign(Desc);
	mtlpp::Heap Heap = GetTextureHeap(Desc, Res);
	if (Heap)
	{
		METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocTexture: %s"), TEXT("")/**FString([Desc.GetPtr() description])*/)));
		FAGXTexture Texture = Heap.NewTexture(Desc);
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
		AGXLLM::LogAllocTexture(Queue->GetDevice(), Desc, Texture);
#endif
		return Texture;
	}
	else if (Desc.GetUsage() & mtlpp::TextureUsage::RenderTarget)
	{
		LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::RenderTargets);
		return TargetPool.CreateTexture(Queue->GetDevice(), Desc);
	}
	else
	{
		return TexturePool.CreateTexture(Queue->GetDevice(), Desc);
	}
}

void FAGXResourceHeap::ReleaseTexture(FAGXSurface* Surface, FAGXTexture& Texture)
{
	if (Texture && !Texture.GetBuffer() && !Texture.GetParentTexture() && !Texture.GetHeap())
	{
        if (Texture.GetUsage() & mtlpp::TextureUsage::RenderTarget)
        {
           	TargetPool.ReleaseTexture(Texture);
        }
        else
        {
            TexturePool.ReleaseTexture(Texture);
        }
	}
}

void FAGXResourceHeap::Compact(FAGXRenderPass* Pass, bool const bForce)
{
	FScopeLock Lock(&Mutex);
    for (uint32 u = 0; u < NumUsageTypes; u++)
    {
        for (uint32 t = 0; t < NumAllocTypes; t++)
        {
            for (uint32 i = 0; i < NumMagazineSizes; i++)
            {
                for (auto It = SmallBuffers[u][t][i].CreateIterator(); It; ++It)
                {
                    FAGXSubBufferMagazine* Data = *It;
                    if (Data->NumCurrentAllocations() == 0 || bForce)
                    {
                        It.RemoveCurrent();
                        delete Data;
                    }
                }
            }
            
            uint32 BytesCompacted = 0;
            uint32 const BytesToCompact = GAGXHeapBufferBytesToCompact;
            
            for (uint32 i = 0; i < NumHeapSizes; i++)
            {
                for (auto It = BufferHeaps[u][t][i].CreateIterator(); It; ++It)
                {
                    FAGXSubBufferHeap* Data = *It;
                    if (Data->NumCurrentAllocations() == 0 || bForce)
                    {
                        It.RemoveCurrent();
                        delete Data;
                    }
                }
            }
        }
    }

	Buffers[AllocShared].DrainPool(bForce);
	Buffers[AllocPrivate].DrainPool(bForce);
#if PLATFORM_MAC
	ManagedBuffers.DrainPool(bForce);
	for (auto It = ManagedSubHeaps.CreateIterator(); It; ++It)
	{
		FAGXSubBufferLinear* Data = *It;
		if (Data->GetUsedSize() == 0 || bForce)
		{
			It.RemoveCurrent();
			delete Data;
		}
	}
#endif
	TexturePool.Drain(bForce);
	TargetPool.Drain(bForce);
}
	
uint32 FAGXBufferPoolPolicyData::BucketSizes[FAGXBufferPoolPolicyData::NumPoolBucketSizes] = {
	256,
	512,
	1024,
	2048,
	4096,
	8192,
	16384,
	32768,
	65536,
	131072,
	262144,
	524288,
	1048576,
	2097152,
	4194304,
	8388608,
	12582912,
	16777216,
	25165824,
	33554432,
};

FAGXBufferPool::~FAGXBufferPool()
{
}

uint32 FAGXResourceHeap::MagazineSizes[FAGXResourceHeap::NumMagazineSizes] = {
	16,
	32,
	64,
	128,
	256,
	512,
	1024,
	2048,
	4096,
	8192,
};

uint32 FAGXResourceHeap::HeapSizes[FAGXResourceHeap::NumHeapSizes] = {
	1048576,
	2097152,
};

uint32 FAGXResourceHeap::MagazineAllocSizes[FAGXResourceHeap::NumMagazineSizes] = {
	4096,
	4096,
	4096,
	8192,
	8192,
	8192,
	16384,
	16384,
	16384,
	32768,
};

uint32 FAGXResourceHeap::HeapAllocSizes[FAGXResourceHeap::NumHeapSizes] = {
	2097152,
	4194304,
};

uint32 FAGXResourceHeap::HeapTextureHeapSizes[FAGXResourceHeap::NumTextureHeapSizes] = {
	4194304,
	8388608,
	16777216,
	33554432,
	67108864,
	134217728,
	268435456
};
