// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionTypes.generated.h"

/**
 * Enumeration for the different update phases.
 * This is used as context information when tracing debug events.
 * The values are ordered based on their hierarchy starting with the "higher" parents.
 */
UENUM(Flags)
enum class EStateTreeUpdatePhase : uint16
{
	Unset						= 0,
	StartTree					= 1ull << 1 UMETA(DisplayName = "Start Tree"),
	StopTree					= 1ull << 2 UMETA(DisplayName = "Stop Tree"),
	StartGlobalTasks			= 1ull << 3 UMETA(DisplayName = "Start Global Tasks"),
	StopGlobalTasks				= 1ull << 4 UMETA(DisplayName = "Stop Global Tasks"),
	TickStateTree				= 1ull << 5 UMETA(DisplayName = "Tick State Tree"),
	ApplyTransitions			= 1ull << 6 UMETA(DisplayName = "Transition"),
	TriggerTransitions			= 1ull << 7 UMETA(DisplayName = "Trigger Transitions"),
	TickingGlobalTasks			= 1ull << 8 UMETA(DisplayName = "Tick Global Tasks"),
	TickingTasks				= 1ull << 9 UMETA(DisplayName = "Tick Tasks"),
	TransitionConditions		= 1ull << 10 UMETA(DisplayName = "Transition conditions"),
	StateSelection				= 1ull << 11 UMETA(DisplayName = "Try Enter"),
	EnterConditions				= 1ull << 12 UMETA(DisplayName = "Enter conditions"),
};
ENUM_CLASS_FLAGS(EStateTreeUpdatePhase);