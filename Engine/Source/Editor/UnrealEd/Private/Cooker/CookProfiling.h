// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "ProfilingDebugging/CookStats.h"
#include "Stats/Stats.h"

#define OUTPUT_COOKTIMING ENABLE_COOK_STATS
#define PROFILE_NETWORK 0

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
#define UE_ADD_CUSTOM_COOKTIMER_META(name, key, value)

inline void OutputHierarchyTimers() {}
inline void ClearHierarchyTimers() {}
#endif

#if ENABLE_COOK_STATS
namespace DetailedCookStats
{
	extern double TickCookOnTheSideTimeSec;
	extern double TickCookOnTheSideLoadPackagesTimeSec;
	extern double TickCookOnTheSideResolveRedirectorsTimeSec;
	extern double TickCookOnTheSideSaveCookedPackageTimeSec;
	extern double TickCookOnTheSideBeginPrepareSaveTimeSec;
	extern double TickCookOnTheSideFinishPrepareSaveTimeSec;
	extern double GameCookModificationDelegateTimeSec;

	// Stats tracked through FAutoRegisterCallback
	extern uint32 NumPreloadedDependencies;
	extern uint32 NumPackagesIterativelySkipped;
	extern int32 PeakRequestQueueSize;
	extern int32 PeakLoadQueueSize;
	extern int32 PeakSaveQueueSize;
}
#endif

#if PROFILE_NETWORK
double TimeTillRequestStarted = 0.0;
double TimeTillRequestForfilled = 0.0;
double TimeTillRequestForfilledError = 0.0;
double WaitForAsyncFilesWrites = 0.0;
FEvent* NetworkRequestEvent = nullptr;
#endif

DECLARE_STATS_GROUP(TEXT("Cooking"), STATGROUP_Cooking, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Precache Derived data for platform"), STAT_TickPrecacheCooking, STATGROUP_Cooking);
DECLARE_CYCLE_STAT(TEXT("Tick cooking"), STAT_TickCooker, STATGROUP_Cooking);
