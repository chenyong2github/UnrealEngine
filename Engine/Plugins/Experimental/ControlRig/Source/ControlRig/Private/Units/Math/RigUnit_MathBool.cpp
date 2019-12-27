// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathBool.h"
#include "Units/RigUnitContext.h"

FRigUnit_MathBoolNot_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = !Value;
}

FRigUnit_MathBoolAnd_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A && B;
}

FRigUnit_MathBoolNand_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = (!A) && (!B);
}

FRigUnit_MathBoolOr_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A || B;
}

FRigUnit_MathBoolEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

FRigUnit_MathBoolNotEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

