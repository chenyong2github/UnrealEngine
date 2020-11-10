// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSplineUnits.h"
#include "Units/RigUnitContext.h"
#include "Features/IModularFeatures.h"

#include "tinysplinecxx.h"

FRigUnit_ControlRigSplineFromPoints_Execute()
{
	switch (Context.State)
	{
		case EControlRigState::Init:
		case EControlRigState::Update:
		{
			const int32 ControlPointsCount = Points.Num();
		
			bool bModeChanged = Spline.SplineMode != SplineMode;
			Spline.SplineMode = SplineMode;

			Spline.SetControlPoints(Points, bModeChanged);
			break;
		}
		default:
		{
			checkNoEntry(); // Execute is only defined for Init and Update
			break;
		}
	}
}

FRigUnit_PositionFromControlRigSpline_Execute()
{
	switch (Context.State)
	{
		case EControlRigState::Init:
		case EControlRigState::Update:
		{
			Position = Spline.PositionAtParam(U);
			break;
		}
		default:
		{
			checkNoEntry(); // Execute is only defined for Init and Update
			break;
		}
	}
}

FRigUnit_DrawControlRigSpline_Execute()
{
	if (Context.State == EControlRigState::Init)
	{
		return;
	}

	if (Context.DrawInterface == nullptr)
	{
		return;
	}

	int32 Count = FMath::Clamp<int32>(Detail, 4, 64);
	FControlRigDrawInstruction Instruction(EControlRigDrawSettings::LineStrip, Color, Thickness);
	Instruction.Positions.SetNumUninitialized(Count);

	float T = 0;
	float Step = 1.f / float(Count-1);
	for(int32 Index=0; Index<Count; ++Index)
	{
		// Evaluate at T
		Instruction.Positions[Index] = Spline.PositionAtParam(T);
		T += Step;
	}

	Context.DrawInterface->Instructions.Add(Instruction);
}
