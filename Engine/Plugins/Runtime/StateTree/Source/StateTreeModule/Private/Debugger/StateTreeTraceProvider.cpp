// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTraceProvider.h"
#include "StateTreeTypes.h"
#include "Debugger/StateTreeDebugger.h"

FName FStateTreeTraceProvider::ProviderName("StateTreeDebuggerProvider");

#define LOCTEXT_NAMESPACE "StateTreeDebuggerProvider"

FStateTreeTraceProvider::FStateTreeTraceProvider(TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
{
}

bool FStateTreeTraceProvider::ReadTimelines(const FStateTreeInstanceDebugId InstanceId, const TFunctionRef<void(const FStateTreeInstanceDebugId ProcessedInstanceId, const FEventsTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	// Read specific timeline if specified
	if (InstanceId.IsValid())
	{
		const uint32* IndexPtr = InstanceIdToDebuggerEntryTimelines.Find(InstanceId);
		if (IndexPtr != nullptr && EventsTimelines.IsValidIndex(*IndexPtr))
		{
			Callback(InstanceId, *EventsTimelines[*IndexPtr]);
			return true;
		}	
	}
	else
	{
		for(auto It = InstanceIdToDebuggerEntryTimelines.CreateConstIterator(); It; ++It)
		{
			if (EventsTimelines.IsValidIndex(It.Value()))
			{
				Callback(It.Key(), *EventsTimelines[It.Value()]);
			}
		}

		return EventsTimelines.Num() > 0;	
	}

	return false;
}

bool FStateTreeTraceProvider::ReadTimelines(const UStateTree& StateTree, TFunctionRef<void(const FStateTreeInstanceDebugId ProcessedInstanceId, const FEventsTimeline&)> Callback) const
{
	Session.ReadAccessCheck();

	for (auto It = InstanceIdToDebuggerEntryTimelines.CreateConstIterator(); It; ++It)
	{
		check(EventsTimelines.IsValidIndex(It.Value()));
		check(Descriptors.Num() == EventsTimelines.Num());

		if (Descriptors[It.Value()].StateTree == &StateTree)
		{
			Callback(Descriptors[It.Value()].Id, *EventsTimelines[It.Value()]);
		}
	}

	return EventsTimelines.Num() > 0;
}

void FStateTreeTraceProvider::AppendEvent(const FStateTreeInstanceDebugId InInstanceId, const double InTime, const FStateTreeTraceEventVariantType& InEvent)
{
	Session.WriteAccessCheck();

	const uint32* IndexPtr = InstanceIdToDebuggerEntryTimelines.Find(InInstanceId);
	if (ensureMsgf(IndexPtr != nullptr, TEXT("Timeline can't be found: probably caused by a missing instance event 'Started'.")))
	{
		EventsTimelines[*IndexPtr]->AppendEvent(InTime, InEvent);
	}
	
	Session.UpdateDurationSeconds(InTime);
}

void FStateTreeTraceProvider::AppendInstanceEvent(
	const UStateTree* InStateTree,
	const FStateTreeInstanceDebugId InInstanceId,
	const TCHAR* InInstanceName,
	const double InTime,
	const EStateTreeTraceInstanceEventType InEventType)
{
	if (InEventType == EStateTreeTraceInstanceEventType::Started)
	{
		Descriptors.Emplace(InStateTree, InInstanceId, InInstanceName, TRange<double>(InTime, std::numeric_limits<double>::max()));

		check(InstanceIdToDebuggerEntryTimelines.Find(InInstanceId) == nullptr);
		InstanceIdToDebuggerEntryTimelines.Add(InInstanceId, EventsTimelines.Num());
		
		EventsTimelines.Emplace(MakeShared<TraceServices::TPointTimeline<FStateTreeTraceEventVariantType>>(Session.GetLinearAllocator()));
	}
	else if (InEventType == EStateTreeTraceInstanceEventType::Stopped)
	{
		const uint32* Index = InstanceIdToDebuggerEntryTimelines.Find(InInstanceId);
		if (ensureMsgf(Index != nullptr, TEXT("Unable to find matching instance. Looks like we never received event 'Started' for this instance.")))
		{
			check(Descriptors.IsValidIndex(*Index));
			Descriptors[*Index].Lifetime.SetUpperBound(InTime);
		}
	}

	Session.UpdateDurationSeconds(InTime);
}

void FStateTreeTraceProvider::GetInstances(TArray<UE::StateTreeDebugger::FInstanceDescriptor>& OutInstances) const
{
	OutInstances = Descriptors;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER
