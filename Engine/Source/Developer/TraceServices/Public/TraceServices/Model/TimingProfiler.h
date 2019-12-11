// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/Containers/Tables.h"
#include "Containers/Array.h"

namespace Trace
{

struct FTimingProfilerTimer
{
	const TCHAR* Name = nullptr;
	uint32 Id = 0;
	uint32 NameHash = 0;
	bool IsGpuTimer = false;
};

struct FTimingProfilerEvent
{
	uint32 TimerIndex = uint32(-1);
};

struct FTimingProfilerAggregatedStats
{
	const FTimingProfilerTimer* Timer = nullptr;
	uint64 InstanceCount = 0;
	double TotalInclusiveTime = 0.0;
	double MinInclusiveTime = DBL_MAX;
	double MaxInclusiveTime = -DBL_MAX;
	double AverageInclusiveTime = 0.0;
	double MedianInclusiveTime = 0.0;
	double TotalExclusiveTime = 0.0;
	double MinExclusiveTime = DBL_MAX;
	double MaxExclusiveTime = -DBL_MAX;
	double AverageExclusiveTime = 0.0;
	double MedianExclusiveTime = 0.0;
};

struct FTimingProfilerButterflyNode
{
	const FTimingProfilerTimer* Timer = nullptr;
	uint64 Count = 0;
	double InclusiveTime = 0.0;
	double ExclusiveTime = 0.0;
	const FTimingProfilerButterflyNode* Parent = nullptr;
	TArray<FTimingProfilerButterflyNode*> Children;
};

class ITimingProfilerButterfly
{
public:
	virtual ~ITimingProfilerButterfly() = default;
	virtual const FTimingProfilerButterflyNode& GenerateCallersTree(uint32 TimerId) = 0;
	virtual const FTimingProfilerButterflyNode& GenerateCalleesTree(uint32 TimerId) = 0;
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
	virtual ITimingProfilerButterfly* CreateButterfly(double IntervalStart, double IntervalEnd, TFunctionRef<bool(uint32)> CpuThreadFilter, bool IncludeGpu) const = 0;
};

TRACESERVICES_API const ITimingProfilerProvider* ReadTimingProfilerProvider(const IAnalysisSession& Session);

}
