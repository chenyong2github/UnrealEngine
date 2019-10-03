// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_AlphaInterp.h"
#include "Units/RigUnitContext.h"

FRigUnit_AlphaInterp_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		ScaleBiasClamp.Reinitialize();
		ScaleBiasClamp.bMapRange = bMapRange;
		ScaleBiasClamp.bClampResult = bClampResult;
		ScaleBiasClamp.bInterpResult = bInterpResult;
		ScaleBiasClamp.InRange = InRange;
		ScaleBiasClamp.OutRange = OutRange;
		ScaleBiasClamp.ClampMin = ClampMin;
		ScaleBiasClamp.ClampMax = ClampMax;
	}
	else
	{
		ScaleBiasClamp.Scale = Scale;
		ScaleBiasClamp.Bias = Bias;
		ScaleBiasClamp.InterpSpeedIncreasing = InterpSpeedIncreasing;
		ScaleBiasClamp.InterpSpeedDecreasing = InterpSpeedDecreasing;

		Result = ScaleBiasClamp.ApplyTo(Value, Context.DeltaTime);
	}
}
