// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/Allocators/CachedOSVeryLargePageAllocator.h"
#include "HAL/UnrealMemory.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CsvProfiler.h"

#if UE_USE_VERYLARGEPAGEALLOCATOR

// Free pages that are cached can be preallocated at the initialization of the VeryLargePageAllocator
// Having them preallocated and kept during the life of the title help to avoid defragmentation of physical memory
#ifndef UE_VERYLARGEPAGEALLOCATOR_PREALLOCATE_EMPTY_BACKSTORE
	#define UE_VERYLARGEPAGEALLOCATOR_PREALLOCATE_EMPTY_BACKSTORE 0
#endif

// Number of free pages (2MB each) to be kept (not decommited) in cache so they can be reused:
// Number of free pages to cache for SmallPool allocation hint. 
#ifndef UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_SMALLPOOL
	#define UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_SMALLPOOL 10
#endif

// Number of free pages to cache for Default allocation hint.
#ifndef UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_DEFAULT
	#define UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_DEFAULT 10
#endif

CORE_API bool GEnableVeryLargePageAllocator = true;
static int32 GVeryLargePageAllocatorMaxEmptyBackStoreCount[FMemory::AllocationHints::Max] = {
	UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_DEFAULT,	// FMemory::AllocationHints::Default
	0,														// FMemory::AllocationHints::Temporary
	UE_VERYLARGEPAGEALLOCATOR_MAX_EMPTY_BACKSTORE_SMALLPOOL // FMemory::AllocationHints::SmallPool
};

static_assert(int32(FMemory::AllocationHints::Max) == 3); // ensure FMemory::AllocationHints has 3 types of hint so GVeryLargePageAllocatorMaxEmptyBackStoreCount has needed values

static FAutoConsoleVariableRef CVarVeryLargePageAllocatorMaxEmptyBackstoreDefault(
	TEXT("VeryLargePageAllocator.MaxEmptyBackstoreDefault"),
	GVeryLargePageAllocatorMaxEmptyBackStoreCount[FMemory::AllocationHints::Default],
	TEXT(""));

static FAutoConsoleVariableRef CVarVeryLargePageAllocatorMaxEmptyBackstoreSmallPool(
	TEXT("VeryLargePageAllocator.MaxEmptyBackstoreSmallPool"),
	GVeryLargePageAllocatorMaxEmptyBackStoreCount[FMemory::AllocationHints::SmallPool],
	TEXT(""));

#if CSV_PROFILER
CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, FMemory);

static volatile int32 GLargePageAllocatorCommitCount = 0;
static volatile int32 GLargePageAllocatorDecommitCount = 0;
static volatile int32 GLargePageAllocatorLargePageCount[FMemory::AllocationHints::Max];
#endif

void FCachedOSVeryLargePageAllocator::Init()
{
	Block = FPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(AddressSpaceToReserve);
	AddressSpaceReserved = (uintptr_t)Block.GetVirtualPointer();
	AddressSpaceReservedEnd = AddressSpaceReserved + AddressSpaceToReserve;
#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	AddressSpaceReservedEndSmallPool = AddressSpaceReserved + (AddressSpaceToReserve / 2);
#else
	AddressSpaceReservedEndSmallPool = AddressSpaceReservedEnd;
#endif


	for (int i = 0; i < FMemory::AllocationHints::Max; i++)
	{
		FreeLargePagesHead[i] = nullptr;
		UsedLargePagesWithSpaceHead[i] = nullptr;
		UsedLargePagesHead[i] = nullptr;
		EmptyButAvailableLargePagesHead[i] = nullptr;
		EmptyBackStoreCount[i] = 0;
		
#if CSV_PROFILER
		GLargePageAllocatorLargePageCount[i] = 0;
#endif
	}

#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	for (int i = 0; i < NumberOfLargePages / 2; i++)
#else
	for (int i = 0; i < NumberOfLargePages; i++)
#endif
	{
		LargePagesArray[i].Init((void*)((uintptr_t)AddressSpaceReserved + (i * SizeOfLargePage)));
		LargePagesArray[i].LinkHead(FreeLargePagesHead[FMemory::AllocationHints::SmallPool]);
	}

#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	for (int i = NumberOfLargePages / 2; i < NumberOfLargePages; i++)
	{
		LargePagesArray[i].Init((void*)((uintptr_t)AddressSpaceReserved + (i * SizeOfLargePage)));
		LargePagesArray[i].LinkHead(FreeLargePagesHead[FMemory::AllocationHints::Default]);
	}
#endif

#if UE_VERYLARGEPAGEALLOCATOR_PREALLOCATE_EMPTY_BACKSTORE
	for (int32 i = 0; i < FMemory::AllocationHints::Max; i++)
	{
		FMemory::AllocationHints AllocationHint = FMemory::AllocationHints(i);
		
		for (int32 BackStoreIndex = 0; BackStoreIndex < GVeryLargePageAllocatorMaxEmptyBackStoreCount[AllocationHint]; BackStoreIndex++)
		{
			FLargePage* LargePage = FreeLargePagesHead[AllocationHint];

			check(LargePage != nullptr); // Can't happen
			LargePage->AllocationHint = AllocationHint;
			LargePage->Unlink();
			Block.Commit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage, false);
			LargePage->LinkHead(EmptyButAvailableLargePagesHead[AllocationHint]);
			EmptyBackStoreCount[AllocationHint] += 1;
			CachedFree += SizeOfLargePage;
		}
	}
