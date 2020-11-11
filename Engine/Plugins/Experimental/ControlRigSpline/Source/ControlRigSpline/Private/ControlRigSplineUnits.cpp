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

FRigUnit_TransformFromControlRigSpline_Execute()
{
	switch (Context.State)
	{
		case EControlRigState::Init:
		case EControlRigState::Update:
		{
			FVector UpVectorNormalized = UpVector;
			UpVectorNormalized.Normalize();

			const float ClampedU = FMath::Clamp<float>(U, 0.f, 1.f);
			const float ClampedTwist = FMath::Clamp<float>(Twist, -180.f, 180.f);
		
			FVector Tangent = Spline.TangentAtParam(ClampedU);

			// Check if Tangent can be normalized. If not, keep the same tangent as before.
			if (!Tangent.Normalize())
			{
				Tangent = Transform.ToMatrixNoScale().GetUnitAxis(EAxis::X);
			}
			FVector Binormal = FVector::CrossProduct(Tangent, UpVectorNormalized);
			Binormal = Binormal.RotateAngleAxis(ClampedTwist * ClampedU, Tangent);

			FMatrix RotationMatrix = FRotationMatrix::MakeFromXZ(Tangent, Binormal);

			Transform.SetFromMatrix(RotationMatrix);
			Transform.SetTranslation(Spline.PositionAtParam(U));
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
