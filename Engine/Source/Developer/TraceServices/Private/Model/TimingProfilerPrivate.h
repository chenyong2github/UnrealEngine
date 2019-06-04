// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/TimingProfiler.h"
#include "Common/SlabAllocator.h"
#include "Model/MonotonicTimeline.h"
#include "Model/Tables.h"

namespace Trace
{

class FAnalysisSessionLock;
class FStringStore;

class FTimingProfilerProvider
	: public ITimingProfilerProvider
{
public:
	typedef TMonotonicTimeline<FTimingProfilerEvent> TimelineInternal;

	FTimingProfilerProvider(IAnalysisSession& InSession);
	virtual ~FTimingProfilerProvider();
	uint32 AddCpuTimer(const TCHAR* Name);
	uint32 AddGpuTimer(const TCHAR* Name);
	void SetTimerName(uint32 TimerId, const TCHAR* Name);
	TimelineInternal& EditCpuThreadTimeline(uint32 ThreadId);
	TimelineInternal& EditGpuTimeline();
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const override;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const override;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual uint64 GetTimelineCount() const override { return Timelines.Num(); }
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual void ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const override;
	virtual ITable<FTimingProfilerAggregatedStats>* CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const override;

private:
	FTimingProfilerTimer& AddTimerInternal(const TCHAR* Name, bool IsGpuEvent);

	IAnalysisSession& Session;
	TArray<FTimingProfilerTimer> Timers;
	TArray<TSharedRef<TimelineInternal>> Timelines;
	TMap<uint32, uint32> CpuThreadTimelineIndexMap;
	uint32 GpuTimelineIndex = 0;

	UE_TRACE_TABLE_LAYOUT_BEGIN(FAggregatedStatsTableLayout, FTimingProfilerAggregatedStats)
		UE_TRACE_TABLE_PROJECTED_COLUMN(TableColumnType_CString, TEXT("Name"), [](const FTimingProfilerAggregatedStats& Row) { return Row.Timer->Name; })
		UE_TRACE_TABLE_COLUMN(InstanceCount, TEXT("Count"))
		UE_TRACE_TABLE_COLUMN(TotalInclusiveTime, TEXT("Incl"))
		UE_TRACE_TABLE_COLUMN(MinInclusiveTime, TEXT("I.Min"))
		UE_TRACE_TABLE_COLUMN(MaxInclusiveTime, TEXT("I.Max"))
		UE_TRACE_TABLE_COLUMN(AverageInclusiveTime, TEXT("I.Avg"))
		UE_TRACE_TABLE_COLUMN(MedianInclusiveTime, TEXT("I.Med"))
		UE_TRACE_TABLE_COLUMN(TotalExclusiveTime, TEXT("Excl"))
		UE_TRACE_TABLE_COLUMN(MinExclusiveTime, TEXT("E.Min"))
		UE_TRACE_TABLE_COLUMN(MaxExclusiveTime, TEXT("E.Max"))
		UE_TRACE_TABLE_COLUMN(AverageExclusiveTime, TEXT("E.Avg"))
		UE_TRACE_TABLE_COLUMN(MedianExclusiveTime, TEXT("E.Med"))
	UE_TRACE_TABLE_LAYOUT_END()
};

}