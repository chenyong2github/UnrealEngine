// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraceServices/Model/AllocationsProvider.h"
#include "TraceServices/Model/Callstack.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryAlloc
{
	friend class SMemAllocTableTreeView;

public:
	FMemoryAlloc();
	~FMemoryAlloc();

	int64 GetStartEventIndex() const { return int64(StartEventIndex); }
	int64 GetEndEventIndex() const { return int64(EndEventIndex); }
	int64 GetEventDistance() const { return int64(EndEventIndex) - int64(StartEventIndex); }
	double GetStartTime() const { return StartTime; }
	double GetEndTime() const { return EndTime; }
	double GetDuration() const { return EndTime - StartTime; }
	uint64 GetAddress() const { return Address; }
	uint64 GetPage() const { return Address & ~(4llu*1024-1); }
	int64 GetSize() const { return Size; }
	const TCHAR* GetTag() const { return Tag; }
	const TraceServices::FCallstack* GetCallstack() const { return Callstack; }
	FText GetFullCallstack() const;
	HeapId GetRootHeap() const { return RootHeap; };

private:
	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Address;
	int64 Size;
	const TCHAR* Tag;
	const TraceServices::FCallstack* Callstack;
	HeapId RootHeap;
	bool bIsBlock;
	bool bIsDecline;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
