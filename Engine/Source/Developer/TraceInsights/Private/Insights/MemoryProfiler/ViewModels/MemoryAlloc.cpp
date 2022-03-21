// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryAlloc.h"

#include "Insights/MemoryProfiler/ViewModels/CallstackFormatting.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::FMemoryAlloc()
: StartEventIndex(0)
, EndEventIndex(0)
, StartTime(0.0)
, EndTime(0.0)
, Address(0)
, Size(0)
, Tag(nullptr)
, Callstack(nullptr)
, RootHeap(0)
, bIsBlock(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemoryAlloc::~FMemoryAlloc()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemoryAlloc::GetFullCallstack() const
{
	if (!Callstack)
	{
		return FText::FromString(GetCallstackNotAvailableString());
	}

	if (Callstack->Num() == 0)
	{
		return FText::FromString(GetEmptyCallstackString());
	}

	TStringBuilder<1024> Tooltip;
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
	return FText::FromString(FString(Tooltip));
}

} // namespace Insights
