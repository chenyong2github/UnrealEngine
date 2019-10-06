// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Utility/DatasmithMathUtils.h"

#include "Math/UnrealMathUtility.h"
#include "Math/TransformVectorized.h"
#include "Math/Quat.h"


void FDatasmithTransformUtils::GetRotation(const FTransform& InTransform, FQuat& OutRotation)
{
	OutRotation = InTransform.GetRotation();

	// Workaround to floating point precision issues with quaternion
	{
		// this value was found from ONE experience, so I'm waiting on other problem to improve it
		if (FMath::Abs(OutRotation.X) < 0.005f)
		{
			OutRotation.X = 0.f;
		}
		if (FMath::Abs(OutRotation.Y) < 0.005f)
		{
			OutRotation.Y = 0.f;
		}
		if (FMath::Abs(OutRotation.Z) < 0.005f)
		{
			OutRotation.Z = 0.f;
		}
		if ((1 - FMath::Abs(OutRotation.W)) < 0.001f)
		{
			OutRotation.W = (OutRotation.W > 0) ? 1.f : -1.f;
		}
		OutRotation.Normalize();
	}
}