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
enum class EStateTreeTraceEventType : uint8
{
	Unset,
	OnEnter				UMETA(DisplayName = "Enter"),		// for State + Task events
	OnExit				UMETA(DisplayName = "Exit"),		// for State + Task events
	Push				UMETA(DisplayName = "Push"),		// for State + Phase + Instance events
	Pop					UMETA(DisplayName = "Pop"),			// for State + Phase + Instance events
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

struct FStateTreeTraceBaseEvent
{
	explicit FStateTreeTraceBaseEvent(const double RecordingWorldTime)
		: RecordingWorldTime(RecordingWorldTime)
	{
	}

	static FString GetDataTypePath() { return TEXT(""); }
	static FString GetDataAsText() { return TEXT(""); }

	double RecordingWorldTime = 0;
};

struct FStateTreeTracePhaseEvent : FStateTreeTraceBaseEvent
{
	explicit FStateTreeTracePhaseEvent(const double RecordingWorldTime, const EStateTreeUpdatePhase Phase, const EStateTreeTraceEventType EventType)
		: FStateTreeTraceBaseEvent(RecordingWorldTime)
		, Phase(Phase)
		, EventType(EventType)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	EStateTreeUpdatePhase Phase;
	EStateTreeTraceEventType EventType;
};

struct FStateTreeTraceLogEvent : FStateTreeTraceBaseEvent
{
	explicit FStateTreeTraceLogEvent(const double RecordingWorldTime, const FString& Message)
		: FStateTreeTraceBaseEvent(RecordingWorldTime)
		, Message(Message)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FString Message;
};

struct FStateTreeTraceTransitionEvent : FStateTreeTraceBaseEvent
{
	explicit FStateTreeTraceTransitionEvent(const double RecordingWorldTime, const FStateTreeIndex16 Index, const EStateTreeTraceEventType EventType)
		: FStateTreeTraceBaseEvent(RecordingWorldTime)
		, TransitionIndex(Index)
		, EventType(EventType)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	FStateTreeIndex16 TransitionIndex;
	EStateTreeTraceEventType EventType = EStateTreeTraceEventType::Unset;
};

struct FStateTreeTraceNodeEvent : FStateTreeTraceBaseEvent
{
	explicit FStateTreeTraceNodeEvent(const double RecordingWorldTime, const FStateTreeIndex16 Index, const EStateTreeTraceEventType EventType)
		: FStateTreeTraceBaseEvent(RecordingWorldTime)
		, Index(Index)
		, EventType(EventType)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;
	
	FStateTreeIndex16 Index;
	EStateTreeTraceEventType EventType = EStateTreeTraceEventType::Unset;
};

struct FStateTreeTraceStateEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceStateEvent(const double RecordingWorldTime, const FStateTreeIndex16 Index, const EStateTreeTraceEventType EventType, const EStateTreeStateSelectionBehavior SelectionBehavior)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Index, EventType)
		, SelectionBehavior(SelectionBehavior)
	{
	}

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;
	STATETREEMODULE_API FStateTreeStateHandle GetStateHandle() const;

	EStateTreeStateSelectionBehavior SelectionBehavior;
};

struct FStateTreeTraceTaskEvent : FStateTreeTraceNodeEvent
{
	explicit FStateTreeTraceTaskEvent(const double RecordingWorldTime, const FStateTreeIndex16 Index,	const EStateTreeTraceEventType EventType, const EStateTreeRunStatus Status,	const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Index, EventType)
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
	explicit FStateTreeTraceConditionEvent(const double RecordingWorldTime, const FStateTreeIndex16 Index, const EStateTreeTraceEventType EventType, const FString& TypePath, const FString& InstanceDataAsText)
		: FStateTreeTraceNodeEvent(RecordingWorldTime, Index, EventType)
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

struct FStateTreeTraceActiveStatesEvent : FStateTreeTraceBaseEvent
{
	// Intentionally implemented in source file to compile 'TArray<FStateTreeStateHandle>' using only forward declaration.
	explicit FStateTreeTraceActiveStatesEvent(const double RecordingWorldTime);

	STATETREEMODULE_API FString ToString(const UStateTree& StateTree) const;

	TArray<FStateTreeStateHandle> ActiveStates;
};

/** Type aliases for statetree trace events */
using FStateTreeTraceEventVariantType = TVariant<FStateTreeTracePhaseEvent,
												FStateTreeTraceLogEvent,
												FStateTreeTraceNodeEvent,
												FStateTreeTraceStateEvent,
												FStateTreeTraceTaskEvent,
												FStateTreeTraceTransitionEvent,
												FStateTreeTraceConditionEvent,
												FStateTreeTraceActiveStatesEvent>;

#endif // WITH_STATETREE_DEBUGGER
