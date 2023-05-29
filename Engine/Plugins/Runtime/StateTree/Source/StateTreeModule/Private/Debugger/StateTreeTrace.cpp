// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "Debugger/StateTreeTrace.h"
#include "Debugger/StateTreeDebugger.h"
#include "Exporters/Exporter.h"
#include "ObjectTrace.h"
#include "Serialization/BufferArchive.h"
#include "StateTree.h"
#include "StateTreeExecutionTypes.h"
#include "UObject/Package.h"
#include "Trace/Trace.inl"

#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

UE_TRACE_CHANNEL_DEFINE(StateTreeDebugChannel)

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, WorldTimestampEvent)
	UE_TRACE_EVENT_FIELD(double, WorldTime)
UE_TRACE_EVENT_END()

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

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, PhaseEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, Phase)
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
	UE_TRACE_EVENT_FIELD(uint16, StateIndex)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
	UE_TRACE_EVENT_FIELD(uint8, SelectionBehavior)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, TaskEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, NodeIndex)
	UE_TRACE_EVENT_FIELD(uint8[], DataView)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
	UE_TRACE_EVENT_FIELD(uint8, Status)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, TransitionEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, TransitionIndex)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, ConditionEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16, NodeIndex)
	UE_TRACE_EVENT_FIELD(uint8[], DataView)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(StateTreeDebugger, ActiveStatesEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, InstanceId)
	UE_TRACE_EVENT_FIELD(uint32, InstanceSerial)
	UE_TRACE_EVENT_FIELD(uint16[], ActiveStates)
UE_TRACE_EVENT_END()

namespace UE::StateTreeTrace
{

FDelegateHandle GOnWorldTickStartDelegateHandle;

#if WITH_EDITOR
FDelegateHandle GOnPIEStartDelegateHandle;
#endif // WITH_EDITOR

double GRecordingWorldTime = -1;
double GTracedRecordingWorldTime = -1;

/** Struct to keep track if a given phase was traced or not. */
struct FPhaseTraceStatusPair
{
	explicit FPhaseTraceStatusPair(const EStateTreeUpdatePhase Phase) : Phase(Phase) {}

	EStateTreeUpdatePhase Phase = EStateTreeUpdatePhase::Unset;
	bool bTraced = false;
};

/** Struct to keep track of the list of stacked phases for a given statetree instance. */
struct FPhaseStack
{
	FStateTreeInstanceDebugId InstanceId;
	TArray<FPhaseTraceStatusPair> Stack;
};

/**
 * Stacks to keep track of all received phase events so other events will control when and if a given phase trace will be sent.
 * This is per thread since it is possible to update execution contexts on multiple threads.
 */
thread_local TArray<FPhaseStack> GPhaseStacks;

/**
 * Called by TraceBufferedEvents from the OutputXYZ methods to flush pending phase events.
 * Phases popped before TraceStackedPhases gets called will never produce any trace since
 * they will not be required for the analysis.
 */
void TraceStackedPhases(const FStateTreeInstanceDebugId InstanceId)
{
	for (FPhaseStack& PhaseStack : GPhaseStacks)
	{
		if (PhaseStack.InstanceId == InstanceId)
		{
			for (FPhaseTraceStatusPair& StackEntry : PhaseStack.Stack)
			{
				if (StackEntry.bTraced == false)
				{
					UE_TRACE_LOG(StateTreeDebugger, PhaseEvent, StateTreeDebugChannel)
						<< PhaseEvent.Cycle(FPlatformTime::Cycles64())
						<< PhaseEvent.InstanceId(InstanceId.Id)
						<< PhaseEvent.InstanceSerial(InstanceId.SerialNumber)
						<< PhaseEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(StackEntry.Phase))
						<< PhaseEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EStateTreeTraceEventType::Push));
			
					StackEntry.bTraced = true;
				}
			}
			break;
		}
	}
}

/**
 * Pushed or pops an entry on the Phase stack for a given Instance.
 * Will send the Pop events for phases popped if their associated Push events were sent.
 */
