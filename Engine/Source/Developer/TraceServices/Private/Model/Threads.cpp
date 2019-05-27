// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Threads.h"
#include "Model/ThreadsPrivate.h"
#include "Misc/ScopeLock.h"
#include "AnalysisServicePrivate.h"
#include "Common/StringStore.h"

namespace Trace
{

const FName FThreadProvider::ProviderName = "ThreadProvider";

FThreadProvider::FThreadProvider(IAnalysisSession& InSession)
	: Session(InSession)
{

}

FThreadProvider::~FThreadProvider()
{
	for (FThreadInfoInternal* Thread : SortedThreads)
	{
		delete Thread;
	}
}

void FThreadProvider::AddGameThread(uint32 Id)
{
	Session.WriteAccessCheck();

	check(!ThreadMap.Contains(Id));
	FThreadInfoInternal* ThreadInfo = new FThreadInfoInternal();
	ThreadInfo->Id = Id;
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(TPri_Normal);
	ThreadInfo->Name = Session.StoreString(*FName(NAME_GameThread).GetPlainNameString());
	ThreadInfo->FallbackSortOrder = SortedThreads.Num();
	ThreadInfo->IsGameThread = true;
	SortedThreads.Add(ThreadInfo);
	ThreadMap.Add(Id, ThreadInfo);
	++ModCount;
}

void FThreadProvider::AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority)
{
	Session.WriteAccessCheck();

	FThreadInfoInternal* ThreadInfo;
	if (!ThreadMap.Contains(Id))
	{
		ThreadInfo = new FThreadInfoInternal();
		ThreadInfo->Id = Id;
		ThreadInfo->FallbackSortOrder = SortedThreads.Num();
		SortedThreads.Add(ThreadInfo);
		ThreadMap.Add(Id, ThreadInfo);
	}
	else
	{
		ThreadInfo = ThreadMap[Id];
	}
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(Priority);
	ThreadInfo->Name = Session.StoreString(Name);
	SortThreads();
	++ModCount;
}

void FThreadProvider::SetThreadPriority(uint32 Id, EThreadPriority Priority)
{
	Session.WriteAccessCheck();

	check(ThreadMap.Contains(Id));
	FThreadInfoInternal* ThreadInfo = ThreadMap[Id];
	ThreadInfo->PrioritySortOrder = GetPrioritySortOrder(Priority);
	SortThreads();
	++ModCount;
}

void FThreadProvider::SetThreadGroup(uint32 Id, ETraceThreadGroup Group)
{
	Session.WriteAccessCheck();

	check(ThreadMap.Contains(Id));
	FThreadInfoInternal* ThreadInfo = ThreadMap[Id];
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
	++ModCount;
}

void FThreadProvider::EnumerateThreads(TFunctionRef<void(const FThreadInfo &)> Callback) const
{
	Session.ReadAccessCheck();

	for (const FThreadInfoInternal* Thread : SortedThreads)
	{
		FThreadInfo ThreadInfo;
		ThreadInfo.Id = Thread->Id;
		ThreadInfo.Name = Thread->Name;
		ThreadInfo.GroupName = Thread->GroupName;
		Callback(ThreadInfo);
	}
}

void FThreadProvider::SortThreads()
{
	Algo::SortBy(SortedThreads, [](const FThreadInfoInternal* In) -> const FThreadInfoInternal&
	{
		return *In;
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

const IThreadProvider& ReadThreadProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IThreadProvider>(FThreadProvider::ProviderName);
}

}