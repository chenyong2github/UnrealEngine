// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Frames.h"
#include "Model/FramesPrivate.h"
#include "AnalysisServicePrivate.h"
#include <limits>

namespace Trace
{

const FName FFrameProvider::ProviderName("FrameProvider");

FFrameProvider::FFrameProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
	for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
	{
		Frames.Emplace(Session.GetLinearAllocator(), 65536);
	}
}

uint64 FFrameProvider::GetFrameCount(ETraceFrameType FrameType) const
{
	Session.ReadAccessCheck();
	return Frames[FrameType].Num();
}

void FFrameProvider::EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const
{
	Session.ReadAccessCheck();

	End = FMath::Min(End, Frames[FrameType].Num());
	if (Start >= End)
	{
		return;
	}
	for (auto Iterator = Frames[FrameType].GetIteratorFromItem(Start); Iterator; ++Iterator)
	{
		Callback(*Iterator);
	}
}

const FFrame* FFrameProvider::GetFrame(ETraceFrameType FrameType, uint64 Index) const
{
	Session.ReadAccessCheck();

	TPagedArray<FFrame> FramesOfType = Frames[FrameType];
	if (Index < FramesOfType.Num())
	{
		return &FramesOfType[Index];
	}
	else
	{
		return nullptr;
	}
}

void FFrameProvider::BeginFrame(ETraceFrameType FrameType, double Time)
{
	Session.WriteAccessCheck();
	
	uint64 Index = Frames[FrameType].Num();
	FFrame& Frame = Frames[FrameType].PushBack();
	Frame.StartTime = Time;
	Frame.EndTime = std::numeric_limits<double>::infinity();
	Frame.Index = Index;
	Frame.FrameType = FrameType;

	FrameStartTimes[FrameType].Add(Time);

	Session.UpdateDurationSeconds(Time);
}

void FFrameProvider::EndFrame(ETraceFrameType FrameType, double Time)
{
	Session.WriteAccessCheck();
	FFrame& Frame = Frames[FrameType][Frames[FrameType].Num() - 1];
	Frame.EndTime = Time;
	Session.UpdateDurationSeconds(Time);
}

const IFrameProvider& ReadFrameProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IFrameProvider>(FFrameProvider::ProviderName);
}

}