// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace Trace
{
class FAnalysisSessionLock;

class FThreadProvider
	: public IThreadProvider
{
public:
	FThreadProvider(const FAnalysisSessionLock& SessionLock);
	void EnsureThreadExists(uint32 Id);
	void AddGameThread(uint32 Id);
	void AddThread(uint32 Id, const FString& Name, EThreadPriority Priority);
	void SetThreadPriority(uint32 Id, EThreadPriority Priority);
	void SetThreadGroup(uint32 Id, ETraceThreadGroup Group);

	virtual void EnumerateThreads(TFunctionRef<void(const FThreadInfo&)> Callback) const override;

private:
	struct FThreadInfoInternal
	{
		uint32 Id = 0;
		FString Name;
		uint32 GroupSortOrder = ~0ul;
		uint32 PrioritySortOrder = ~0ul;
		uint32 FallbackSortOrder = ~0ul;
		const TCHAR* GroupName = nullptr;
		bool IsGameThread = false;

		bool operator<(const FThreadInfoInternal& Other) const;
	};

	void SortThreads();
	static uint32 GetPrioritySortOrder(EThreadPriority ThreadPriority);
	static uint32 GetGroupSortOrder(ETraceThreadGroup ThreadGroup);

	const FAnalysisSessionLock& SessionLock;
	TMap<uint32, TSharedRef<FThreadInfoInternal>> ThreadMap;
	TArray<TSharedRef<FThreadInfoInternal>> SortedThreads;
};

}