// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTraceAnalyzer.h"
#include "Debugger/StateTreeDebugger.h"
#include "Debugger/StateTreeTraceProvider.h"
#include "Debugger/StateTreeTraceTypes.h"
#include "TraceServices/Model/AnalysisSession.h"

FStateTreeTraceAnalyzer::FStateTreeTraceAnalyzer(TraceServices::IAnalysisSession& InSession, FStateTreeTraceProvider& InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
}

void FStateTreeTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Instance, "StateTreeDebugger", "InstanceEvent");
	Builder.RouteEvent(RouteId_LogMessage, "StateTreeDebugger", "LogEvent");
	Builder.RouteEvent(RouteId_State, "StateTreeDebugger", "StateEvent");
	Builder.RouteEvent(RouteId_Task, "StateTreeDebugger", "TaskEvent");
	Builder.RouteEvent(RouteId_ActiveStates, "StateTreeDebugger", "ActiveStatesEvent");
}

bool FStateTreeTraceAnalyzer::OnEvent(const uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FStateTreeAnalyzer"));

	TraceServices::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Instance:
		{
			FString ObjectName, ObjectPathName;
			EventData.GetString("TreeName", ObjectName);
			EventData.GetString("TreePath", ObjectPathName);

			const FTopLevelAssetPath Path((FName)ObjectPathName, (FName)ObjectName);
			TWeakObjectPtr<const UStateTree> WeakStateTree;
			{
				// This might not work when using a debugger on a client but should be fine in Editor as long as
				// we are not trying to find the object during GC. We might not currently be in the game thread.  
				// @todo STDBG: eventually errors should be reported in the UI
				FGCScopeGuard Guard;
				WeakStateTree = FindObject<UStateTree>(Path);
			}
			
			if (const UStateTree* StateTree = WeakStateTree.Get())
			{
				const uint32 CompiledDataHash = EventData.GetValue<uint32>("CompiledDataHash");
				if (StateTree->LastCompiledEditorDataHash == CompiledDataHash)
				{
					const FStateTreeInstanceDebugId InstanceId(
						EventData.GetValue<uint32>("InstanceId"),
						EventData.GetValue<uint32>("InstanceSerial"));
					FString InstanceName;
					EventData.GetString("InstanceName", InstanceName);
					const EStateTreeTraceInstanceEventType EventType = EventData.GetValue<EStateTreeTraceInstanceEventType>("EventType");
					Provider.AppendInstance(StateTree, InstanceId, *InstanceName, EventType);
				}
				else
				{
					UE_LOG(LogStateTree, Warning, TEXT("Traces are not using the same StateTree asset version as the current asset."));
				}
			}
			else
			{
				UE_LOG(LogStateTree, Warning, TEXT("Unable to find StateTree asset: %s : %s"), *ObjectPathName, *ObjectName);
			}
			break;
		}
	case RouteId_LogMessage:
		{
			const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			const FStateTreeInstanceDebugId InstanceId(
				EventData.GetValue<uint32>("InstanceId"),
				EventData.GetValue<uint32>("InstanceSerial"));

			FStateTreeTraceLogEvent Event;
			EventData.GetString("Message", Event.Message);

			Provider.AppendEvent(
				InstanceId,
				Context.EventTime.AsSeconds(Cycle),
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceLogEvent>(), Event));
			break;
		}
	case RouteId_State:
		{
			const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			const FStateTreeInstanceDebugId InstanceId(
				EventData.GetValue<uint32>("InstanceId"),
				EventData.GetValue<uint32>("InstanceSerial"));

			FStateTreeTraceStateEvent Event;
			Event.StateIdx = EventData.GetValue<uint16>("StateIdx");
			Event.EventType = EventData.GetValue<EStateTreeTraceNodeEventType>("EventType");

			Provider.AppendEvent(
				InstanceId,
				Context.EventTime.AsSeconds(Cycle),
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceStateEvent>(), Event));
			break;
		}
	case RouteId_Task:
		{
			const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			const FStateTreeInstanceDebugId InstanceId(
				EventData.GetValue<uint32>("InstanceId"),
				EventData.GetValue<uint32>("InstanceSerial"));

			FStateTreeTraceTaskEvent Event;
			Event.TaskIdx = EventData.GetValue<uint16>("TaskIdx");
			Event.EventType = EventData.GetValue<EStateTreeTraceNodeEventType>("EventType");

			Provider.AppendEvent(
				InstanceId,
				Context.EventTime.AsSeconds(Cycle),
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceTaskEvent>(), Event));
			break;
		}
	case RouteId_ActiveStates:
		{
			const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			const FStateTreeInstanceDebugId InstanceId(
				EventData.GetValue<uint32>("InstanceId"),
				EventData.GetValue<uint32>("InstanceSerial"));

			FStateTreeTraceActiveStatesEvent Event;
			Event.ActiveStates = EventData.GetArrayView<uint16>("ActiveStates");

			Provider.AppendEvent(
				InstanceId,
				Context.EventTime.AsSeconds(Cycle),
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceActiveStatesEvent>(), Event));
			break;
		}
	default:
		ensureMsgf(false, TEXT("Unhandle route id: %s"), RouteId);
	}

	return true;
}

#endif // WITH_STATETREE_DEBUGGER