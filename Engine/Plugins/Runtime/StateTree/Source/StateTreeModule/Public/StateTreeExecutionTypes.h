// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionTypes.generated.h"

/**
 * Enumeration for the different update phases.
 * This is used as context information when tracing debug events.
 */
UENUM()
enum class EStateTreeUpdatePhase : uint8
{
	Unset					= 0,
	StartTree				UMETA(DisplayName = "Start Tree"),
	StopTree				UMETA(DisplayName = "Stop Tree"),
	StartGlobalTasks		UMETA(DisplayName = "Start Global Tasks"),
	StopGlobalTasks			UMETA(DisplayName = "Stop Global Tasks"),
	TickStateTree			UMETA(DisplayName = "Tick State Tree"),
	ApplyTransitions		UMETA(DisplayName = "Transition"),
	TriggerTransitions		UMETA(DisplayName = "Trigger Transitions"),
	TickingGlobalTasks		UMETA(DisplayName = "Tick Global Tasks"),
	TickingTasks			UMETA(DisplayName = "Tick Tasks"),
	TransitionConditions	UMETA(DisplayName = "Transition conditions"),
	StateSelection			UMETA(DisplayName = "Try Enter"),
	EnterConditions			UMETA(DisplayName = "Enter conditions"),
};