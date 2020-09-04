// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathInt.h"
#include "Units/RigUnitContext.h"

FRigUnit_MathIntAdd_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A + B;
}

FRigUnit_MathIntSub_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A - B;
}

FRigUnit_MathIntMul_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

FRigUnit_MathIntDiv_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(B == 0)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B is 0"));
		Result = 0;
		return;
	}
	Result = A / B;
}

FRigUnit_MathIntMod_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(B <= 0)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("B needs to be greater than 0"));
		Result = 0;
		return;
	}
	Result = A % B;
}

FRigUnit_MathIntMin_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Min<int32>(A, B);
}

FRigUnit_MathIntMax_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Max<int32>(A, B);
}

FRigUnit_MathIntPow_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Pow(A, B);
}

FRigUnit_MathIntNegate_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = -Value;
}

FRigUnit_MathIntAbs_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Abs(Value);
}

FRigUnit_MathIntToFloat_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = (float)Value;
}

FRigUnit_MathIntSign_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value < 0 ? -1 : 1;
}

FRigUnit_MathIntClamp_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FMath::Clamp<int32>(Value, Minimum, Maximum);
}

FRigUnit_MathIntEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A == B;
}

FRigUnit_MathIntNotEquals_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A != B;
}

FRigUnit_MathIntGreater_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A > B;
}

FRigUnit_MathIntLess_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A < B;
}

FRigUnit_MathIntGreaterEqual_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A >= B;
}

FRigUnit_MathIntLessEqual_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A <= B;
}
