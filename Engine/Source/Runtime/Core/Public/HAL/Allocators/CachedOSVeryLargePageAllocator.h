// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/UnrealMemory.h"
#include "Containers/List.h"
#include "HAL/Allocators/CachedOSPageAllocator.h"
#include "HAL/PlatformMemory.h"

#if UE_USE_VERYLARGEPAGEALLOCATOR

#if PLATFORM_64BITS
#define CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT (64*1024*1024)
#else
#define CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT (16*1024*1024)
#endif
//#define CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES (CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT/(1024*64))
#define CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES (128)

class FCachedOSVeryLargePageAllocator
{
	static const uint64 AddressSpaceToReserve = ((1024 * 1024) * (1024 + 512));
	static const uint64 SizeOfLargePage = (1024 * 1024 * 4);
	static const uint64 SizeOfSubPage = (1024 * 64);
public:

	FCachedOSVeryLargePageAllocator()
		: bEnabled(true)
	{
		Init();
	}

	~FCachedOSVeryLargePageAllocator()
	{
		// this leaks everything!
	}

	void* Allocate(SIZE_T Size);

	void Free(void* Ptr, SIZE_T Size);

	void FreeAll();

	uint64 GetCachedFreeTotal()
	{
		return CachedOSPageAllocator.GetCachedFreeTotal();
	}

private:

	void Init();

	bool bEnabled;
	uintptr_t	AddressSpaceReserved;

	FPlatformMemory::FPlatformVirtualMemoryBlock Block;

	struct FLargePage : public TIntrusiveLinkedList<FLargePage>
	{
		uintptr_t	FreeSubPages[SizeOfLargePage / SizeOfSubPage];
		int32		NumberOfFreeSubPages;

		uintptr_t	BaseAddress;

		void Init(void* InBaseAddress)
		{
			BaseAddress = (uintptr_t)InBaseAddress;
			NumberOfFreeSubPages = (SizeOfLargePage / SizeOfSubPage);
			uintptr_t Ptr = BaseAddress;
			for (int i = 0; i < NumberOfFreeSubPages; i++)
			{
				FreeSubPages[i] = Ptr;
				Ptr += SizeOfSubPage;
			}
		}

		void Free(void* Ptr)
		{
			FreeSubPages[NumberOfFreeSubPages++] = (uintptr_t)Ptr;
		}

		void* Allocate()
		{
			void* ret = nullptr;
			if (NumberOfFreeSubPages)
			{
				ret = (void*)FreeSubPages[--NumberOfFreeSubPages];
			}
			return ret;
		}
	};

	FLargePage*	FreeLargePagesHead;				// no backing store

	FLargePage*	UsedLargePagesHead;				// has backing store and is full

	FLargePage*	UsedLargePagesWithSpaceHead;	// has backing store and still has room

	FLargePage	LargePagesArray[AddressSpaceToReserve / SizeOfLargePage];

	TCachedOSPageAllocator<CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES, CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT> CachedOSPageAllocator;

};
CORE_API extern bool GEnableVeryLargePageAllocator;
#endif // UE_USE_VERYLARGEPAGEALLOCATOR