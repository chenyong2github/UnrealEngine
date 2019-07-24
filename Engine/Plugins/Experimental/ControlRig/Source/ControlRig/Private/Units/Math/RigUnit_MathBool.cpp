// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathBool.h"
#include "Units/RigUnitContext.h"

void FRigUnit_MathBoolNot::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = !Value;
}

void FRigUnit_MathBoolAnd::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A && B;
}

void FRigUnit_MathBoolNand::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = (!A) && (!B);
}

void FRigUnit_MathBoolOr::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A || B;
}

void FRigUnit_MathBoolEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

void FRigUnit_MathBoolNotEquals::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

