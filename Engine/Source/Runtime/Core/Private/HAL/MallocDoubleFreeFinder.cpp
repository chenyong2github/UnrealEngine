// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FMallocDoubleFreeFinder.cpp: Memoory tracking allocator
=============================================================================*/
#include "HAL/MallocDoubleFreeFinder.h"
#include "Logging/LogMacros.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
//#include "Templates/Function.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformStackWalk.h"

CORE_API FMallocDoubleFreeFinder* GMallocDoubleFreeFinder;
CORE_API bool GMallocDoubleFreeFinderEnabled = false;

FMallocDoubleFreeFinder::FMallocDoubleFreeFinder(FMalloc* InMalloc)
	: UsedMalloc(InMalloc)
	, Initialized(false)
{
	DisabledTLS = FPlatformTLS::AllocTlsSlot();
	uint64_t Count = 0;
	FPlatformTLS::SetTlsValue(DisabledTLS, (void*)Count);
}

void FMallocDoubleFreeFinder::Init()
{
	if (Initialized)
	{
		return;
	}
	CallStackInfoArray.Reserve(1250000);	/* Needs to be big enough to never resize! */
	CallStackMapKeyToCallStackIndexMap.Reserve(1250000);
	TrackedFreeAllocations.Reserve(6000000);
	TrackedCurrentAllocations.Reserve(8000000);
	Initialized = true;
}



//***********************************
// FMalloc
//***********************************

void* FMallocDoubleFreeFinder::Malloc(SIZE_T Size, uint32 Alignment)
{
	if (IsDisabled())
	{
		return UsedMalloc->Malloc(Size, Alignment);
	}

	FScopeDisableDoubleFreeFinder Disable;

	int32 CallStackIndex = GetCallStackIndex();

	FScopeLock Lock(&CriticalSection);

	void* Ptr = UsedMalloc->Malloc(Size, Alignment);

	SIZE_T AllocatedSize = Size;
	if (UsedMalloc->GetAllocationSize(Ptr, AllocatedSize))
	{
		TrackMalloc(Ptr, (uint32)AllocatedSize, CallStackIndex);
	}
	else
	{
		TrackMalloc(Ptr, (uint32)Size, CallStackIndex);
	}
	return Ptr;
}

void* FMallocDoubleFreeFinder::Realloc(void* OldPtr, SIZE_T NewSize, uint32 Alignment)
{
	if (IsDisabled())
	{
		return UsedMalloc->Realloc(OldPtr, NewSize, Alignment);
	}

	FScopeDisableDoubleFreeFinder Disable;

	int32 CallStackIndex = GetCallStackIndex();
	SIZE_T OldSize = 0;

	FScopeLock Lock(&CriticalSection);

	UsedMalloc->GetAllocationSize(OldPtr, OldSize);

	void* NewPtr = UsedMalloc->Realloc(OldPtr, NewSize, Alignment);

	SIZE_T AllocatedSize = NewSize;
	if (UsedMalloc->GetAllocationSize(NewPtr, AllocatedSize))
	{
		TrackRealloc(OldPtr, NewPtr, (uint32)AllocatedSize, (uint32)OldSize, CallStackIndex);
	}
	else
	{
		TrackRealloc(OldPtr, NewPtr, (uint32)NewSize, (uint32)OldSize, CallStackIndex);
	}
	return NewPtr;
}

void FMallocDoubleFreeFinder::Free(void* Ptr)
{
	if (IsDisabled() || Ptr == nullptr)
	{
		return UsedMalloc->Free(Ptr);
	}

	FScopeDisableDoubleFreeFinder Disable;

	int32 CallStackIndex = GetCallStackIndex();

	FScopeLock Lock(&CriticalSection);
	SIZE_T OldSize = 0;
	UsedMalloc->GetAllocationSize(Ptr, OldSize);
	UsedMalloc->Free(Ptr);
	TrackFree(Ptr, OldSize, CallStackIndex);
}

void FMallocDoubleFreeFinder::TrackMalloc(void* Ptr, uint32 Size, int32 CallStackIndex)
{
	if (Ptr != nullptr)
	{
		TrackedAllocationData* AlreadyThere = TrackedCurrentAllocations.Find(Ptr);
		if (AlreadyThere != nullptr)
		{
			static TrackedAllocationData* AlreadyThereStatic = AlreadyThere;
			TrackSpecial(Ptr);
			PLATFORM_BREAK();
		}
		TrackedCurrentAllocations.Add(Ptr, TrackedAllocationData(Size, CallStackIndex));
	}
}

