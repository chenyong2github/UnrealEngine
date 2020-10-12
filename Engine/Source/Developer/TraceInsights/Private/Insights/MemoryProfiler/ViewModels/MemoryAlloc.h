// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryAlloc
{
	friend class SMemAllocTableTreeView;

public:
	FMemoryAlloc(double InStartTime, double InEndTime, uint64 InAddress, uint64 InSize, FMemoryTagId InMemTag, uint64 InBacktraceId);
	~FMemoryAlloc();

	double GetStartTime() const { return StartTime; }
	double GetEndTime() const { return EndTime; }
	double GetDuration() const { return EndTime - StartTime; }
	uint64 GetAddress() const { return Address; }
	uint64 GetSize() const { return Size; }
	FMemoryTagId GetMemTag() const { return MemTag; }
	uint64 GetBacktraceId() const { return BacktraceId; }

private:
	double StartTime;
	double EndTime;
	uint64 Address;
	uint64 Size;
	FMemoryTagId MemTag;
	uint64 BacktraceId;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
