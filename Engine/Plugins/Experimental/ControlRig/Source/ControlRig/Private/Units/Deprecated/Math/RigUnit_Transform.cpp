// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Transform.h"

FRigUnit_MultiplyTransform_Execute()
{
	Result = Argument0*Argument1;
}

FRigUnit_GetRelativeTransform_Execute()
{
	Result = Argument0.GetRelativeTransform(Argument1);
}

