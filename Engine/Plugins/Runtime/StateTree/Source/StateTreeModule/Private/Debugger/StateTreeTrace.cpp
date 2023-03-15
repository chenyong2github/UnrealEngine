// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTrace.h"
#include "Debugger/StateTreeDebugger.h"
#include "StateTree.h"
#include "UObject/Package.h"
#include "Trace/Trace.inl"

UE_TRACE_CHANNEL_DEFINE(StateTreeDebugChannel)

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, InstanceEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, TreeName)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, TreePath)
	UE_TRACE_EVENT_FIELD(uint32, CompiledDataHash)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, InstanceName)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, LogEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Message)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, StateEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, StateIdx)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, TaskEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, TaskIdx)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, ActiveStatesEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16[], ActiveStates)
UE_TRACE_EVENT_END()

void UE::StateTreeTrace::OutputInstanceLifetimeEvent(
	const FStateTreeInstanceDebugId InstanceId,
	const UStateTree* StateTree,
	const TCHAR* InstanceName,
	const EStateTreeTraceInstanceEventType EventType)
{
	if (StateTree == nullptr)
	{
		return;
	}
	const FString ObjectName = StateTree->GetName();
	const FString ObjectPackageName = StateTree->GetPackage()->GetName();

	UE_TRACE_LOG(StateTreeDebugger, InstanceEvent, StateTreeDebugChannel)
		<< InstanceEvent.Cycle(FPlatformTime::Cycles64())
		<< InstanceEvent.TreeName(*ObjectName, ObjectName.Len())
		<< InstanceEvent.TreePath(*ObjectPackageName, ObjectPackageName.Len())
		<< InstanceEvent.CompiledDataHash(StateTree->LastCompiledEditorDataHash)
		<< InstanceEvent.InstanceId(InstanceId.Id)
		<< InstanceEvent.InstanceSerial(InstanceId.SerialNumber)
		<< InstanceEvent.InstanceName(InstanceName)
		<< InstanceEvent.EventType(static_cast<uint8>(EventType));
}

void UE::StateTreeTrace::OutputLogEventTrace(const FStateTreeInstanceDebugId InstanceId, const TCHAR* Fmt, ...)
{
	static TCHAR TraceStaticBuffer[8192];
	GET_VARARGS(TraceStaticBuffer, UE_ARRAY_COUNT(TraceStaticBuffer), UE_ARRAY_COUNT(TraceStaticBuffer) - 1, Fmt, Fmt);

	UE_TRACE_LOG(StateTreeDebugger, LogEvent, StateTreeDebugChannel)
		<< LogEvent.Cycle(FPlatformTime::Cycles64())
		<< LogEvent.InstanceId(InstanceId.Id)
		<< LogEvent.InstanceSerial(InstanceId.SerialNumber)
		<< LogEvent.Message(TraceStaticBuffer);
}

void UE::StateTreeTrace::OutputStateEventTrace(const FStateTreeInstanceDebugId InstanceId, const uint16 StateIdx, const EStateTreeTraceNodeEventType EventType)
{
	UE_TRACE_LOG(StateTreeDebugger, StateEvent, StateTreeDebugChannel)
		<< StateEvent.Cycle(FPlatformTime::Cycles64())
		<< StateEvent.InstanceId(InstanceId.Id)
		<< StateEvent.InstanceSerial(InstanceId.SerialNumber)
		<< StateEvent.StateIdx(StateIdx)
		<< StateEvent.EventType(static_cast<uint8>(EventType));
}

void UE::StateTreeTrace::OutputTaskEventTrace(const FStateTreeInstanceDebugId InstanceId, const uint16 TaskIdx, const EStateTreeTraceNodeEventType EventType)
{
	UE_TRACE_LOG(StateTreeDebugger, TaskEvent, StateTreeDebugChannel)
		<< TaskEvent.Cycle(FPlatformTime::Cycles64())
		<< TaskEvent.InstanceId(InstanceId.Id)
		<< TaskEvent.InstanceSerial(InstanceId.SerialNumber)
		<< TaskEvent.TaskIdx(TaskIdx)
		<< TaskEvent.EventType(static_cast<uint8>(EventType));
}

void UE::StateTreeTrace::OutputActiveStatesEventTrace(const FStateTreeInstanceDebugId InstanceId, const FStateTreeActiveStates& ActiveStates)
{
	TArray<uint16, TInlineAllocator<FStateTreeActiveStates::MaxStates>> StatesIndices;
	for (int32 i = 0; i < ActiveStates.Num(); i++)
	{
		StatesIndices.Add(ActiveStates[i].Index);
	}

	UE_TRACE_LOG(StateTreeDebugger, ActiveStatesEvent, StateTreeDebugChannel)
		<< ActiveStatesEvent.Cycle(FPlatformTime::Cycles64())
		<< ActiveStatesEvent.InstanceId(InstanceId.Id)
		<< ActiveStatesEvent.InstanceSerial(InstanceId.SerialNumber)
		<< ActiveStatesEvent.ActiveStates(StatesIndices.GetData(), StatesIndices.Num());
}

#endif // WITH_STATETREE_DEBUGGER
