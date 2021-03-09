// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_SpringInterp.h"
#include "Units/RigUnitContext.h"

namespace RigUnitSpringInterpConstants
{
	static const float FixedTimeStep = 1.0f / 60.0f;
	static const float MaxTimeStep = 0.1f;
}

FRigUnit_SpringInterp_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		SpringState.Reset();
	}
	else
	{
		// Clamp to avoid large time deltas.
		float RemainingTime = FMath::Min(Context.DeltaTime, RigUnitSpringInterpConstants::MaxTimeStep);

		Result = Current;
		while (RemainingTime >= RigUnitSpringInterpConstants::FixedTimeStep)
		{
			Result = UKismetMathLibrary::FloatSpringInterp(Result, Target, SpringState, Stiffness, CriticalDamping, RigUnitSpringInterpConstants::FixedTimeStep, Mass);
			RemainingTime -= RigUnitSpringInterpConstants::FixedTimeStep;
		}

		Result = UKismetMathLibrary::FloatSpringInterp(Result, Target, SpringState, Stiffness, CriticalDamping, RemainingTime, Mass);
	}
}

FRigUnit_SpringInterpVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		SpringState.Reset();
	}
	else
	{
		// Clamp to avoid large time deltas.
		float RemainingTime = FMath::Min(Context.DeltaTime, RigUnitSpringInterpConstants::MaxTimeStep);

		Result = Current;
		while (RemainingTime >= RigUnitSpringInterpConstants::FixedTimeStep)
		{
			Result = UKismetMathLibrary::VectorSpringInterp(Result, Target, SpringState, Stiffness, CriticalDamping, RigUnitSpringInterpConstants::FixedTimeStep, Mass);
			RemainingTime -= RigUnitSpringInterpConstants::FixedTimeStep;
		}

		Result = UKismetMathLibrary::VectorSpringInterp(Result, Target, SpringState, Stiffness, CriticalDamping, RemainingTime, Mass);
	}
}