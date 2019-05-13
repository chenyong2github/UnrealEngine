// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Frames.h"
#include "AnalysisServicePrivate.h"
#include <limits>

namespace Trace
{

FFrameProvider::FFrameProvider(FSlabAllocator& InAllocator, FAnalysisSessionLock& InSessionLock)
	: Allocator(InAllocator)
	, SessionLock(InSessionLock)
{
	for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
	{
		Frames.Emplace(Allocator, 65536);
	}
}

uint64 FFrameProvider::GetFrameCount(ETraceFrameType FrameType) const
{
	SessionLock.ReadAccessCheck();
	return Frames[FrameType].Num();
}

void FFrameProvider::EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const
{
	SessionLock.ReadAccessCheck();

	End = FMath::Min(End, Frames[FrameType].Num());
	if (Start >= End)
	{
		return;
	}
	auto Iterator = Frames[FrameType].GetIteratorFromItem(Start);
	const FFrame* Frame = Iterator.GetCurrentItem();
	for (uint64 Index = Start; Index < End; ++Index)
	{
		Callback(*Frame);
		Frame = Iterator.NextItem();
	}
}

void FFrameProvider::BeginFrame(ETraceFrameType FrameType, double Time)
{
	SessionLock.WriteAccessCheck();
	
	uint64 Index = Frames[FrameType].Num();
	FFrame& Frame = Frames[FrameType].PushBack();
	Frame.StartTime = Time;
	Frame.EndTime = std::numeric_limits<double>::infinity();
	Frame.Index = Index;
}

void FFrameProvider::EndFrame(ETraceFrameType FrameType, double Time)
{
	SessionLock.WriteAccessCheck();
	FFrame& Frame = Frames[FrameType][Frames[FrameType].Num() - 1];
	Frame.EndTime = Time;
}

}