#endif

	if (!GEnableVeryLargePageAllocator)
	{
		bEnabled = false;
	}
}

void* FCachedOSVeryLargePageAllocator::Allocate(SIZE_T Size, uint32 AllocationHint, FCriticalSection* Mutex)
{
	Size = Align(Size, 4096);

	void* ret = nullptr;

	if (bEnabled && Size == SizeOfSubPage)
	{
#if !UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
		if (AllocationHint == FMemory::AllocationHints::SmallPool)
#endif
		{
			bool bLinkToUsedLargePagesWithSpaceHead = false;
			FLargePage* LargePage = UsedLargePagesWithSpaceHead[AllocationHint];

			if (LargePage == nullptr)
			{
				bLinkToUsedLargePagesWithSpaceHead = true;
				LargePage = EmptyButAvailableLargePagesHead[AllocationHint];

				if (LargePage != nullptr)
				{
					LargePage->AllocationHint = AllocationHint;
					LargePage->Unlink();
					EmptyBackStoreCount[AllocationHint] -= 1;
				}
				else
				{
					LargePage = FreeLargePagesHead[AllocationHint];

					if (LargePage != nullptr)
					{
						LargePage->AllocationHint = AllocationHint;
						LargePage->Unlink();
						{
#if UE_ALLOW_OSMEMORYLOCKFREE
							FScopeUnlock ScopeUnlock(Mutex);
#endif
							LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
#if UE_USE_VERYLARGEPAGEALLOCATOR_FALLBACKPATH
							if (!Block.Commit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage, false))
							{
#if UE_ALLOW_OSMEMORYLOCKFREE
								if (Mutex != nullptr)
								{
									FScopeLock Lock(Mutex);
									LargePage->LinkHead(FreeLargePagesHead[AllocationHint]);
								}
								else
#endif
								{
									LargePage->LinkHead(FreeLargePagesHead[AllocationHint]);
								}

								// Fallback to regular allocator
								LargePage = nullptr;
							}
#if CSV_PROFILER
							else
							{
								// A new large page has been created. Add it to CachedFree counter
								CachedFree += SizeOfLargePage;
								FPlatformAtomics::InterlockedIncrement(&GLargePageAllocatorCommitCount);
								FPlatformAtomics::InterlockedIncrement(&GLargePageAllocatorLargePageCount[AllocationHint]);
							}
#endif
						}
					}
#else 
							Block.Commit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
						}
					}
#endif // UE_USE_VERYLARGEPAGEALLOCATOR_FALLBACKPATH
				}
			}
			if (LargePage != nullptr)
			{
				if (bLinkToUsedLargePagesWithSpaceHead)
				{
					LargePage->LinkHead(UsedLargePagesWithSpaceHead[AllocationHint]);
				}

				ret = LargePage->Allocate();
				if (ret)
				{
					if (LargePage->NumberOfFreeSubPages == 0)
					{
						LargePage->Unlink();
						LargePage->LinkHead(UsedLargePagesHead[AllocationHint]);
					}
					CachedFree -= SizeOfSubPage;
				}
				else
				{
					if (AllocationHint == FMemory::AllocationHints::SmallPool)
					{
						UE_CLOG(!ret, LogMemory, Fatal, TEXT("The FCachedOSVeryLargePageAllocator has run out of address space for SmallPool allocations, increase UE_VERYLARGEPAGEALLOCATOR_RESERVED_SIZE_IN_GB for your platform!"));
					}
				}
			}
		}
	}

	if (ret == nullptr)
	{
		ret = CachedOSPageAllocator.Allocate(Size, AllocationHint, Mutex);
	}
	return ret;
}

#define LARGEPAGEALLOCATOR_SORT_OnAddress 1

