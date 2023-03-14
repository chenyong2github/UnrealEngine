// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "StateTreeTraceTypes.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Timelines.h"

struct FStateTreeDebuggerInstanceDesc;
struct FStateTreeInstanceDebugId;

class IStateTreeTraceProvider : public TraceServices::IProvider
{
public:
	typedef TraceServices::ITimeline<FStateTreeTraceEventVariantType> FEventsTimeline;

	virtual void GetActivateInstances(TArray<FStateTreeDebuggerInstanceDesc>& OutInstances) const = 0;

	/**
	 * Execute given function receiving an event timeline for a given instance or all timelines if instance not specified.  
	 * @param InstanceId Id of a specific instance to get the timeline for; could be an FStateTreeInstanceDebugId::Invalid to go through all timelines
	 * @param Callback Function called for timeline(s) matching the provided Id 
	 * @return True if the specified instance was found for a given Id or at least one timeline was found when no specific Id is provided. 
	 */
	virtual bool ReadTimelines(const FStateTreeInstanceDebugId InstanceId, TFunctionRef<void(const FStateTreeInstanceDebugId ProcessedInstanceId, const FEventsTimeline&)> Callback) const = 0;
};

#endif // WITH_STATETREE_DEBUGGER
