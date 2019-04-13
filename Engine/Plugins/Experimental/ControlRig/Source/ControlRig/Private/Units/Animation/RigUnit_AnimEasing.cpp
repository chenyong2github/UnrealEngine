// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_AnimEasing.h"
#include "Units/RigUnitContext.h"

void FRigUnit_AnimEasing::Execute(const FRigUnitContext& Context)
{
	if (FMath::IsNearlyEqual(SourceMinimum, SourceMaximum))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("The source minimum and maximum are the same."));
	}

	Result = FMath::Clamp<float>((Value - SourceMinimum) / (SourceMaximum - SourceMinimum), 0.f, 1.f);

	switch(Type)
	{
		case EControlRigAnimEasingType::Linear:
		{
			break;
		}
		case EControlRigAnimEasingType::QuadraticIn:
		{
			Result = Result * Result;
			break;
		}
		case EControlRigAnimEasingType::QuadraticOut:
		{
			Result = -(Result * (Result - 2.f));
			break;
		}
		case EControlRigAnimEasingType::QuadraticInOut:
		{
			if (Result < 0.5f)
			{
				Result = 2.f * Result * Result;
			}
			else
			{
				Result = (-2.f * Result * Result) + (4.f * Result) - 1.f;
			}
			break;
		}
		case EControlRigAnimEasingType::CubicIn:
		{
			Result = Result * Result * Result;
			break;
		}
		case EControlRigAnimEasingType::CubicOut:
		{
			Result = Result - 1.f;
			Result = Result * Result * Result + 1.f;
			break;
		}
		case EControlRigAnimEasingType::CubicInOut:
		{
			if (Result < 0.5f)
			{
				Result = 4.f * Result * Result * Result;
			}
			else
			{
				Result = 2.f * Result - 2.f;
				Result = 0.5f * Result * Result * Result + 1.f;
			}
			break;
		}
		case EControlRigAnimEasingType::Sinusoidal:
		{
			Result = (FMath::Sin(Result * PI - HALF_PI) + 1.f) * 0.5f;
			break;
		}
	}

	Result = FMath::Lerp<float>(TargetMinimum, TargetMaximum, Result);
}

