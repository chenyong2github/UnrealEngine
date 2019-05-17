// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Common/SlabAllocator.h"
#include "Model/MonotonicTimeline.h"

namespace Trace
{

class FAnalysisSessionLock;

class FTimingProfilerProvider
	: public ITimingProfilerProvider
{
public:
	typedef TMonotonicTimeline<FTimingProfilerEvent> TimelineInternal;

	FTimingProfilerProvider(FSlabAllocator& Allocator, FAnalysisSessionLock& InSessionLock);
	virtual ~FTimingProfilerProvider();
	uint32 AddCpuTimer(const TCHAR* Name);
	uint32 AddGpuTimer(const TCHAR* Name);
	TSharedRef<TimelineInternal> EditCpuThreadTimeline(uint32 ThreadId);
	TSharedRef<TimelineInternal> EditGpuTimeline();
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const override;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const override;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual uint64 GetTimelineCount() const override { return Timelines.Num(); }
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const override;
	virtual void ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const override;

private:
	FTimingProfilerTimer& AddTimerInternal(const TCHAR* Name, bool IsGpuEvent);

	FSlabAllocator& Allocator;
	FAnalysisSessionLock& SessionLock;
	TArray<FTimingProfilerTimer> Timers;
	TArray<TSharedRef<TimelineInternal>> Timelines;
	TMap<uint32, uint32> CpuThreadTimelineIndexMap;
	uint32 GpuTimelineIndex = 0;
};

}