void FCachedOSVeryLargePageAllocator::Free(void* Ptr, SIZE_T Size, FCriticalSection* Mutex, bool ThreadIsTimeCritical)
{
	Size = Align(Size, 4096);
	uint64 Index = ((uintptr_t)Ptr - (uintptr_t)AddressSpaceReserved) / SizeOfLargePage;
	if (Index < (NumberOfLargePages))
	{
		FLargePage* LargePage = &LargePagesArray[Index];

		LargePage->Free(Ptr);
		CachedFree += SizeOfSubPage;

		if (LargePage->NumberOfFreeSubPages == NumberOfSubPagesPerLargePage)
		{
			// totally free
			LargePage->Unlink();

			// move it to EmptyButAvailableLargePagesHead if that pool of backstore is not full yet
			if (EmptyBackStoreCount[LargePage->AllocationHint] < GVeryLargePageAllocatorMaxEmptyBackStoreCount[LargePage->AllocationHint])
			{
				LargePage->LinkHead(EmptyButAvailableLargePagesHead[LargePage->AllocationHint]);
				EmptyBackStoreCount[LargePage->AllocationHint] += 1;
			}
			else
			{
				// need to move which list we are in and remove the backing store
				{
#if UE_ALLOW_OSMEMORYLOCKFREE
					FScopeUnlock ScopeUnlock(Mutex);
#endif
					Block.Decommit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
				}

#if CSV_PROFILER
				FPlatformAtomics::InterlockedIncrement(&GLargePageAllocatorDecommitCount);
				FPlatformAtomics::InterlockedDecrement(&GLargePageAllocatorLargePageCount[LargePage->AllocationHint]);
#endif

				LargePage->LinkHead(FreeLargePagesHead[LargePage->AllocationHint]);
				CachedFree -= SizeOfLargePage;
			}
		}
		else if (LargePage->NumberOfFreeSubPages == 1)
		{
			LargePage->Unlink();
#if LARGEPAGEALLOCATOR_SORT_OnAddress
			FLargePage* InsertPoint = UsedLargePagesWithSpaceHead[LargePage->AllocationHint];
			while (InsertPoint != nullptr)
			{
				if (LargePage->BaseAddress < InsertPoint->BaseAddress)	// sort on address
				{
					break;
				}
				InsertPoint = InsertPoint->Next();
			}
			if (InsertPoint == nullptr || InsertPoint == UsedLargePagesWithSpaceHead[LargePage->AllocationHint])
			{
				LargePage->LinkHead(UsedLargePagesWithSpaceHead[LargePage->AllocationHint]);
			}
			else
			{
				LargePage->LinkBefore(InsertPoint);
			}
#else
			LargePage->LinkHead(UsedLargePagesWithSpaceHead[LargePage->AllocationHint]);
#endif
		}
		else
		{
#if !LARGEPAGEALLOCATOR_SORT_OnAddress
			FLargePage* InsertPoint = LargePage->Next();
			FLargePage* LastInsertPoint = nullptr;

			if ((InsertPoint != nullptr) && LargePage->NumberOfFreeSubPages > InsertPoint->NumberOfFreeSubPages)
			{
				LastInsertPoint = InsertPoint;
				LargePage->Unlink();
				while (InsertPoint != nullptr)
				{
					if (LargePage->NumberOfFreeSubPages <= InsertPoint->NumberOfFreeSubPages)	// sort on number of free sub pages
					{
						break;
					}
					InsertPoint = InsertPoint->Next();
				}
				if (InsertPoint != nullptr)
				{
					LargePage->LinkBefore(InsertPoint);
				}
				else
				{
					LargePage->LinkAfter(LastInsertPoint);
				}
			}
#endif
		}
	}
	else
	{
		CachedOSPageAllocator.Free(Ptr, Size, Mutex, ThreadIsTimeCritical);
	}
}

void FCachedOSVeryLargePageAllocator::FreeAll(FCriticalSection* Mutex)
{
	for (int i = 0; i < FMemory::AllocationHints::Max; i++)
	{
		FMemory::AllocationHints AllocationHint = FMemory::AllocationHints(i);
		
		for (;;)
		{
			FLargePage* LargePage = EmptyButAvailableLargePagesHead[AllocationHint];

			if (LargePage == nullptr)
			{
				break;
			}
			LargePage->Unlink();
			EmptyBackStoreCount[AllocationHint] -= 1;
			Block.Decommit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);

			LargePage->LinkHead(FreeLargePagesHead[LargePage->AllocationHint]);
			CachedFree -= SizeOfLargePage;

#if CSV_PROFILER
			FPlatformAtomics::InterlockedDecrement(&GLargePageAllocatorLargePageCount[LargePage->AllocationHint]);
#endif
		}
	}


	// Free empty cached pages of CachedOSPageAllocator
	CachedOSPageAllocator.FreeAll(Mutex);
}

void FCachedOSVeryLargePageAllocator::UpdateStats()
{
#if CSV_PROFILER
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorCommitCount, GLargePageAllocatorCommitCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorDecommitCount, GLargePageAllocatorDecommitCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorBackingStoreCountSmall, EmptyBackStoreCount[FMemory::AllocationHints::SmallPool], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorBackingStoreCountDefault, EmptyBackStoreCount[FMemory::AllocationHints::Default], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorPageCountSmall, GLargePageAllocatorLargePageCount[FMemory::AllocationHints::SmallPool], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FMemory, LargeAllocatorPageCountDefault, GLargePageAllocatorLargePageCount[FMemory::AllocationHints::Default], ECsvCustomStatOp::Set);

	GLargePageAllocatorCommitCount = 0;
	GLargePageAllocatorDecommitCount = 0;
#endif
}
#endif
