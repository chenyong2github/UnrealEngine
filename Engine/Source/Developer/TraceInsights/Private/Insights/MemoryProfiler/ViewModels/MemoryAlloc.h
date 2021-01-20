// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/Callstack.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryAlloc
{
	friend class SMemAllocTableTreeView;

public:
	FMemoryAlloc(double InStartTime, double InEndTime, uint64 InAddress, uint64 InSize, const TCHAR* InTag, const TraceServices::FCallstack* InCallstack);
	~FMemoryAlloc();

	double GetStartTime() const { return StartTime; }
	double GetEndTime() const { return EndTime; }
	double GetDuration() const { return EndTime - StartTime; }
	uint64 GetAddress() const { return Address; }
	uint64 GetSize() const { return Size; }
	const TCHAR* GetTag() const { return Tag; }
	const TraceServices::FCallstack* GetCallstack() const { return Callstack; }
	FText GetFullCallstack() const;

private:
	double StartTime;
	double EndTime;
	uint64 Address;
	uint64 Size;
	const TCHAR* Tag;
	const TraceServices::FCallstack* Callstack;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
