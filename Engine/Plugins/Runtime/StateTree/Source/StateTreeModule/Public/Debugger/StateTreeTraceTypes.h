// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/TVariant.h"
#include "StateTreeTraceTypes.generated.h"

struct FStateTreeStateHandle;
enum class EStateTreeRunStatus : uint8;
enum class EStateTreeUpdatePhase : uint8;

UENUM()
enum class EStateTreeTraceInstanceEventType : uint8
{
	Started,
	Stopped
};

UENUM()
enum class EStateTreeTraceNodeEventType : uint8
{
	Unset,
	OnEnter,			// for State + Task events
	OnExit,				// for State + Task events
	OnStateCompleted,	// for State events
	OnTaskCompleted,	// for Task events
	Passed,				// for Condition events
	Failed				// for Condition events
};

#if WITH_STATETREE_DEBUGGER

struct FStateTreeTracePhaseEvent
{
	explicit FStateTreeTracePhaseEvent(const EStateTreeUpdatePhase Phase)
		: Phase(Phase)
	{
	}

	EStateTreeUpdatePhase Phase;
};

struct FStateTreeTraceLogEvent : FStateTreeTracePhaseEvent
{
	explicit FStateTreeTraceLogEvent(const EStateTreeUpdatePhase Phase, const FString& Message)
		: FStateTreeTracePhaseEvent(Phase)
		, Message(Message)
	{
	}

	FString Message;
};

struct FStateTreeTraceNodeEvent : FStateTreeTracePhaseEvent
{
	explicit FStateTreeTraceNodeEvent(const EStateTreeUpdatePhase Phase, const int16 Idx, const EStateTreeTraceNodeEventType EventType)
		: FStateTreeTracePhaseEvent(Phase)
		, Idx(Idx)
		, EventType(EventType)
	{
	}

	int16 Idx = INDEX_NONE;
	EStateTreeTraceNodeEventType EventType = EStateTreeTraceNodeEventType::Unset;
};

struct FStateTreeTraceStateEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceStateEvent(const EStateTreeUpdatePhase Phase, const int16 Idx, const EStateTreeTraceNodeEventType EventType)
		: FStateTreeTraceNodeEvent(Phase, Idx, EventType)
	{
	}
};

struct FStateTreeTraceTaskEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceTaskEvent(const EStateTreeUpdatePhase Phase, const int16 Idx,	const EStateTreeTraceNodeEventType EventType, const EStateTreeRunStatus Status,	const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(Phase, Idx, EventType)
		, TypePath(TypePath)
		, InstanceDataAsText(InstanceDataAsText)
		, Status(Status)
	{
	}

	FString TypePath;
	FString InstanceDataAsText;
	EStateTreeRunStatus Status;	
};

struct FStateTreeTraceConditionEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceConditionEvent(const EStateTreeUpdatePhase Phase, const int16 Idx, const EStateTreeTraceNodeEventType EventType, const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(Phase, Idx, EventType)
		, TypePath(TypePath)
		, InstanceDataAsText(InstanceDataAsText)
	{
	}

	FString TypePath;
	FString InstanceDataAsText;
};

struct FStateTreeTraceActiveStatesEvent
{
	TArray<FStateTreeStateHandle> ActiveStates;
};

/** Type aliases for statetree trace events */
using FStateTreeTraceEventVariantType = TVariant<FStateTreeTraceLogEvent, FStateTreeTraceNodeEvent, FStateTreeTraceStateEvent, FStateTreeTraceTaskEvent, FStateTreeTraceConditionEvent, FStateTreeTraceActiveStatesEvent>;

#endif // WITH_STATETREE_DEBUGGER
