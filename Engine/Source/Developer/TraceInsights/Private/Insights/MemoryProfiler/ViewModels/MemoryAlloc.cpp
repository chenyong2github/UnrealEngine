// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryAlloc.h"
#include "CallstackFormatting.h"

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
	TStringBuilder<1024> Tooltip;
	if (Callstack)
	{
		check(Callstack->Num() > 0);
		const uint32 FramesNum = Callstack->Num();
		for (uint32 Index = 0; Index < FramesNum; ++Index)
		{
			const TraceServices::FStackFrame* Frame = Callstack->Frame(Index);
			check(Frame != nullptr);

			FormatStackFrame(*Frame, Tooltip, EStackFrameFormatFlags::Module | EStackFrameFormatFlags::FileAndLine);
			if (Index != (FramesNum - 1))
			{
				Tooltip << TEXT("\n");
			}
		}
	}

	if (Tooltip.Len() == 0)
	{
		Tooltip.Append(TEXT("N/A"));
	}

	return FText::FromString(FString(Tooltip));
}

} // namespace Insights
