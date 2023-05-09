// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "Trace/Trace.h"

class UStateTree;
struct FStateTreeDataView;
struct FStateTreeActiveStates;
struct FStateTreeInstanceDebugId;
enum class EStateTreeRunStatus : uint8;
enum class EStateTreeTraceNodeEventType : uint8;
enum class EStateTreeUpdatePhase : uint16;
enum class EStateTreeTraceInstanceEventType : uint8;

UE_TRACE_CHANNEL_EXTERN(StateTreeDebugChannel, STATETREEMODULE_API)

namespace UE::StateTreeTrace
{
	void OutputInstanceLifetimeEvent(const FStateTreeInstanceDebugId InstanceId, const UStateTree* StateTree, const TCHAR* InstanceName, const EStateTreeTraceInstanceEventType EventType);
	void OutputLogEventTrace(const FStateTreeInstanceDebugId InstanceId, const EStateTreeUpdatePhase Phase, const TCHAR* Fmt, ...);
	void OutputStateEventTrace(const FStateTreeInstanceDebugId InstanceId, const EStateTreeUpdatePhase Phase, const uint16 StateIdx, const EStateTreeTraceNodeEventType EventType);
	void OutputTaskEventTrace(const FStateTreeInstanceDebugId InstanceId, const EStateTreeUpdatePhase Phase, const uint16 TaskIdx, FStateTreeDataView DataView, const EStateTreeTraceNodeEventType EventType, const EStateTreeRunStatus Status);
	void OutputConditionEventTrace(const FStateTreeInstanceDebugId InstanceId, const EStateTreeUpdatePhase Phase, const uint16 ConditionIdx, FStateTreeDataView DataView, const EStateTreeTraceNodeEventType EventType);
	void OutputActiveStatesEventTrace(const FStateTreeInstanceDebugId InstanceId, const FStateTreeActiveStates& ActiveStates);
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

#define TRACE_STATETREE_STATE_EVENT(InstanceId, Phase, StateIdx, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputStateEventTrace(InstanceId, Phase, StateIdx, EventType); \
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

#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, ActivateStates) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputActiveStatesEventTrace(InstanceId, ActivateStates); \
	}

#else //STATETREE_DEBUG_TRACE_ENABLED

#define TRACE_STATETREE_INSTANCE_EVENT(InstanceID, StateTree, InstanceName, EventType)
#define TRACE_STATETREE_LOG_EVENT(InstanceId, Phase, Format, ...)
#define TRACE_STATETREE_STATE_EVENT(InstanceId, Phase, StateIdx, EventType)
#define TRACE_STATETREE_TASK_EVENT(InstanceId, Phase, TaskIdx, DataView, EventType, Status)
#define TRACE_STATETREE_CONDITION_EVENT(InstanceId, Phase, ConditionIdx, DataView, EventType)
#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, ActivateStates)

#endif // STATETREE_DEBUG_TRACE_ENABLED
