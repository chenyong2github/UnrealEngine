// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "Trace/Trace.h"

class UStateTree;
struct FStateTreeDataView;
struct FStateTreeActiveStates;
struct FStateTreeInstanceDebugId;
struct FStateTreeIndex16;
struct FStateTreeStateHandle;
enum class EStateTreeStateSelectionBehavior : uint8;
enum class EStateTreeRunStatus : uint8;
enum class EStateTreeTraceNodeEventType : uint8;
enum class EStateTreeUpdatePhase : uint16;
enum class EStateTreeTraceInstanceEventType : uint8;

UE_TRACE_CHANNEL_EXTERN(StateTreeDebugChannel, STATETREEMODULE_API)

namespace UE::StateTreeTrace
{
	void RegisterGlobalDelegates();
	void UnregisterGlobalDelegates();
	void OutputInstanceLifetimeEvent(FStateTreeInstanceDebugId InstanceId, const UStateTree* StateTree, const TCHAR* InstanceName, EStateTreeTraceInstanceEventType EventType);
	void OutputLogEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, const TCHAR* Fmt, ...);
	void OutputStateEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, FStateTreeStateHandle StateHandle, EStateTreeTraceNodeEventType EventType, EStateTreeStateSelectionBehavior SelectionBehavior);
	void OutputTaskEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, FStateTreeIndex16 TaskIdx, FStateTreeDataView DataView, EStateTreeTraceNodeEventType EventType, EStateTreeRunStatus Status);
	void OutputConditionEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, FStateTreeIndex16 ConditionIdx, FStateTreeDataView DataView, EStateTreeTraceNodeEventType EventType);
	void OutputTransitionEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, FStateTreeIndex16 TransitionIdx, EStateTreeTraceNodeEventType EventType);
	void OutputActiveStatesEventTrace(FStateTreeInstanceDebugId InstanceId, EStateTreeUpdatePhase Phase, const FStateTreeActiveStates& ActiveStates);
}

#define TRACE_STATETREE_INSTANCE_EVENT(InstanceID, StateTree, InstanceName, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputInstanceLifetimeEvent(InstanceID, StateTree, InstanceName, EventType); \
	}

#define TRACE_STATETREE_LOG_EVENT(InstanceId, Phase, Format, ...) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputLogEventTrace(InstanceId, Phase, Format, ##__VA_ARGS__); \
	}

#define TRACE_STATETREE_STATE_EVENT(InstanceId, Phase, StateHandle, EventType, SelectionBehavior) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputStateEventTrace(InstanceId, Phase, StateHandle, EventType, SelectionBehavior); \
	}

#define TRACE_STATETREE_TASK_EVENT(InstanceId, Phase, TaskIdx, DataView, EventType, Status) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputTaskEventTrace(InstanceId, Phase, TaskIdx, DataView, EventType, Status); \
	}

#define TRACE_STATETREE_CONDITION_EVENT(InstanceId, Phase, ConditionIdx, DataView, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputConditionEventTrace(InstanceId, Phase, ConditionIdx, DataView, EventType); \
	}

#define TRACE_STATETREE_TRANSITION_EVENT(InstanceId, Phase, TransitionIdx, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputTransitionEventTrace(InstanceId, Phase, TransitionIdx, EventType); \
	}

#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, Phase, ActivateStates) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputActiveStatesEventTrace(InstanceId, Phase, ActivateStates); \
	}

#else //STATETREE_DEBUG_TRACE_ENABLED

#define TRACE_STATETREE_INSTANCE_EVENT(InstanceID, StateTree, InstanceName, EventType)
#define TRACE_STATETREE_LOG_EVENT(InstanceId, Phase, Format, ...)
#define TRACE_STATETREE_STATE_EVENT(InstanceId, Phase, StateHandle, EventType, SelectionBehavior)
#define TRACE_STATETREE_TASK_EVENT(InstanceId, Phase, TaskIdx, DataView, EventType, Status)
#define TRACE_STATETREE_CONDITION_EVENT(InstanceId, Phase, ConditionIdx, DataView, EventType)
#define TRACE_STATETREE_TRANSITION_EVENT(InstanceId, Phase, TransitionIdx, EventType)
#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, Phase, ActivateStates)

#endif // STATETREE_DEBUG_TRACE_ENABLED
