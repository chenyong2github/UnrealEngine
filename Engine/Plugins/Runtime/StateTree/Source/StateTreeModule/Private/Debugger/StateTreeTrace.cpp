// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTrace.h"
#include "Debugger/StateTreeDebugger.h"
#include "Exporters/Exporter.h"
#include "Serialization/BufferArchive.h"
#include "StateTree.h"
#include "StateTreeExecutionTypes.h"
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
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Message)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, StateEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(uint16, StateIndex)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
	UE_TRACE_EVENT_FIELD(uint8, SelectionBehavior)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, TaskEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(uint16, NodeIndex)
	UE_TRACE_EVENT_FIELD(uint8[], DataView)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
	UE_TRACE_EVENT_FIELD(uint8, Status)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, TransitionEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(uint16, TransitionIndex)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, ConditionEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(uint16, NodeIndex)
	UE_TRACE_EVENT_FIELD(uint8[], DataView)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, ActiveStatesEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
	UE_TRACE_EVENT_FIELD(uint16[], ActiveStates)
UE_TRACE_EVENT_END()

namespace UE::StateTreeTrace
{
	void SerializeDataViewToArchive(FBufferArchive Ar, const FStateTreeDataView DataView)
	{
		if (const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(DataView.GetStruct()))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UE::StateTree::ExportStructAsText)
			FString StructPath = ScriptStruct->GetPathName();
			FString TextValue;

			ScriptStruct->ExportText(TextValue, DataView.GetMemory(), DataView.GetMemory(), /*OwnerObject*/nullptr, PPF_None, /*ExportRootScope*/nullptr);

			Ar << StructPath;		
			Ar << TextValue;
		}
		else if (const UClass* Class = Cast<const UClass>(DataView.GetStruct()))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UE::StateTree::ExportObjectAsText)
			FString StructPath = Class->GetPathName();
			FStringOutputDevice OutputDevice;
			UObject* Object = DataView.GetMutablePtr<UObject>();

			// Not using on scope FExportObjectInnerContext since it is very costly to build.
			// Passing a null context will make the export use an already built thread local context.
			UExporter::ExportToOutputDevice(nullptr, Object, /*Exporter*/nullptr, OutputDevice, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, Object->GetOuter());

			Ar << StructPath;
			Ar << OutputDevice;
		}
	}
}

void UE::StateTreeTrace::OutputInstanceLifetimeEvent(
	const FStateTreeInstanceDebugId InstanceId,
	const UStateTree* StateTree,
	const TCHAR* InstanceName,
	const EStateTreeTraceInstanceEventType EventType
	)
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
		<< InstanceEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceNodeEventType>>(EventType));
}

void UE::StateTreeTrace::OutputLogEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const TCHAR* Fmt, ...
	)
{
	static TCHAR TraceStaticBuffer[8192];
	GET_VARARGS(TraceStaticBuffer, UE_ARRAY_COUNT(TraceStaticBuffer), UE_ARRAY_COUNT(TraceStaticBuffer) - 1, Fmt, Fmt);

	UE_TRACE_LOG(StateTreeDebugger, LogEvent, StateTreeDebugChannel)
		<< LogEvent.Cycle(FPlatformTime::Cycles64())
		<< LogEvent.InstanceId(InstanceId.Id)
		<< LogEvent.InstanceSerial(InstanceId.SerialNumber)
		<< LogEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
		<< LogEvent.Message(TraceStaticBuffer);
}

void UE::StateTreeTrace::OutputStateEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const FStateTreeStateHandle StateHandle,
	const EStateTreeTraceNodeEventType EventType,
	const EStateTreeStateSelectionBehavior SelectionBehavior
	)
{
	UE_TRACE_LOG(StateTreeDebugger, StateEvent, StateTreeDebugChannel)
		<< StateEvent.Cycle(FPlatformTime::Cycles64())
		<< StateEvent.InstanceId(InstanceId.Id)
		<< StateEvent.InstanceSerial(InstanceId.SerialNumber)
		<< StateEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
		<< StateEvent.StateIndex(StateHandle.Index)
		<< StateEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceNodeEventType>>(EventType))
		<< StateEvent.SelectionBehavior(static_cast<std::underlying_type_t<EStateTreeStateSelectionBehavior>>(SelectionBehavior));
}

void UE::StateTreeTrace::OutputTaskEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const FStateTreeIndex16 TaskIdx,
	const FStateTreeDataView DataView,
	const EStateTreeTraceNodeEventType EventType,
	const EStateTreeRunStatus Status
	)
{
	FBufferArchive Archive;
	SerializeDataViewToArchive(Archive, DataView);

	UE_TRACE_LOG(StateTreeDebugger, TaskEvent, StateTreeDebugChannel)
		<< TaskEvent.Cycle(FPlatformTime::Cycles64())
		<< TaskEvent.InstanceId(InstanceId.Id)
		<< TaskEvent.InstanceSerial(InstanceId.SerialNumber)
		<< TaskEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
		<< TaskEvent.NodeIndex(TaskIdx.Get())
		<< TaskEvent.DataView(Archive.GetData(), Archive.Num())
		<< TaskEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceNodeEventType>>(EventType))
		<< TaskEvent.Status(static_cast<std::underlying_type_t<EStateTreeRunStatus>>(Status));
}

void UE::StateTreeTrace::OutputTransitionEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const FStateTreeIndex16 TransitionIdx,
	const EStateTreeTraceNodeEventType EventType
	)
{
	FBufferArchive Archive;
	Archive << EventType;

	UE_TRACE_LOG(StateTreeDebugger, TransitionEvent, StateTreeDebugChannel)
	<< TransitionEvent.Cycle(FPlatformTime::Cycles64())
	<< TransitionEvent.InstanceId(InstanceId.Id)
	<< TransitionEvent.InstanceSerial(InstanceId.SerialNumber)
	<< TransitionEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
	<< TransitionEvent.TransitionIndex(TransitionIdx.Get())
	<< TransitionEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceNodeEventType>>(EventType));
}

void UE::StateTreeTrace::OutputConditionEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const FStateTreeIndex16 ConditionIdx,
	const FStateTreeDataView DataView,
	const EStateTreeTraceNodeEventType EventType
	)
{
	FBufferArchive Archive;
	SerializeDataViewToArchive(Archive, DataView);
	
	UE_TRACE_LOG(StateTreeDebugger, ConditionEvent, StateTreeDebugChannel)
	<< ConditionEvent.Cycle(FPlatformTime::Cycles64())
	<< ConditionEvent.InstanceId(InstanceId.Id)
	<< ConditionEvent.InstanceSerial(InstanceId.SerialNumber)
	<< ConditionEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
	<< ConditionEvent.NodeIndex(ConditionIdx.Get())
	<< ConditionEvent.DataView(Archive.GetData(), Archive.Num())
	<< ConditionEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceNodeEventType>>(EventType));
}

void UE::StateTreeTrace::OutputActiveStatesEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const EStateTreeUpdatePhase Phase,
	const FStateTreeActiveStates& ActiveStates
	)
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
		<< ActiveStatesEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
		<< ActiveStatesEvent.ActiveStates(StatesIndices.GetData(), StatesIndices.Num());
}

#endif // WITH_STATETREE_DEBUGGER
