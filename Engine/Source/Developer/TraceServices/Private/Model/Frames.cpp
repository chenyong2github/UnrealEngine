// Copyright Epic Games, Inc. All Rights Reserved.

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

// Pulled from Algo/BinarySearch.h, which by default truncates to int32.
// @TODO: remove when LowerBound supports 64 bit results
template <typename RangeValueType, typename PredicateValueType, typename ProjectionType, typename SortPredicateType>
static int64 LowerBound64(RangeValueType* First, const int64 Num, const PredicateValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate)
{
	// Current start of sequence to check
	int64 Start = 0;
	// Size of sequence to check
	int64 Size = Num;

	// With this method, if Size is even it will do one more comparison than necessary, but because Size can be predicted by the CPU it is faster in practice
	while (Size > 0)
	{
		const int64 LeftoverSize = Size % 2;
		Size = Size / 2;

		const int64 CheckIndex = Start + Size;
		const int64 StartIfLess = CheckIndex + LeftoverSize;

		auto&& CheckValue = Invoke(Projection, First[CheckIndex]);
		Start = SortPredicate(CheckValue, Value) ? StartIfLess : Start;
	}
	return Start;
}

bool FFrameProvider::GetFrameFromTime(ETraceFrameType FrameType, double Time, FFrame& OutFrame) const
{
	int64 LowerBound = LowerBound64(FrameStartTimes[FrameType].GetData(), FrameStartTimes[FrameType].Num(), Time, FIdentityFunctor(), TLess<double>());
	if(FrameStartTimes[FrameType].IsValidIndex(LowerBound) && LowerBound > 0)
	{
		OutFrame.Index = LowerBound;
		OutFrame.StartTime = FrameStartTimes[FrameType][LowerBound - 1];
		OutFrame.EndTime = FrameStartTimes[FrameType][LowerBound];
		OutFrame.FrameType = FrameType;
		return true;
	}

	return false;
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
	// If the EndFrame event is the first event that comes through
	if (Frames[FrameType].Num() == 0)
	{
		return;
	}
	FFrame& Frame = Frames[FrameType][Frames[FrameType].Num() - 1];
	Frame.EndTime = Time;
	Session.UpdateDurationSeconds(Time);
}

const IFrameProvider& ReadFrameProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IFrameProvider>(FFrameProvider::ProviderName);
}

}