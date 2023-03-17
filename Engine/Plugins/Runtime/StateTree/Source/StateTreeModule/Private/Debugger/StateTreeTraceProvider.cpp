// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTraceProvider.h"
#include "StateTreeTypes.h"
#include "Debugger/StateTreeDebugger.h"
#include "Algo/RemoveIf.h"

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

void FStateTreeTraceProvider::AppendEvent(const FStateTreeInstanceDebugId InInstanceId, const double InTime, const FStateTreeTraceEventVariantType& Event)
{
	Session.WriteAccessCheck();

	TSharedPtr<TraceServices::TPointTimeline<FStateTreeTraceEventVariantType>> Timeline;
	const uint32* IndexPtr = InstanceIdToDebuggerEntryTimelines.Find(InInstanceId);
	if (IndexPtr != nullptr)
	{
		Timeline = EventsTimelines[*IndexPtr];
	}
	else
	{
		InstanceIdToDebuggerEntryTimelines.Add(InInstanceId, EventsTimelines.Num());
		Timeline = EventsTimelines.Emplace_GetRef(MakeShared<TraceServices::TPointTimeline<FStateTreeTraceEventVariantType>>(Session.GetLinearAllocator()));
	}

	Timeline->AppendEvent(InTime, Event);
	Session.UpdateDurationSeconds(InTime);
}

void FStateTreeTraceProvider::AppendInstance(
	const UStateTree* InStateTree,
	const FStateTreeInstanceDebugId InInstanceId,
	const TCHAR* InInstanceName,
	const EStateTreeTraceInstanceEventType EventType)
{
	if (EventType == EStateTreeTraceInstanceEventType::Started)
	{
		ActiveInstances.Emplace(InStateTree, InInstanceId, InInstanceName);	
	}
	else if (EventType == EStateTreeTraceInstanceEventType::Stopped)
	{
		ActiveInstances.SetNum(Algo::RemoveIf(ActiveInstances, [InInstanceId](const FStateTreeDebuggerInstanceDesc& Instance)
			{
				return Instance.Id == InInstanceId;
			}));
	}
}

void FStateTreeTraceProvider::GetActiveInstances(TArray<FStateTreeDebuggerInstanceDesc>& OutInstances) const
{
	OutInstances = ActiveInstances;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER
