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
	static const TCHAR* DisplayStrings[] = {
	TEXT("Pending..."),
	TEXT("Not found"),
	TEXT("N/A"),
	};

	FString Tooltip;
	if (Callstack)
	{
		check(Callstack->Num() > 0);
		for (uint32 Index = 0; Index < Callstack->Num(); ++Index)
		{
			const TraceServices::FStackFrame* Frame = Callstack->Frame(Index);
			check(Frame != nullptr);

			const TraceServices::QueryResult Result = Frame->Symbol->Result.load(std::memory_order_acquire);
			switch (Result)
			{
			case TraceServices::QueryResult::QR_NotLoaded:
				Tooltip.Appendf(TEXT("%s\n"), DisplayStrings[0]); // pending
				break;
			case TraceServices::QueryResult::QR_NotFound:
				Tooltip.Appendf(TEXT("%s\n"), DisplayStrings[1]); // not found
				break;
			case TraceServices::QueryResult::QR_OK:
				Tooltip.Appendf(TEXT("%s\n"), Frame->Symbol->Name);
				break;
			}
		}

		// Remove the last "\n"
		Tooltip.RemoveAt(Tooltip.Len() - 1, 1, false);
	}

	if (Tooltip.Len() == 0)
	{
		Tooltip.Append(DisplayStrings[2]);
	}

	return FText::FromString(Tooltip);
}

} // namespace Insights
