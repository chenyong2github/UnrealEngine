// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ProfilingDebugging/CookStats.h"

#define OUTPUT_COOKTIMING ENABLE_COOK_STATS

#if OUTPUT_COOKTIMING
#include "Trace/Trace.inl"
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

#define UE_CREATE_HIERARCHICAL_COOKTIMER(name, incrementScope)	FScopeTimer ScopeTimer##name(__COUNTER__, #name, incrementScope); 

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.
#define UE_SCOPED_COOKTIMER(name)						TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(name, CookChannel)
#define UE_SCOPED_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); UE_SCOPED_COOKTIMER(name)

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.  Also creates a named hierarchical timer that can be aggregated and reported at cook completion.
#define UE_SCOPED_HIERARCHICAL_COOKTIMER(name)			UE_CREATE_HIERARCHICAL_COOKTIMER(name, true); ScopeTimer##name.Start(); UE_SCOPED_COOKTIMER(name)
#define UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); UE_SCOPED_HIERARCHICAL_COOKTIMER(name)

#define UE_CUSTOM_COOKTIMER_LOG Cpu

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.
#define UE_SCOPED_CUSTOM_COOKTIMER(name)				UE_TRACE_LOG_SCOPED_T(UE_CUSTOM_COOKTIMER_LOG, name, CookChannel)
#define UE_SCOPED_CUSTOM_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); UE_SCOPED_CUSTOM_COOKTIMER(name)

// Emits trace events denoting scope/lifetime of an activity on the cooking channel.  Also creates a named hierarchical timer that can be aggregated and reported at cook completion.
#define UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER(name)	UE_CREATE_HIERARCHICAL_COOKTIMER(name, true); ScopeTimer##name.Start(); UE_SCOPED_CUSTOM_COOKTIMER(name)
#define UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER_AND_DURATION(name, durationStorage) \
			FScopedDurationTimer name##Timer(durationStorage); UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER(name)
#define UE_ADD_CUSTOM_COOKTIMER_META(name, key, value) << name.key(value)

#else
#define UE_SCOPED_COOKTIMER(name)
#define UE_SCOPED_COOKTIMER_AND_DURATION(name, durationStorage)
#define UE_SCOPED_HIERARCHICAL_COOKTIMER(name)
#define UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(name, durationStorage)

#define UE_CUSTOM_COOKTIMER_LOG Cpu
#define UE_SCOPED_CUSTOM_COOKTIMER(name)
#define UE_SCOPED_CUSTOM_COOKTIMER_AND_DURATION(name, durationStorage)
#define UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER(name)
#define UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER_AND_DURATION(name, durationStorage)
#define UE_ADD_CUSTOM_COOKTIMER_META(key, value)

void OutputHierarchyTimers() {}
void ClearHierarchyTimers() {}
#endif