// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathBool.h"
#include "Units/RigUnitContext.h"

UE_RigUnit_MathBoolNot_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = !Value;
}

UE_RigUnit_MathBoolAnd_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A && B;
}

UE_RigUnit_MathBoolNand_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = (!A) && (!B);
}

UE_RigUnit_MathBoolOr_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A || B;
}

UE_RigUnit_MathBoolEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

UE_RigUnit_MathBoolNotEquals_IMPLEMENT_STATIC_VIRTUAL_METHOD(void, Execute, const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

