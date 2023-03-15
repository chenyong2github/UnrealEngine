// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "Trace/Trace.h"

class UStateTree;
struct FStateTreeActiveStates;
struct FStateTreeInstanceDebugId;
enum class EStateTreeTraceNodeEventType : uint8;
enum class EStateTreeTraceInstanceEventType : uint8;

UE_TRACE_CHANNEL_EXTERN(StateTreeDebugChannel, STATETREEMODULE_API)

namespace UE::StateTreeTrace
{
	void OutputInstanceLifetimeEvent(const FStateTreeInstanceDebugId InstanceId, const UStateTree* StateTree, const TCHAR* InstanceName, const EStateTreeTraceInstanceEventType EventType);
	void OutputLogEventTrace(const FStateTreeInstanceDebugId InstanceId, const TCHAR* Fmt, ...);
	void OutputStateEventTrace(const FStateTreeInstanceDebugId InstanceId, const uint16 StateIdx, const EStateTreeTraceNodeEventType EventType);
	void OutputTaskEventTrace(const FStateTreeInstanceDebugId InstanceId, const uint16 TaskIdx, const EStateTreeTraceNodeEventType EventType);
	void OutputActiveStatesEventTrace(const FStateTreeInstanceDebugId InstanceId, const FStateTreeActiveStates& ActiveStates);
};

#define TRACE_STATETREE_INSTANCE_EVENT(InstanceID, StateTree, InstanceName, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputInstanceLifetimeEvent(InstanceID, StateTree, InstanceName, EventType); \
	}

#define TRACE_STATETREE_LOG_EVENT(InstanceId, Format, ...) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputLogEventTrace(InstanceId, Format, ##__VA_ARGS__); \
	}

#define TRACE_STATETREE_STATE_EVENT(InstanceId, StateIdx, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputStateEventTrace(InstanceId, StateIdx, EventType); \
	}

#define TRACE_STATETREE_TASK_EVENT(InstanceId, TaskIdx, EventType) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputTaskEventTrace(InstanceId, TaskIdx, EventType); \
	}

#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, ActivateStates) \
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(StateTreeDebugChannel)) \
	{ \
		UE::StateTreeTrace::OutputActiveStatesEventTrace(InstanceId, ActivateStates); \
	}

#else //STATETREE_DEBUG_TRACE_ENABLED

#define TRACE_STATETREE_INSTANCE_EVENT(InstanceID, StateTree, InstanceName, EventType)
#define TRACE_STATETREE_LOG_EVENT(InstanceId, Format, ...)
#define TRACE_STATETREE_STATE_EVENT(InstanceId, StateIdx, EventType)
#define TRACE_STATETREE_TASK_EVENT(InstanceId, TaskIdx, EventType)
#define TRACE_STATETREE_ACTIVE_STATES_EVENT(InstanceId, ActivateStates)

#endif // STATETREE_DEBUG_TRACE_ENABLED
