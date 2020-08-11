// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FMallocFrameProfiler.cpp: Memoory tracking allocator
=============================================================================*/
#include "HAL/MallocFrameProfiler.h"
#include "Logging/LogMacros.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformStackWalk.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMallocFrameProfiler, Log, All);
DEFINE_LOG_CATEGORY(LogMallocFrameProfiler);

CORE_API FMallocFrameProfiler* GMallocFrameProfiler;
CORE_API bool GMallocFrameProfilerEnabled = false;

FMallocFrameProfiler::FMallocFrameProfiler(FMalloc* InMalloc)
	: FMallocCallstackHandler(InMalloc)
	, bEnabled(false)
	, FrameCount(0)
{
}

void FMallocFrameProfiler::Init()
{
	if (Initialized)
	{
		return;
	}
	FMallocCallstackHandler::Init();

	TrackedCurrentAllocations.Reserve(8000000);
	CallStackIndexUsageCountArray.Reserve(8000000);
}

void FMallocFrameProfiler::TrackMalloc(void* Ptr, uint32 Size, int32 CallStackIndex)
{
	if (Ptr != nullptr)
	{
		if (CallStackIndexUsageCountArray.Num() <= CallStackIndex)
		{
			CallStackIndexUsageCountArray.AddDefaulted(CallStackIndex - CallStackIndexUsageCountArray.Num() + 1);
		}
		CallStackIndexUsageCountArray[CallStackIndex].CallStackIndex = CallStackIndex;

		TrackedCurrentAllocations.Add(Ptr, CallStackIndex);
	}
}

void FMallocFrameProfiler::TrackFree(void* Ptr, uint32 OldSize, int32 CallStackIndex)
{
	int32* CallStackIndexMalloc = TrackedCurrentAllocations.Find(Ptr);
	if (CallStackIndexMalloc!=nullptr)
	{
		if (CallStackIndexUsageCountArray.Num() <= *CallStackIndexMalloc)
		{
			// it cant be
			PLATFORM_BREAK();
		}

		CallStackIndexUsageCountArray[*CallStackIndexMalloc].UsageCount++;
	}
}

void FMallocFrameProfiler::TrackRealloc(void* OldPtr, void* NewPtr, uint32 NewSize, uint32 OldSize, int32 CallStackIndex)
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

bool FMallocFrameProfiler::IsDisabled()
{
	return FMallocCallstackHandler::IsDisabled() || !bEnabled;
}

void FMallocFrameProfiler::UpdateStats()
{
	UsedMalloc->UpdateStats();

	if (!bEnabled)
	{
		return;
	}

	FScopeLock Lock(&CriticalSection);
	TrackedCurrentAllocations.Reset();

	if (FrameCount > 0)
	{
		FrameCount--;
		return;
	}
	
	bEnabled = false;

	CallStackIndexUsageCountArray.Sort
	(
		[](const FCallStackUsageCount& A, const FCallStackUsageCount& B)
		{
			return A.UsageCount > B.UsageCount;
		}
	);

	for (int32 CallStackIndex=0; CallStackIndex< CallStackIndexUsageCountArray.Num(); CallStackIndex++)
	{
		UE_LOG(LogMallocFrameProfiler, Display, TEXT("---- Frame alloc count %d"), CallStackIndexUsageCountArray[CallStackIndex].UsageCount);
		DumpStackTraceToLog(CallStackIndexUsageCountArray[CallStackIndex].CallStackIndex);
		
		if (CallStackIndex == 15)
		{
			break;
		}

	}
	CallStackInfoArray.Reset();
	CallStackMapKeyToCallStackIndexMap.Reset();
	CallStackIndexUsageCountArray.Reset();
}

bool FMallocFrameProfiler::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("MallocFrameProfiler")))
	{
		if (!FParse::Value(Cmd, TEXT("FrameCount="), FrameCount))
		{
			FrameCount = 0;
		}
		bEnabled = true;
		return true;
	}

	return UsedMalloc->Exec(InWorld, Cmd, Ar);
}


FMalloc* FMallocFrameProfiler::OverrideIfEnabled(FMalloc*InUsedAlloc)
{
	if (GMallocFrameProfilerEnabled)
	{
		GMallocFrameProfiler = new FMallocFrameProfiler(InUsedAlloc);
		GMallocFrameProfiler->Init();
		return GMallocFrameProfiler;
	}
	return InUsedAlloc;
}
