// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Vector.h"

FRigUnit_Multiply_VectorVector_Execute()
{
	Result = FRigMathLibrary::Multiply(Argument0, Argument1);
}

FRigUnit_Add_VectorVector_Execute()
{
	Result = FRigMathLibrary::Add(Argument0, Argument1);
}

FRigUnit_Subtract_VectorVector_Execute()
{
	Result = FRigMathLibrary::Subtract(Argument0, Argument1);
}

FRigUnit_Divide_VectorVector_Execute()
{
	Result = FRigMathLibrary::Divide(Argument0, Argument1);
}

FRigUnit_Distance_VectorVector_Execute()
{
	Result = (Argument0 - Argument1).Size();
}
