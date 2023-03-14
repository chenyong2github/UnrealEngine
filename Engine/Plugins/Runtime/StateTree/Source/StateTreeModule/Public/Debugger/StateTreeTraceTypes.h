// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/TVariant.h"
#include "StateTreeTraceTypes.generated.h"

struct FStateTreeStateHandle;

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
	OnEnter,
	OnExit,
	OnCompleted
};

#if WITH_STATETREE_DEBUGGER

struct FStateTreeTraceLogEvent
{
	FString Message;
};

struct FStateTreeTraceStateEvent
{
	int16 StateIdx = INDEX_NONE;
	EStateTreeTraceNodeEventType EventType = EStateTreeTraceNodeEventType::Unset;
};

struct FStateTreeTraceTaskEvent
{
	int16 TaskIdx = INDEX_NONE;
	EStateTreeTraceNodeEventType EventType = EStateTreeTraceNodeEventType::Unset;
};

struct FStateTreeTraceActiveStatesEvent
{
	TArray<FStateTreeStateHandle> ActiveStates;
};

/** Type aliases for statetree trace events */
using FStateTreeTraceEventVariantType = TVariant<FStateTreeTraceLogEvent, FStateTreeTraceStateEvent, FStateTreeTraceTaskEvent, FStateTreeTraceActiveStatesEvent>;

#endif // WITH_STATETREE_DEBUGGER
