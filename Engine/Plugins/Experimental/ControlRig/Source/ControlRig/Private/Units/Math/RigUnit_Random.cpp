// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_Random.h"
#include "Units/RigUnitContext.h"
#include "GenericPlatform/GenericPlatformMath.h"

float FRigUnit_Random_Helper(int32& Seed)
{
	Seed = (Seed * 196314165) + 907633515;
	union { float f; int32 i; } Result;
	union { float f; int32 i; } Temp;
	const float SRandTemp = 1.0f;
	Temp.f = SRandTemp;
	Result.i = (Temp.i & 0xff800000) | (Seed & 0x007fffff);
	return FPlatformMath::Fractional(Result.f);
}

void FRigUnit_RandomFloat::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		LastSeed = Seed;
		return;
	}

	Result = FRigUnit_Random_Helper(LastSeed);
	Result = FMath::Lerp<float>(Minimum, Maximum, Result);
}

void FRigUnit_RandomVector::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		LastSeed = Seed;
		return;
	}

	Result.X = FMath::Lerp<float>(Minimum, Maximum, FRigUnit_Random_Helper(LastSeed));
	Result.Y = FMath::Lerp<float>(Minimum, Maximum, FRigUnit_Random_Helper(LastSeed));
	Result.Z = FMath::Lerp<float>(Minimum, Maximum, FRigUnit_Random_Helper(LastSeed));
}