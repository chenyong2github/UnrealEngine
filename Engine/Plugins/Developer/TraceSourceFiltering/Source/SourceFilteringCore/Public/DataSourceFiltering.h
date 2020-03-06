// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/EnumRange.h"
#include "UObject/ObjectMacros.h"

/** Enum representing the possible Filter Set operations */
UENUM()
enum class EFilterSetMode : uint8
{
	AND,
	OR,
	NOT,
	Count UMETA(Hidden)
};

ENUM_RANGE_BY_COUNT(EFilterSetMode, EFilterSetMode::Count);

/** Enum representing all possible Source Filter operations, used by FSourceFilterTrace::OutputFilterOperation */
enum class ESourceActorFilterOperation
{
	RemoveFilter,
	MoveFilter,
	ReplaceFilter,
	SetFilterMode,
	SetFilterState
};

/** Enum representing all possible World Filter operations, used by FSourceFilterTrace::OutputWorldOperation */
enum class EWorldFilterOperation
{
	TypeFilter,
	NetModeFilter,
	InstanceFilter,
	RemoveWorld
};
