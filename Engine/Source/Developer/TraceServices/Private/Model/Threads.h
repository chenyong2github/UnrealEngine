// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace Trace
{
class FAnalysisSessionLock;
class FStringStore;

class FThreadProvider
	: public IThreadProvider
{
public:
	FThreadProvider(const FAnalysisSessionLock& SessionLock, FStringStore& StringStore);
	~FThreadProvider();
	void EnsureThreadExists(uint32 Id);
	void AddGameThread(uint32 Id);
	void AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority);
	void SetThreadPriority(uint32 Id, EThreadPriority Priority);
	void SetThreadGroup(uint32 Id, ETraceThreadGroup Group);
	virtual uint64 GetModCount() const override { return ModCount; }
	virtual void EnumerateThreads(TFunctionRef<void(const FThreadInfo&)> Callback) const override;

private:
	struct FThreadInfoInternal
	{
		uint32 Id = 0;
		const TCHAR* Name = nullptr;
		uint32 GroupSortOrder = ~0u;
		uint32 PrioritySortOrder = ~0u;
		uint32 FallbackSortOrder = ~0u;
		const TCHAR* GroupName = nullptr;
		bool IsGameThread = false;

		bool operator<(const FThreadInfoInternal& Other) const;
	};

	void SortThreads();
	static uint32 GetPrioritySortOrder(EThreadPriority ThreadPriority);
	static uint32 GetGroupSortOrder(ETraceThreadGroup ThreadGroup);

	const FAnalysisSessionLock& SessionLock;
	FStringStore& StringStore;
	uint64 ModCount = 0;
	TMap<uint32, FThreadInfoInternal*> ThreadMap;
	TArray<FThreadInfoInternal*> SortedThreads;
};

}