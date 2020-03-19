// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/Allocators/CachedOSVeryLargePageAllocator.h"
#include "HAL/UnrealMemory.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "HAL/LowLevelMemTracker.h"

#if UE_USE_VERYLARGEPAGEALLOCATOR

CORE_API bool GEnableVeryLargePageAllocator = true;

void FCachedOSVeryLargePageAllocator::Init()
{
	Block = FPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(AddressSpaceToReserve);
	AddressSpaceReserved = (uintptr_t)Block.GetVirtualPointer();


	FreeLargePagesHead = nullptr;
	for (int i = 0; i < NumberOfLargePages; i++)
	{
		LargePagesArray[i].Init((void*)((uintptr_t)AddressSpaceReserved + (i*SizeOfLargePage)));
		LargePagesArray[i].LinkHead(FreeLargePagesHead);
	}

	UsedLargePagesHead = nullptr;
	for (int i = 0; i < FMemory::AllocationHints::Max; i++)
	{
		UsedLargePagesWithSpaceHead[i] = nullptr;
	}
	if (!GEnableVeryLargePageAllocator)
	{
		bEnabled = false;
	}
}

void* FCachedOSVeryLargePageAllocator::Allocate(SIZE_T Size, uint32 AllocationHint)
{
	Size = Align(Size, 4096);

	void* ret = nullptr;

	if (bEnabled && Size == SizeOfSubPage && AllocationHint == FMemory::AllocationHints::SmallPool)
	{

		if (UsedLargePagesWithSpaceHead[AllocationHint] == nullptr)
		{
			FLargePage* LargePage = FreeLargePagesHead;
			if (LargePage)
			{
				Block.Commit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
				LargePage->AllocationHint = AllocationHint;
				LargePage->Unlink();
				LargePage->LinkHead(UsedLargePagesWithSpaceHead[AllocationHint]);
				CachedFree += SizeOfLargePage;
			}
		}
		FLargePage* LargePage = UsedLargePagesWithSpaceHead[AllocationHint];
		if (LargePage != nullptr)
		{
			ret = LargePage->Allocate();
			if (ret)
			{
				if (LargePage->NumberOfFreeSubPages == 0)
				{
					LargePage->Unlink();
					LargePage->LinkHead(UsedLargePagesHead);
				}
				CachedFree -= SizeOfSubPage;
			}
		}
	}

	if (ret == nullptr)
	{
		ret = CachedOSPageAllocator.Allocate(Size);
	}
	return ret;
}

#define LARGEPAGEALLOCATOR_SORT_OnAddress 1

void FCachedOSVeryLargePageAllocator::Free(void* Ptr, SIZE_T Size)
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
			// totally free, need to move which list we are in and remove the backing store
			LargePage->Unlink();
			LargePage->LinkHead(FreeLargePagesHead);
			Block.Decommit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
			CachedFree -= SizeOfLargePage;
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
		CachedOSPageAllocator.Free(Ptr, Size);
	}
}

void FCachedOSVeryLargePageAllocator::FreeAll()
{
	CachedOSPageAllocator.FreeAll();
}
#endif