void FMallocDoubleFreeFinder::TrackFree(void* Ptr, uint32 OldSize, int32 CallStackIndex)
{
	TrackedAllocationData Removed;
	if (!TrackedCurrentAllocations.RemoveAndCopyValue(Ptr, Removed))
	{
		static TrackedAllocationData WhatHaveWeHere;
		//memory we don't know about
		TrackedAllocationData*AlreadyThere = TrackedFreeAllocations.Find(Ptr);
		WhatHaveWeHere = *AlreadyThere;
		DumpStackTraceToLog(AlreadyThere->CallStackIndex);
		PLATFORM_BREAK();
	}
	else
	{
		if (OldSize != 0 && OldSize != Removed.Size)
		{
			PLATFORM_BREAK();
		}
		TrackedFreeAllocations.Add(Ptr, TrackedAllocationData(OldSize, CallStackIndex));
	}
}

void FMallocDoubleFreeFinder::TrackRealloc(void* OldPtr, void* NewPtr, uint32 NewSize, uint32 OldSize, int32 CallStackIndex)
{
	if (OldPtr == nullptr)
	{
		TrackMalloc(NewPtr, NewSize, CallStackIndex);
	}
	else
	{
		if (OldPtr != NewPtr)
		{
			if (OldPtr)
			{
				TrackFree(OldPtr, OldSize, CallStackIndex);
			}
			if (NewPtr)
			{
				TrackMalloc(NewPtr, NewSize, CallStackIndex);
			}
		}
	}
}

int32 FMallocDoubleFreeFinder::GetCallStackIndex()
{
	// Index of the callstack in CallStackInfoArray.
	int32 Index = INDEX_NONE;

	// Capture callstack and create FCallStackMapKey.
	uint64 FullCallStack[MallocDoubleFreeFinderMaxCallStackDepth + MallocDoubleFreeFinderCallStackEntriesToSkipCount] = { 0 };
	// CRC is filled in by the CaptureStackBackTrace, not all platforms calculate this for you.
	uint32 CRC;
	FPlatformStackWalk::CaptureStackBackTrace(&FullCallStack[0], MallocDoubleFreeFinderMaxCallStackDepth + MallocDoubleFreeFinderCallStackEntriesToSkipCount, &CRC);
	// Skip first n entries as they are inside the allocator.
	uint64* CallStack = &FullCallStack[MallocDoubleFreeFinderCallStackEntriesToSkipCount];
	FCallStackMapKey CallStackMapKey(CRC, CallStack);
	RWLock.ReadLock();
	int32* IndexPtr = CallStackMapKeyToCallStackIndexMap.Find(CallStackMapKey);
	if (IndexPtr)
	{
		// Use index if found
		Index = *IndexPtr;
		RWLock.ReadUnlock();
	}
	else
	{
		// new call stack, add to array and set index.
		RWLock.ReadUnlock();
		FCallStackInfoDoublleFreeFinder CallStackInfo;
		CallStackInfo.Count = MallocDoubleFreeFinderMaxCallStackDepth;
		for (int32 i = 0; i < MallocDoubleFreeFinderMaxCallStackDepth; i++)
		{
			if (!CallStack[i] && CallStackInfo.Count == MallocDoubleFreeFinderMaxCallStackDepth)
			{
				CallStackInfo.Count = i;
			}
			CallStackInfo.FramePointers[i] = CallStack[i];
		}

		RWLock.WriteLock();
		Index = CallStackInfoArray.Num();
		CallStackInfoArray.Append(&CallStackInfo, 1);
		CallStackMapKey.CallStack = &CallStackInfoArray[Index].FramePointers[0];
		CallStackMapKeyToCallStackIndexMap.Add(CallStackMapKey, Index);
		RWLock.WriteUnlock();
	}
	return Index;
}

// This can be set externally, if it is we try and find what freed it before.
void * GTrackFreeSpecialPtr = nullptr;

