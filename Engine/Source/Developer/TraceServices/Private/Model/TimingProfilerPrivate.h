// Copyright Epic Games, Inc. All Rights Reserved.

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
	, public ITimingProfilerTimerReader
{
public:
	typedef TMonotonicTimeline<FTimingProfilerEvent> TimelineInternal;

	FTimingProfilerProvider(IAnalysisSession& InSession);
	virtual ~FTimingProfilerProvider();
	uint32 AddCpuTimer(const TCHAR* Name);
	uint32 AddGpuTimer(const TCHAR* Name);
	void SetTimerName(uint32 TimerId, const TCHAR* Name);
	uint32 AddMetadata(uint32 MasterTimerId, TArray<uint8>&& Metadata);
	TimelineInternal& EditCpuThreadTimeline(uint32 ThreadId);
	TimelineInternal& EditGpuTimeline();
	TimelineInternal& EditGpu2Timeline();
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const override;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const override;
	virtual bool GetGpu2TimelineIndex(uint32& OutTimelineIndex) const override;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual uint32 GetTimelineCount() const override { return Timelines.Num(); }
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual void ReadTimers(TFunctionRef<void(const ITimingProfilerTimerReader&)> Callback) const override;
	virtual ITable<FTimingProfilerAggregatedStats>* CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const override;
	virtual ITimingProfilerButterfly* CreateButterfly(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const override;
	virtual const FTimingProfilerTimer* GetTimer(uint32 TimerId) const override;
	virtual uint32 GetTimerCount() const override;
	virtual TArrayView<const uint8> GetMetadata(uint32 TimerId) const override;

private:
	FTimingProfilerTimer& AddTimerInternal(const TCHAR* Name, bool IsGpuEvent);

	struct FMetadata 
	{
		TArray<uint8> Payload;
		uint32 TimerId;
	};

	IAnalysisSession& Session;
	TArray<FMetadata> Metadatas;
	TArray<FTimingProfilerTimer> Timers;
	TArray<TSharedRef<TimelineInternal>> Timelines;
	TMap<uint32, uint32> CpuThreadTimelineIndexMap;
	uint32 GpuTimelineIndex = 0;
	uint32 Gpu2TimelineIndex = 1;
	TTableLayout<FTimingProfilerAggregatedStats> AggregatedStatsTableLayout;
};

}