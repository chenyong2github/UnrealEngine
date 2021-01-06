// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryAlloc.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::FMemoryAlloc(double InStartTime, double InEndTime, uint64 InAddress, uint64 InSize, FMemoryTagId InMemTag, const TraceServices::FCallstack* InCallstack)
: StartTime(InStartTime)
, EndTime(InEndTime)
, Address(InAddress)
, Size(InSize)
, MemTag(InMemTag)
, Callstack(InCallstack)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::~FMemoryAlloc()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
