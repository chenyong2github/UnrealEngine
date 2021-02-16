// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryAlloc.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::FMemoryAlloc(double InStartTime, double InEndTime, uint64 InAddress, uint64 InSize, const TCHAR* InTag, const TraceServices::FCallstack* InCallstack)
: StartTime(InStartTime)
, EndTime(InEndTime)
, Address(InAddress)
, Size(InSize)
, Tag(InTag)
, Callstack(InCallstack)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::~FMemoryAlloc()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemoryAlloc::GetFullCallstack() const
{
	FString Tooltip;
	if (Callstack)
	{
		check(Callstack->Num() > 0);
		for (uint32 Index = 0; Index < Callstack->Num(); ++Index)
		{
			const TraceServices::FStackFrame* Frame = Callstack->Frame(Index);
			check(Frame != nullptr);

			const TraceServices::ESymbolQueryResult Result = Frame->Symbol->GetResult();
			if (Result == TraceServices::ESymbolQueryResult::OK)
			{
				Tooltip.Appendf(TEXT("%s\n"), Frame->Symbol->Name);
			}
			else
			{
				Tooltip.Appendf(TEXT("%s\n"), QueryResultToString(Result));
			}
		}

		// Remove the last "\n"
		Tooltip.RemoveAt(Tooltip.Len() - 1, 1, false);
	}

	if (Tooltip.Len() == 0)
	{
		Tooltip.Append(TEXT("N/A"));
	}

	return FText::FromString(Tooltip);
}

} // namespace Insights