// Can be called to find out what freed something last
void FMallocDoubleFreeFinder::TrackSpecial(void* Ptr)
{
	FScopeDisableDoubleFreeFinder Disable;
	FScopeLock Lock(&CriticalSection);
	static TrackedAllocationData WhatHaveWeHere;	// Made static so it should be visible in the debugger
	TrackedAllocationData*AlreadyThere;
	TrackedAllocationData Removed;
	if (GTrackFreeSpecialPtr != nullptr)
	{
		if (!TrackedCurrentAllocations.RemoveAndCopyValue(GTrackFreeSpecialPtr, Removed))
		{
			// Untracked memory!!
			AlreadyThere = TrackedFreeAllocations.Find(GTrackFreeSpecialPtr);
			WhatHaveWeHere = *AlreadyThere;
			DumpStackTraceToLog(AlreadyThere->CallStackIndex);
			PLATFORM_BREAK();
		}

	}
	if (!TrackedCurrentAllocations.RemoveAndCopyValue(Ptr, Removed))
	{
		// Untracked memory!!
		AlreadyThere = TrackedFreeAllocations.Find(Ptr);
		WhatHaveWeHere = *AlreadyThere;
		DumpStackTraceToLog(AlreadyThere->CallStackIndex);
		PLATFORM_BREAK();
	}

	// Untracked memory!!
	AlreadyThere = TrackedFreeAllocations.Find(Ptr);
	if (AlreadyThere)
	{
		// found an exact match
		WhatHaveWeHere = *AlreadyThere;
		DumpStackTraceToLog(AlreadyThere->CallStackIndex);
		PLATFORM_BREAK();
	}
	else
	{
		// look for the pointer within another allocation that was previously freed
		auto MyIterator = TrackedFreeAllocations.CreateIterator();
		for (; MyIterator; ++MyIterator)
		{
			intptr_t MyKey = (intptr_t)MyIterator.Key();
			intptr_t MyPtr = (intptr_t)Ptr;
			TrackedAllocationData *AlreadyThere2 = &MyIterator.Value();
			if (MyPtr >= MyKey)
			{
				if (MyPtr < (MyKey + (intptr_t)AlreadyThere2->Size))
				{
					WhatHaveWeHere = *AlreadyThere2;
					DumpStackTraceToLog(AlreadyThere2->CallStackIndex);
					PLATFORM_BREAK();
				}
			}
		}
	}
}

FORCENOINLINE void FMallocDoubleFreeFinder::DumpStackTraceToLog(int32 StackIndex)
{
#if !NO_LOGGING
	// Walk the stack and dump it to the allocated memory.
	const SIZE_T StackTraceStringSize = 16384;
	ANSICHAR StackTraceString[StackTraceStringSize];
	{
		StackTraceString[0] = 0;
		uint32 CurrentDepth = 0;
		while (CurrentDepth < MallocDoubleFreeFinderMaxCallStackDepth && CallStackInfoArray[StackIndex].FramePointers[CurrentDepth] != 0)
		{
			FPlatformStackWalk::ProgramCounterToHumanReadableString(CurrentDepth, CallStackInfoArray[StackIndex].FramePointers[CurrentDepth], &StackTraceString[0], StackTraceStringSize, reinterpret_cast<FGenericCrashContext*>(0));
			FCStringAnsi::Strncat(StackTraceString, LINE_TERMINATOR_ANSI, StackTraceStringSize);
			CurrentDepth++;
		}
	}
	// Dump the error and flush the log.
	// ELogVerbosity::Error to make sure it gets printed in log for convenience.
	FDebug::LogFormattedMessageWithCallstack(LogOutputDevice.GetCategoryName(), __FILE__, __LINE__, TEXT("FMallocDoubleFreeFinder::DumpStackTraceToLog"), ANSI_TO_TCHAR(&StackTraceString[0]), ELogVerbosity::Error);
	GLog->Flush();
#endif
}


bool FMallocDoubleFreeFinder::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("DoubleFreeFinderCrash")))
	{
		void * test;
		test = FMemory::Malloc(128);
		FMemory::Free(test);
		FMemory::Free(test);
		return true;
	}

	return UsedMalloc->Exec(InWorld, Cmd, Ar);
}


FMalloc* FMallocDoubleFreeFinder::OverrideIfEnabled(FMalloc*InUsedAlloc)
{
	if (GMallocDoubleFreeFinderEnabled)
	{
		GMallocDoubleFreeFinder = new FMallocDoubleFreeFinder(InUsedAlloc);
		GMallocDoubleFreeFinder->Init();
		return GMallocDoubleFreeFinder;
	}
	return InUsedAlloc;
}
