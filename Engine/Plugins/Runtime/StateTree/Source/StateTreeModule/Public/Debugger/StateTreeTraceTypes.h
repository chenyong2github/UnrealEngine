// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/TVariant.h"
#include "StateTreeIndexTypes.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeTraceTypes.generated.h"

class UStateTree;
struct FStateTreeStateHandle;
enum class EStateTreeStateSelectionBehavior : uint8;
enum class EStateTreeRunStatus : uint8;

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
	OnEnter				UMETA(DisplayName = "Enter"),		// for State + Task events
	OnExit				UMETA(DisplayName = "Exit"),		// for State + Task events
	PushStateSelection	UMETA(DisplayName = "Push"),		// for State events
	PopStateSelection	UMETA(DisplayName = "Pop"),			// for State events
	OnStateSelected		UMETA(DisplayName = "Selected"),	// for State events
	OnStateCompleted	UMETA(DisplayName = "Completed"),	// for State events
	OnTickingTask		UMETA(DisplayName = "Tick"),		// for Task events
	OnTaskCompleted		UMETA(DisplayName = "Completed"),	// for Task events
	OnTaskTicked		UMETA(DisplayName = "Ticked"),		// for Task events
	Passed				UMETA(DisplayName = "Passed"),		// for Condition events
	Failed				UMETA(DisplayName = "Failed"),		// for Condition events
	OnEvaluating		UMETA(DisplayName = "Evaluating")	// for Transition events
};

#if WITH_STATETREE_DEBUGGER

struct FStateTreeTracePhaseEvent
{
	explicit FStateTreeTracePhaseEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase)
		: RecordingWorldTime(RecordingWorldTime)
		, Phase(Phase)
	{
	}

	static FString GetDataTypePath() { return TEXT(""); }
	static FString GetDataAsText() { return TEXT(""); }

	double RecordingWorldTime = 0;
	EStateTreeUpdatePhase Phase;
};

struct FStateTreeTraceLogEvent : FStateTreeTracePhaseEvent
{
	explicit FStateTreeTraceLogEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FString& Message)
		: FStateTreeTracePhaseEvent(RecordingWorldTime, Phase)
		, Message(Message)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FString Message;
};

struct FStateTreeTraceTransitionEvent : FStateTreeTracePhaseEvent
{
	explicit FStateTreeTraceTransitionEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FStateTreeIndex16 Index, const EStateTreeTraceNodeEventType EventType)
		: FStateTreeTracePhaseEvent(RecordingWorldTime, Phase)
		, TransitionIndex(Index)
		, EventType(EventType)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FStateTreeIndex16 TransitionIndex;
	EStateTreeTraceNodeEventType EventType = EStateTreeTraceNodeEventType::Unset;
};

struct FStateTreeTraceNodeEvent : FStateTreeTracePhaseEvent
{
	explicit FStateTreeTraceNodeEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FStateTreeIndex16 Index, const EStateTreeTraceNodeEventType EventType)
		: FStateTreeTracePhaseEvent(RecordingWorldTime, Phase)
		, Index(Index)
		, EventType(EventType)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;
	
	FStateTreeIndex16 Index;
	EStateTreeTraceNodeEventType EventType = EStateTreeTraceNodeEventType::Unset;
};

struct FStateTreeTraceStateEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceStateEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FStateTreeIndex16 Index, const EStateTreeTraceNodeEventType EventType, const EStateTreeStateSelectionBehavior SelectionBehavior)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Phase, Index, EventType)
		, SelectionBehavior(SelectionBehavior)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;
	STATETREEMODULE_API FStateTreeStateHandle GetStateHandle() const;

	EStateTreeStateSelectionBehavior SelectionBehavior;
};

struct FStateTreeTraceTaskEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceTaskEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FStateTreeIndex16 Index,	const EStateTreeTraceNodeEventType EventType, const EStateTreeRunStatus Status,	const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Phase, Index, EventType)
		, TypePath(TypePath)
		, InstanceDataAsText(InstanceDataAsText)
		, Status(Status)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FString GetDataTypePath() const { return TypePath; }
	FString GetDataAsText() const { return InstanceDataAsText; }

	FString TypePath;
	FString InstanceDataAsText;
	EStateTreeRunStatus Status;
};

struct FStateTreeTraceConditionEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceConditionEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const FStateTreeIndex16 Index, const EStateTreeTraceNodeEventType EventType, const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Phase, Index, EventType)
		, TypePath(TypePath)
		, InstanceDataAsText(InstanceDataAsText)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FString GetDataTypePath() const { return TypePath; }
	FString GetDataAsText() const { return InstanceDataAsText; }

	FString TypePath;
	FString InstanceDataAsText;
};

struct FStateTreeTraceActiveStatesEvent : FStateTreeTracePhaseEvent
{
	// Intentionally implemented in source file to compile 'TArray<FStateTreeStateHandle>' using only forward declaration.
	explicit FStateTreeTraceActiveStatesEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase);

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	TArray<FStateTreeStateHandle> ActiveStates;
};

/** Type aliases for statetree trace events */
using FStateTreeTraceEventVariantType = TVariant<FStateTreeTraceLogEvent,
												FStateTreeTraceNodeEvent,
												FStateTreeTraceStateEvent,
												FStateTreeTraceTaskEvent,
												FStateTreeTraceTransitionEvent,
												FStateTreeTraceConditionEvent,
												FStateTreeTraceActiveStatesEvent>;

#endif // WITH_STATETREE_DEBUGGER
