// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookProfiling.h"
#include "CookOnTheSide/CookLog.h"

#if OUTPUT_COOKTIMING || ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"
#endif

#if OUTPUT_COOKTIMING
#include <Containers/AllocatorFixedSizeFreeList.h>

UE_TRACE_CHANNEL_DEFINE(CookChannel)

struct FHierarchicalTimerInfo
{
public:
	FHierarchicalTimerInfo(const FHierarchicalTimerInfo& InTimerInfo) = delete;
	FHierarchicalTimerInfo(FHierarchicalTimerInfo&& InTimerInfo) = delete;

	explicit FHierarchicalTimerInfo(const char* InName, uint16 InId)
	:	Id(InId)
	,	Name(InName)
	{
	}

	~FHierarchicalTimerInfo()
	{
		ClearChildren();
	}

	void ClearChildren()
	{
		for (FHierarchicalTimerInfo* Child = FirstChild; Child;)
		{
			FHierarchicalTimerInfo* NextChild = Child->NextSibling;

			DestroyAndFree(Child);

			Child = NextChild;
		}
		FirstChild = nullptr;
	}
	FHierarchicalTimerInfo* GetChild(int InId, const char* InName)
	{
		for (FHierarchicalTimerInfo* Child = FirstChild; Child;)
		{
			if (Child->Id == InId)
				return Child;

			Child = Child->NextSibling;
		}

		FHierarchicalTimerInfo* Child = AllocNew(InName, InId);

		Child->NextSibling	= FirstChild;
		FirstChild			= Child;

		return Child;
	}
	

	uint32							HitCount = 0;
	uint16							Id = 0;
	bool							IncrementDepth = true;
	double							Length = 0;
	const char*						Name;

	FHierarchicalTimerInfo*			FirstChild = nullptr;
	FHierarchicalTimerInfo*			NextSibling = nullptr;

private:
	static FHierarchicalTimerInfo*	AllocNew(const char* InName, uint16 InId);
	static void						DestroyAndFree(FHierarchicalTimerInfo* InPtr);
};

static FHierarchicalTimerInfo RootTimerInfo("Root", 0);
static FHierarchicalTimerInfo* CurrentTimerInfo = &RootTimerInfo;
static TAllocatorFixedSizeFreeList<sizeof(FHierarchicalTimerInfo), 256> TimerInfoAllocator;

FHierarchicalTimerInfo* FHierarchicalTimerInfo::AllocNew(const char* InName, uint16 InId)
{
	return new(TimerInfoAllocator.Allocate()) FHierarchicalTimerInfo(InName, InId);
}

void FHierarchicalTimerInfo::DestroyAndFree(FHierarchicalTimerInfo* InPtr)
{
	InPtr->~FHierarchicalTimerInfo();
	TimerInfoAllocator.Free(InPtr);
}

FScopeTimer::FScopeTimer(int InId, const char* InName, bool IncrementScope /*= false*/ )
{
	checkSlow(IsInGameThread());

	HierarchyTimerInfo = CurrentTimerInfo->GetChild(InId, InName);

	HierarchyTimerInfo->IncrementDepth = IncrementScope;

	PrevTimerInfo		= CurrentTimerInfo;
	CurrentTimerInfo	= HierarchyTimerInfo;
}

void FScopeTimer::Start()
{
	if (StartTime)
	{
		return;
	}

	StartTime = FPlatformTime::Cycles64();
}

void FScopeTimer::Stop()
{
	if (!StartTime)
	{
		return;
	}

	HierarchyTimerInfo->Length += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
	++HierarchyTimerInfo->HitCount;

	StartTime = 0;
}

FScopeTimer::~FScopeTimer()
{
	Stop();

	check(CurrentTimerInfo == HierarchyTimerInfo);
	CurrentTimerInfo = PrevTimerInfo;
}

void OutputHierarchyTimers(const FHierarchicalTimerInfo* TimerInfo, int32 Depth)
{
	FString TimerName(TimerInfo->Name);

	static const TCHAR LeftPad[] = TEXT("                                ");
	const SIZE_T PadOffset = FMath::Max<int>(UE_ARRAY_COUNT(LeftPad) - 1 - Depth * 2, 0);

	UE_LOG(LogCook, Display, TEXT("  %s%s: %.3fs (%u)"), &LeftPad[PadOffset], *TimerName, TimerInfo->Length, TimerInfo->HitCount);

	// We need to print in reverse order since the child list begins with the most recently added child

	TArray<const FHierarchicalTimerInfo*> Stack;

	for (const FHierarchicalTimerInfo* Child = TimerInfo->FirstChild; Child; Child = Child->NextSibling)
	{
		Stack.Add(Child);
	}

	const int32 ChildDepth = Depth + TimerInfo->IncrementDepth;

	for (size_t i = Stack.Num(); i > 0; --i)
	{
		OutputHierarchyTimers(Stack[i - 1], ChildDepth);
	}
}

void OutputHierarchyTimers()
{
	UE_LOG(LogCook, Display, TEXT("Hierarchy Timer Information:"));

	OutputHierarchyTimers(&RootTimerInfo, 0);
}

void ClearHierarchyTimers()
{
	RootTimerInfo.ClearChildren();
}
#endif
