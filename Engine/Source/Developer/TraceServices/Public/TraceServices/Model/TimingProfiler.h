// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/Containers/Tables.h"

namespace Trace
{

struct FTimingProfilerTimer
{
	const TCHAR* Name;
	uint32 Id;
	uint32 NameHash;
	bool IsGpuTimer;
};

struct FTimingProfilerEvent
{
	uint32 TimerIndex;
};

struct FTimingProfilerAggregatedStats
	: public FAggregatedTimingStats
{
	const FTimingProfilerTimer* Timer = nullptr;
};

class ITimingProfilerProvider
	: public IProvider
{
public:
	typedef ITimeline<FTimingProfilerEvent> Timeline;

	virtual ~ITimingProfilerProvider() = default;
	virtual bool GetCpuThreadTimelineIndex(uint32 ThreadId, uint32& OutTimelineIndex) const = 0;
	virtual bool GetGpuTimelineIndex(uint32& OutTimelineIndex) const = 0;
	virtual bool ReadTimeline(uint32 Index, TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual uint64 GetTimelineCount() const = 0;
	virtual void EnumerateTimelines(TFunctionRef<void(const Timeline&)> Callback) const = 0;
	virtual void ReadTimers(TFunctionRef<void(const FTimingProfilerTimer*, uint64)> Callback) const = 0;
	virtual ITable<FTimingProfilerAggregatedStats>* CreateAggregation(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const = 0;
};

TRACESERVICES_API const ITimingProfilerProvider* ReadTimingProfilerProvider(const IAnalysisSession& Session);

}
