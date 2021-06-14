// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Float.h"

FRigUnit_Multiply_FloatFloat_Execute()
{
	Result = FRigMathLibrary::Multiply(Argument0, Argument1);
}

FRigUnit_Add_FloatFloat_Execute()
{
	Result = FRigMathLibrary::Add(Argument0, Argument1);
}

FRigUnit_Subtract_FloatFloat_Execute()
{
	Result = FRigMathLibrary::Subtract(Argument0, Argument1);
}

FRigUnit_Divide_FloatFloat_Execute()
{
	Result = FRigMathLibrary::Divide(Argument0, Argument1);
}

FRigUnit_Clamp_Float_Execute()
{
	Result = FMath::Clamp(Value, Min, Max);
}

FRigUnit_MapRange_Float_Execute()
{
	Result = FMath::GetMappedRangeValueClamped(FVector2D(MinIn, MaxIn), FVector2D(MinOut, MaxOut), Value);
}
