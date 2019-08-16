// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/TimingProfiler.h"
#include "Model/TimingProfilerPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/StringStore.h"
#include "Common/TimelineStatistics.h"

namespace Trace
{

FTimingProfilerProvider::FTimingProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
	Timelines.Add(MakeShared<TimelineInternal>(Session.GetLinearAllocator()));
}

FTimingProfilerProvider::~FTimingProfilerProvider()
{
}

uint32 FTimingProfilerProvider::AddCpuTimer(const TCHAR* Name)
{
	Session.WriteAccessCheck();

	FTimingProfilerTimer& Timer = AddTimerInternal(Name, false);
	return Timer.Id;
}

void FTimingProfilerProvider::SetTimerName(uint32 TimerId, const TCHAR* Name)
{
	Session.WriteAccessCheck();
	
	FTimingProfilerTimer& Timer = Timers[TimerId];
	Timer.Name = Session.StoreString(Name);
	uint32 NameHash = 0;
	for (const TCHAR* c = Name; *c; ++c)
	{
		NameHash = (NameHash + *c) * 0x2c2c57ed;
	}
	Timer.NameHash = NameHash;
}

uint32 FTimingProfilerProvider::AddGpuTimer(const TCHAR* Name)
{
	Session.WriteAccessCheck();

	FTimingProfilerTimer& Timer = AddTimerInternal(Name, true);
	return Timer.Id;
}

FTimingProfilerTimer& FTimingProfilerProvider::AddTimerInternal(const TCHAR* Name, bool IsGpuTimer)
{
	FTimingProfilerTimer& Timer = Timers.AddDefaulted_GetRef();
	Timer.Id = Timers.Num() - 1;
	Timer.Name = Session.StoreString(Name);
	uint32 NameHash = 0;
	for (const TCHAR* c = Name; *c; ++c)
	{
		NameHash = (NameHash + *c) * 0x2c2c57ed;
	}
	Timer.NameHash = NameHash;
	Timer.IsGpuTimer = IsGpuTimer;
	return Timer;
}

FTimingProfilerProvider::TimelineInternal& FTimingProfilerProvider::EditCpuThreadTimeline(uint32 ThreadId)
{
	Session.WriteAccessCheck();

	if (!CpuThreadTimelineIndexMap.Contains(ThreadId))
	{
		TSharedRef<TimelineInternal> Timeline = MakeShared<TimelineInternal>(Session.GetLinearAllocator());
		uint32 TimelineIndex = Timelines.Num();
		CpuThreadTimelineIndexMap.Add(ThreadId, TimelineIndex);
		Timelines.Add(Timeline);
		return Timeline.Get();
	}
	else
	{
		uint32 TimelineIndex = CpuThreadTimelineIndexMap[ThreadId];
		return Timelines[TimelineIndex].Get();
	}
}

FTimingProfilerProvider::TimelineInternal& FTimingProfilerProvider::EditGpuTimeline()
{
	Session.WriteAccessCheck();

	return Timelines[GpuTimelineIndex].Get();
}

bool FTimingProfilerProvider::GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const
{
	Session.ReadAccessCheck();

	if (CpuThreadTimelineIndexMap.Contains(ThreadId))
	{
		OutTimelineIndex = CpuThreadTimelineIndexMap[ThreadId];
		return true;
	}
	return false;
}

bool FTimingProfilerProvider::GetGpuTimelineIndex(uint32& OutTimelineIndex) const
{
	Session.ReadAccessCheck();

	OutTimelineIndex = GpuTimelineIndex;
	return true;
}

bool FTimingProfilerProvider::ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline &)> Callback) const
{
	Session.ReadAccessCheck();

	if (Index < uint32(Timelines.Num()))
	{
		Callback(*Timelines[Index]);
		return true;
	}
	else
	{
		return false;
	}
}

void FTimingProfilerProvider::EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const
{
	Session.ReadAccessCheck();

	for (const auto& Timeline : Timelines)
	{
		Callback(*Timeline);
	}
}

void FTimingProfilerProvider::ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const
{
	Session.ReadAccessCheck();

	Callback(Timers.GetData(), Timers.Num());
}

ITable<FTimingProfilerAggregatedStats>* FTimingProfilerProvider::CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const
{
	Session.ReadAccessCheck();

	TArray<const TimelineInternal*> IncludedTimelines;
	if (IncludeGpu)
	{
		IncludedTimelines.Add(&Timelines[GpuTimelineIndex].Get());
	}
	for (const auto& KV : CpuThreadTimelineIndexMap)
	{
		if (CpuThreadFilter(KV.Key))
		{
			IncludedTimelines.Add(&Timelines[KV.Value].Get());
		}
	}

	auto BucketMappingFunc = [this](const TimelineInternal::EventType& Event) -> const FTimingProfilerTimer*
	{
		return &Timers[Event.TimerIndex];
	};

	TMap<const FTimingProfilerTimer*, FAggregatedTimingStats> Aggregation;
	FTimelineStatistics::CreateAggregation(IncludedTimelines, BucketMappingFunc, IntervalStart, IntervalEnd, Aggregation);
	TTable<FAggregatedStatsTableLayout>* Table = new TTable<FAggregatedStatsTableLayout>();
	for (const auto& KV : Aggregation)
	{
		FTimingProfilerAggregatedStats& Row = Table->AddRow();
		*static_cast<FAggregatedTimingStats*>(&Row) = KV.Value;
		Row.Timer = KV.Key;
	}
	return Table;
}

}
