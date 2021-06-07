// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_SpringInterp.h"
#include "Units/RigUnitContext.h"


namespace RigUnitSpringInterpConstants
{
	static const float Mass = 1.0f;
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
		Result = UKismetMathLibrary::FloatSpringInterp(Current, Target, SpringState, Stiffness, CriticalDamping,
			Context.DeltaTime, RigUnitSpringInterpConstants::Mass, TargetVelocityAmount, false, 0.0f, 0.0f, true);
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
		Result = UKismetMathLibrary::VectorSpringInterp(Current, Target, SpringState, Stiffness, CriticalDamping,
			Context.DeltaTime, RigUnitSpringInterpConstants::Mass, TargetVelocityAmount, false, FVector(), FVector(), true);
	}
}
