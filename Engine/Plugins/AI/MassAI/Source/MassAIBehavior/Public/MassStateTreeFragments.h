// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassStateTreeSubsystem.h"
#include "MassStateTreeFragments.generated.h"


USTRUCT()
struct MASSAIBEHAVIOR_API FMassStateTreeFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassStateTreeFragment() = default;

	/** Handle to a StateTree asset in MassStateTreeSubsystem */
	FMassStateTreeHandle StateTreeHandle;

	/** Keep track of the last update time to adjust time delta */
	TOptional<float> LastUpdateTimeInSeconds;
};
