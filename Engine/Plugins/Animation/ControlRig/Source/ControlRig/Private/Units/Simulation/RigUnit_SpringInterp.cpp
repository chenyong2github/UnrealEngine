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
		// Treat the input as a frequency in Hz
		float AngularFrequency = Strength * 2.0f * PI;
		float Stiffness = AngularFrequency * AngularFrequency;
		Result = UKismetMathLibrary::FloatSpringInterp(
			bUseCurrentInput ? Current : Result, Target, SpringState, Stiffness, CriticalDamping,
			Context.DeltaTime, RigUnitSpringInterpConstants::Mass, TargetVelocityAmount, 
			false, 0.0f, 0.0f, !bUseCurrentInput || bInitializeFromTarget);
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
		// Treat the input as a frequency in Hz
		float AngularFrequency = Strength * 2.0f * PI;
		float Stiffness = AngularFrequency * AngularFrequency;
		Result = UKismetMathLibrary::VectorSpringInterp(
			bUseCurrentInput ? Current : Result, Target, SpringState, Stiffness, CriticalDamping,
			Context.DeltaTime, RigUnitSpringInterpConstants::Mass, TargetVelocityAmount, 
			false, FVector(), FVector(), !bUseCurrentInput || bInitializeFromTarget);
	}
}

FRigUnit_SpringInterpQuaternion_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		SpringState.Reset();
	}
	else
	{
		// Treat the input as a frequency in Hz
		float AngularFrequency = Strength * 2.0f * PI;
		float Stiffness = AngularFrequency * AngularFrequency;
		Result = UKismetMathLibrary::QuaternionSpringInterp(
			bUseCurrentInput ? Current : Result, Target, SpringState, Stiffness, CriticalDamping,
			Context.DeltaTime, RigUnitSpringInterpConstants::Mass, TargetVelocityAmount, 
			!bUseCurrentInput || bInitializeFromTarget);
	}
}

