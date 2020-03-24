// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ProfilingDebugging/CookStats.h"

#define OUTPUT_COOKTIMING ENABLE_COOK_STATS

#if OUTPUT_COOKTIMING
#include "Trace/Config.h"
#include "Trace/Trace.h"
#include "ProfilingDebugging/FormatArgsTrace.h"
#include "ProfilingDebugging/ScopedTimers.h"

void OutputHierarchyTimers();
void ClearHierarchyTimers();

struct FHierarchicalTimerInfo;

struct FScopeTimer
{
public:
	FScopeTimer(const FScopeTimer&) = delete;
	FScopeTimer(FScopeTimer&&) = delete;

	FScopeTimer(int InId, const char* InName, bool IncrementScope = false );

	void Start();
	void Stop();

	~FScopeTimer();

private:
	uint64					StartTime = 0;
	FHierarchicalTimerInfo* HierarchyTimerInfo;
	FHierarchicalTimerInfo* PrevTimerInfo;
};

UE_TRACE_CHANNEL_EXTERN(CookChannel)

#define CREATE_HIERARCHICAL_COOKTIMER(name, incrementScope)	FScopeTimer ScopeTimer##name(__COUNTER__, #name, incrementScope); 

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.
#define SCOPED_COOKTIMER(name)						TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(name, CookChannel)
#define SCOPED_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); SCOPED_COOKTIMER(name)

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.  Also creates a named hierarchical timer that can be aggregated and reported at cook completion.
#define SCOPED_HIERARCHICAL_COOKTIMER(name)			CREATE_HIERARCHICAL_COOKTIMER(name, true); ScopeTimer##name.Start(); SCOPED_COOKTIMER(name)
#define SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); SCOPED_HIERARCHICAL_COOKTIMER(name)
#else
#define SCOPED_COOKTIMER(name)
#define SCOPED_COOKTIMER_AND_DURATION(name, durationStorage)
#define SCOPED_HIERARCHICAL_COOKTIMER(name)
#define SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(name, durationStorage)

void OutputHierarchyTimers() {}
void ClearHierarchyTimers() {}
#endif