void ProcessPhaseScopeEvent(const FStateTreeInstanceDebugId InstanceId, const EStateTreeUpdatePhase Phase, const EStateTreeTraceEventType EventType)
{
	int32 ExistingStackIndex = GPhaseStacks.IndexOfByPredicate([InstanceId](const FPhaseStack& PhaseStack){ return PhaseStack.InstanceId == InstanceId; });

	if (EventType == EStateTreeTraceEventType::Push)
	{
		if (ExistingStackIndex == INDEX_NONE)
		{
			ExistingStackIndex = GPhaseStacks.AddDefaulted();
		}
		FPhaseStack& PhaseStack = GPhaseStacks[ExistingStackIndex];
		PhaseStack.InstanceId = InstanceId;
		PhaseStack.Stack.Push(FPhaseTraceStatusPair(Phase));
	}
	else if (ensureMsgf(ExistingStackIndex != INDEX_NONE, TEXT("Not expected to pop phases for an instance that never pushed a phase.")))
	{
		FPhaseStack& PhaseStack = GPhaseStacks[ExistingStackIndex];

		if (ensureMsgf(PhaseStack.Stack.IsEmpty() == false, TEXT("Not expected to pop phases that never got pushed.")) &&
			ensureMsgf(PhaseStack.InstanceId == InstanceId, TEXT("Not expected to pop phases for an instance that is not the one currently assigned to the stack.")))
		{
			const FPhaseTraceStatusPair RemovedPair = PhaseStack.Stack.Pop();
			ensureMsgf(RemovedPair.Phase == Phase, TEXT("Not expected to pop a phase that is not on the top of the stack."));

			// Clear associated InstanceId when removing last entry from the stack.
			if (PhaseStack.Stack.IsEmpty())
			{
				GPhaseStacks.RemoveAt(ExistingStackIndex);
			}

			// Phase was previously traced (i.e. other events were traced in that scope so we need to trace the closing (i.e. Pop) event.
			if (RemovedPair.bTraced)
			{
				UE_TRACE_LOG(StateTreeDebugger, PhaseEvent, StateTreeDebugChannel)
				<< PhaseEvent.Cycle(FPlatformTime::Cycles64())
				<< PhaseEvent.InstanceId(InstanceId.Id)
				<< PhaseEvent.InstanceSerial(InstanceId.SerialNumber)
				<< PhaseEvent.Phase(static_cast<std::underlying_type_t<EStateTreeUpdatePhase>>(Phase))
				<< PhaseEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EStateTreeTraceEventType::Pop));
			}
		}
	}
}

/**
 * Called by TraceBufferedEvents from the OutputXYZ methods to make sure we have the current world time was sent.
 */
void TraceTimeMapping()
{
	if (GTracedRecordingWorldTime != GRecordingWorldTime)
	{
		GTracedRecordingWorldTime = GRecordingWorldTime;
		UE_TRACE_LOG(StateTreeDebugger, WorldTimestampEvent, StateTreeDebugChannel)
			<< WorldTimestampEvent.WorldTime(GRecordingWorldTime);
	}
}

/**
 * Called by the OutputXYZ methods to flush pending events (e.g. Push or WorldTime).
 */
void TraceBufferedEvents(const FStateTreeInstanceDebugId InstanceId)
{
	TraceTimeMapping();
	TraceStackedPhases(InstanceId);
}

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

void RegisterGlobalDelegates()
{
#if WITH_EDITOR
	GOnPIEStartDelegateHandle = FEditorDelegates::BeginPIE.AddLambda([&LastRecordingWorldTime=GTracedRecordingWorldTime](const bool bIsSimulating)
		{
			LastRecordingWorldTime = -1;
		});
#endif // WITH_EDITOR
	
	GOnWorldTickStartDelegateHandle = FWorldDelegates::OnWorldTickStart.AddLambda([&WorldTime=GRecordingWorldTime](const UWorld* TickedWorld, ELevelTick TickType, float DeltaTime)
		{
#if OBJECT_TRACE_ENABLED
			WorldTime = FObjectTrace::GetWorldElapsedTime(TickedWorld);
#endif// OBJECT_TRACE_ENABLED
		});
}

void UnregisterGlobalDelegates()
{
#if WITH_EDITOR
	FEditorDelegates::BeginPIE.Remove(GOnPIEStartDelegateHandle);
#endif // WITH_EDITOR
	
	FWorldDelegates::OnWorldTickStart.Remove(GOnWorldTickStartDelegateHandle);
	GOnWorldTickStartDelegateHandle.Reset();
}

void OutputInstanceLifetimeEvent(
	const FStateTreeInstanceDebugId InstanceId,
	const UStateTree* StateTree,
	const TCHAR* InstanceName,
	const EStateTreeTraceEventType EventType
	)
{
	if (StateTree == nullptr)
	{
		return;
	}
	const FString ObjectName = StateTree->GetName();
	const FString ObjectPackageName = StateTree->GetPackage()->GetName();

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, InstanceEvent, StateTreeDebugChannel)
		<< InstanceEvent.Cycle(FPlatformTime::Cycles64())
		<< InstanceEvent.TreeName(*ObjectName, ObjectName.Len())
		<< InstanceEvent.TreePath(*ObjectPackageName, ObjectPackageName.Len())
		<< InstanceEvent.CompiledDataHash(StateTree->LastCompiledEditorDataHash)
		<< InstanceEvent.InstanceId(InstanceId.Id)
		<< InstanceEvent.InstanceSerial(InstanceId.SerialNumber)
		<< InstanceEvent.InstanceName(InstanceName)
		<< InstanceEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EventType));
}

void OutputLogEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const TCHAR* Fmt, ...
	)
{
	static TCHAR TraceStaticBuffer[8192];
	GET_VARARGS(TraceStaticBuffer, UE_ARRAY_COUNT(TraceStaticBuffer), UE_ARRAY_COUNT(TraceStaticBuffer) - 1, Fmt, Fmt);

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, LogEvent, StateTreeDebugChannel)
		<< LogEvent.Cycle(FPlatformTime::Cycles64())
		<< LogEvent.InstanceId(InstanceId.Id)
		<< LogEvent.InstanceSerial(InstanceId.SerialNumber)
		<< LogEvent.Message(TraceStaticBuffer);
}

void OutputStateEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const FStateTreeStateHandle StateHandle,
	const EStateTreeTraceEventType EventType,
	const EStateTreeStateSelectionBehavior SelectionBehavior
	)
{
	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, StateEvent, StateTreeDebugChannel)
		<< StateEvent.Cycle(FPlatformTime::Cycles64())
		<< StateEvent.InstanceId(InstanceId.Id)
		<< StateEvent.InstanceSerial(InstanceId.SerialNumber)
		<< StateEvent.StateIndex(StateHandle.Index)
		<< StateEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EventType))
		<< StateEvent.SelectionBehavior(static_cast<std::underlying_type_t<EStateTreeStateSelectionBehavior>>(SelectionBehavior));
}

void OutputTaskEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const FStateTreeIndex16 TaskIdx,
	const FStateTreeDataView DataView,
	const EStateTreeTraceEventType EventType,
	const EStateTreeRunStatus Status
	)
{
	FBufferArchive Archive;
	SerializeDataViewToArchive(Archive, DataView);

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, TaskEvent, StateTreeDebugChannel)
		<< TaskEvent.Cycle(FPlatformTime::Cycles64())
		<< TaskEvent.InstanceId(InstanceId.Id)
		<< TaskEvent.InstanceSerial(InstanceId.SerialNumber)
		<< TaskEvent.NodeIndex(TaskIdx.Get())
		<< TaskEvent.DataView(Archive.GetData(), Archive.Num())
		<< TaskEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EventType))
		<< TaskEvent.Status(static_cast<std::underlying_type_t<EStateTreeRunStatus>>(Status));
}

void OutputTransitionEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const FStateTreeIndex16 TransitionIdx,
	const EStateTreeTraceEventType EventType
	)
{
	FBufferArchive Archive;
	Archive << EventType;

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, TransitionEvent, StateTreeDebugChannel)
	<< TransitionEvent.Cycle(FPlatformTime::Cycles64())
	<< TransitionEvent.InstanceId(InstanceId.Id)
	<< TransitionEvent.InstanceSerial(InstanceId.SerialNumber)
	<< TransitionEvent.TransitionIndex(TransitionIdx.Get())
	<< TransitionEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EventType));
}

void OutputConditionEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const FStateTreeIndex16 ConditionIdx,
	const FStateTreeDataView DataView,
	const EStateTreeTraceEventType EventType
	)
{
	FBufferArchive Archive;
	SerializeDataViewToArchive(Archive, DataView);

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, ConditionEvent, StateTreeDebugChannel)
	<< ConditionEvent.Cycle(FPlatformTime::Cycles64())
	<< ConditionEvent.InstanceId(InstanceId.Id)
	<< ConditionEvent.InstanceSerial(InstanceId.SerialNumber)
	<< ConditionEvent.NodeIndex(ConditionIdx.Get())
	<< ConditionEvent.DataView(Archive.GetData(), Archive.Num())
	<< ConditionEvent.EventType(static_cast<std::underlying_type_t<EStateTreeTraceEventType>>(EventType));
}

void OutputActiveStatesEventTrace(
	const FStateTreeInstanceDebugId InstanceId,
	const FStateTreeActiveStates& ActiveStates
	)
{
	TArray<uint16, TInlineAllocator<FStateTreeActiveStates::MaxStates>> StatesIndices;
	for (int32 i = 0; i < ActiveStates.Num(); i++)
	{
		StatesIndices.Add(ActiveStates[i].Index);
	}

	TraceBufferedEvents(InstanceId);

	UE_TRACE_LOG(StateTreeDebugger, ActiveStatesEvent, StateTreeDebugChannel)
		<< ActiveStatesEvent.Cycle(FPlatformTime::Cycles64())
		<< ActiveStatesEvent.InstanceId(InstanceId.Id)
		<< ActiveStatesEvent.InstanceSerial(InstanceId.SerialNumber)
		<< ActiveStatesEvent.ActiveStates(StatesIndices.GetData(), StatesIndices.Num());
}

} // UE::StateTreeTrace

#endif // WITH_STATETREE_DEBUGGER
