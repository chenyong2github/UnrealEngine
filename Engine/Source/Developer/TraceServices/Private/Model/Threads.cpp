// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Threads.h"
#include "Misc/ScopeLock.h"
#include "AnalysisServicePrivate.h"

namespace Trace
{

FThreadProvider::FThreadProvider(const FAnalysisSessionLock& InSessionLock)
	: SessionLock(InSessionLock)
{

}

void FThreadProvider::EnsureThreadExists(uint32 Id)
{
	SessionLock.WriteAccessCheck();

	if (!ThreadMap.Contains(Id))
	{
		TSharedRef<FThreadInfoInternal> ThreadInfo = MakeShared<FThreadInfoInternal>();
		ThreadInfo->Id = Id;
		ThreadInfo->FallbackSortOrder = SortedThreads.Num();
		SortedThreads.Add(ThreadInfo);
		ThreadMap.Add(Id, ThreadInfo);
		SortThreads();
	}
}

void FThreadProvider::AddGameThread(uint32 Id)
{
	SessionLock.WriteAccessCheck();

	check(!ThreadMap.Contains(Id));
	TSharedRef<FThreadInfoInternal> ThreadInfo = MakeShared<FThreadInfoInternal>();
	ThreadInfo->Id = Id;
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(TPri_Normal);
	ThreadInfo->Name = *FName(NAME_GameThread).GetPlainNameString();
	ThreadInfo->FallbackSortOrder = SortedThreads.Num();
	ThreadInfo->IsGameThread = true;
	SortedThreads.Add(ThreadInfo);
	ThreadMap.Add(Id, ThreadInfo);
}

void FThreadProvider::AddThread(uint32 Id, const FString& Name, EThreadPriority Priority)
{
	SessionLock.WriteAccessCheck();

	TSharedPtr<FThreadInfoInternal> ThreadInfo;
	if (!ThreadMap.Contains(Id))
	{
		ThreadInfo = MakeShared<FThreadInfoInternal>();
		ThreadInfo->Id = Id;
		ThreadInfo->FallbackSortOrder = SortedThreads.Num();
		SortedThreads.Add(ThreadInfo.ToSharedRef());
		ThreadMap.Add(Id, ThreadInfo.ToSharedRef());
	}
	else
	{
		ThreadInfo = ThreadMap[Id];
	}
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(Priority);
	ThreadInfo->Name = Name;
	SortThreads();
}

void FThreadProvider::SetThreadPriority(uint32 Id, EThreadPriority Priority)
{
	SessionLock.WriteAccessCheck();

	check(ThreadMap.Contains(Id));
	TSharedRef<FThreadInfoInternal> ThreadInfo = ThreadMap[Id];
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(Priority);
	SortThreads();
}

void FThreadProvider::SetThreadGroup(uint32 Id, ETraceThreadGroup Group)
{
	SessionLock.WriteAccessCheck();

	check(ThreadMap.Contains(Id));
	TSharedRef<FThreadInfoInternal> ThreadInfo = ThreadMap[Id];
	ThreadInfo->GroupSortOrder = GetGroupSortOrder(Group);
	switch (Group)
	{
	case TraceThreadGroup_Render:
		ThreadInfo->GroupName = TEXT("Render");
		break;
	case TraceThreadGroup_TaskGraphHigh:
		ThreadInfo->GroupName = TEXT("TaskGraph (High Priority)");
		break;
	case TraceThreadGroup_TaskGraphNormal:
		ThreadInfo->GroupName = TEXT("TaskGraph (Normal Priority)");
		break;
	case TraceThreadGroup_TaskGraphLow:
		ThreadInfo->GroupName = TEXT("TaskGraph (Low Priority)");
		break;
	case TraceThreadGroup_ThreadPool:
		ThreadInfo->GroupName = TEXT("ThreadPool");
		break;
	case TraceThreadGroup_BackgroundThreadPool:
		ThreadInfo->GroupName = TEXT("ThreadPool (Background)");
		break;
	case TraceThreadGroup_LargeThreadPool:
		ThreadInfo->GroupName = TEXT("ThreadPool (Large)");
		break;
	case TraceThreadGroup_IOThreadPool:
		ThreadInfo->GroupName = TEXT("ThreadPool (IO)");
		break;
	}
	SortThreads();
}

void FThreadProvider::EnumerateThreads(TFunctionRef<void(const FThreadInfo &)> Callback) const
{
	SessionLock.ReadAccessCheck();

	for (const TSharedRef<FThreadInfoInternal>& Thread : SortedThreads)
	{
		FThreadInfo ThreadInfo;
		ThreadInfo.Id = Thread->Id;
		ThreadInfo.Name = *Thread->Name;
		ThreadInfo.GroupName = Thread->GroupName;
		Callback(ThreadInfo);
	}
}

void FThreadProvider::SortThreads()
{
	Algo::SortBy(SortedThreads, [](const TSharedRef<FThreadInfoInternal>& In) -> const FThreadInfoInternal&
	{
		return In.Get();
	});
}

uint32 FThreadProvider::GetGroupSortOrder(ETraceThreadGroup ThreadGroup)
{
	return uint32(ThreadGroup);
}

uint32 FThreadProvider::GetPrioritySortOrder(EThreadPriority ThreadPriority)
{
	switch (ThreadPriority)
	{
	case TPri_TimeCritical:
		return 0;
	case TPri_Highest:
		return 1;
	case TPri_AboveNormal:
		return 2;
	case TPri_Normal:
		return 3;
	case TPri_SlightlyBelowNormal:
		return 4;
	case TPri_BelowNormal:
		return 5;
	case TPri_Lowest:
		return 6;
	default:
		return 7;
	}
}

bool FThreadProvider::FThreadInfoInternal::operator<(const FThreadInfoInternal& Other) const
{
	if (IsGameThread == Other.IsGameThread)
	{
		if (GroupSortOrder == Other.GroupSortOrder)
		{
			if (PrioritySortOrder == Other.PrioritySortOrder)
			{
				return FallbackSortOrder < Other.FallbackSortOrder;
			}
			return PrioritySortOrder < Other.PrioritySortOrder;
		}
		return GroupSortOrder < Other.GroupSortOrder;
	}
	return IsGameThread;
}

}