// Copyright Epic Games, Inc. All Rights Reserved.
#include "TraceServices/Model/Memory.h"
#include "Model/MemoryPrivate.h"

namespace Trace
{

FName FMemoryProvider::ProviderName(TEXT("MemoryProvider"));

FMemoryProvider::FMemoryProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, TagsSerial(0)
{
	TagDescs.Reserve(256);
}

void FMemoryProvider::AddEventSpec(FMemoryTagId TagId, const TCHAR* Name, FMemoryTagId ParentTagId)
{
	if (TagId == FMemoryTag::InvalidTagId)
	{
		// invalid tag id
		return;
	}

	if (TagDescs.Contains(TagId))
	{
		// tag already registered
		return;
	}

	TagDescs.Add(TagId, FMemoryTag{ TagId, FString(Name), ParentTagId, 0 });
	++TagsSerial;
}

void FMemoryProvider::AddTrackerSpec(FMemoryTrackerId TrackerId, const TCHAR* Name)
{
	if (TrackerId < 0 || TrackerId > FMemoryTracker::MaxValidTrackerId)
	{
		// invalid tracker id
		return;
	}

	if (TrackerDescs.Contains(TrackerId))
	{
		// tracker already registerd
		return;
	}

	TrackerDescs.Add(TrackerId, FMemoryTracker{ TrackerId, FString(Name) });
}

void FMemoryProvider::AddTagSnapshot(FMemoryTrackerId TrackerId, double Time, const int64* Tags, const FMemoryTagSample* Values, uint32 TagCount)
{
	if (TrackerId < 0 || TrackerId >= TrackerDescs.Num())
	{
		// tracker id is invalid or not yet registered
		return;
	}

	if (TrackerId >= Trackers.Num())
	{
		Trackers.AddDefaulted(TrackerId - Trackers.Num() + 1);
	}

	FTrackerData& TrackerData = Trackers[TrackerId];
	FMemoryTracker& Tracker = TrackerDescs[TrackerId];

	TrackerData.SampleTimes.Push(Time);
	const int32 SampleCount = TrackerData.SampleTimes.Num();

	for (uint32 TagIndex = 0; TagIndex < TagCount; ++TagIndex)
	{
		const FMemoryTagId TagId = FMemoryTagId(Tags[TagIndex]);

		FTagSampleData* TagSamples = TrackerData.Samples.Find(TagId);
		if (!TagSamples)
		{
			TagSamples = &TrackerData.Samples.Add(TagId, FTagSampleData(Session.GetLinearAllocator()));
		}

		auto& TagValues = TagSamples->Values;

		// Add "past values". If new value is added, we need to ensure we have
		// the same number of elements in the value array as in the timestamp array.
		int32 CurrentSampleCount = int32(TagValues.Num());
		if (CurrentSampleCount < SampleCount - 1)
		{
			const FMemoryTagSample LastValue = (CurrentSampleCount > 0) ? TagValues.Last() : FMemoryTagSample { 0 };
			for (int32 ValueIndex = SampleCount - 1; ValueIndex > CurrentSampleCount; --ValueIndex)
			{
				TagValues.PushBack() = LastValue;
			}
		}

		TagValues.PushBack() = Values[TagIndex];
		check(int32(TagValues.Num()) == SampleCount);

		if (!TagSamples->TagPtr)
		{
			// Cache pointer to FMemoryTag to avoid further lookups for this tag.
			TagSamples->TagPtr = TagDescs.Find(TagId);
		}

		if (TagSamples->TagPtr)
		{
			const uint64 TrackerFlag = 1ULL << TrackerId;
			if ((TagSamples->TagPtr->Trackers & TrackerFlag) == 0)
			{
				TagSamples->TagPtr->Trackers |= TrackerFlag;
				++TagsSerial;
			}
		}
	}
}

uint32 FMemoryProvider::GetTagSerial() const
{
	return TagsSerial;
}

uint32 FMemoryProvider::GetTagCount() const
{
	return TagDescs.Num();
}

void FMemoryProvider::EnumerateTags(TFunctionRef<void(const FMemoryTag&)> Callback) const
{
	for (auto& Tag : TagDescs)
	{
		Callback(Tag.Value);
	}
}

const FMemoryTag* FMemoryProvider::GetTag(FMemoryTagId TagId) const
{
	return TagDescs.Find(TagId);
}

uint32 FMemoryProvider::GetTrackerCount() const
{
	return TrackerDescs.Num();
}

void FMemoryProvider::EnumerateTrackers(TFunctionRef<void(const FMemoryTracker&)> Callback) const
{
	for (auto& Desc : TrackerDescs)
	{
		Callback(Desc.Value);
	}
}

uint64 FMemoryProvider::GetTagSampleCount(FMemoryTrackerId TrackerId, FMemoryTagId TagId) const
{
	if (TrackerId < 0 || TrackerId >= Trackers.Num())
	{
		// invalid tracker id
		return 0;
	}

	const FTagSampleData* TagSamples = Trackers[TrackerId].Samples.Find(TagId);
	if (!TagSamples)
	{
		// invalid tag id
		return 0;
	}

	return TagSamples->Values.Num();
}

void FMemoryProvider::EnumerateTagSamples(FMemoryTrackerId TrackerId, FMemoryTagId TagId, double StartTime, double EndTime, bool bIncludeRangeNeighbours, TFunctionRef<void(double Time, double Duration, const FMemoryTagSample&)> Callback) const
{
	if (TrackerId < 0 || TrackerId >= Trackers.Num())
	{
		// invalid tracker id
		return;
	}

	const FTagSampleData* TagSamples = Trackers[TrackerId].Samples.Find(TagId);
	if (!TagSamples)
	{
		// invalid tag id
		return;
	}

	const TArray<double>& SampleTimes = Trackers[TrackerId].SampleTimes;
	const TPagedArray<FMemoryTagSample>& SampleValues = TagSamples->Values;

	int64 IndexStart = Algo::LowerBound(SampleTimes, StartTime);
	int64 IndexEnd = Algo::UpperBound(SampleTimes, EndTime);

	if (bIncludeRangeNeighbours)
	{
		IndexStart = FMath::Max(IndexStart - 1, (int64)0);
		IndexEnd = FMath::Min(IndexEnd + 1, (int64)SampleValues.Num());
	}

	const bool bEnumerateLastSample = (IndexEnd == (int64)SampleValues.Num());
	if (bEnumerateLastSample)
	{
		// Skip last sample from regular enumeration.
		--IndexEnd;
	}

	for (int64 SampleIndex = IndexStart; SampleIndex < IndexEnd; ++SampleIndex)
	{
		const double Time = SampleTimes[SampleIndex];
		const double Duration = SampleTimes[SampleIndex + 1] - Time;
		Callback(Time, Duration, SampleValues[SampleIndex]);
	}

	if (bEnumerateLastSample)
	{
		// Last sample has 0 duration.
		Callback(SampleTimes[IndexEnd], 0.0, SampleValues[IndexEnd]);
	}
}

const IMemoryProvider& ReadMemoryProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IMemoryProvider>(FMemoryProvider::ProviderName);
}

} // namespace Trace
