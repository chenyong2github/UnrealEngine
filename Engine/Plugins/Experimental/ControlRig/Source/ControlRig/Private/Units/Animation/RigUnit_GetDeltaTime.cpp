// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Animation/RigUnit_GetDeltaTime.h"
#include "Units/RigUnitContext.h"

FRigUnit_GetDeltaTime_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Context.DeltaTime;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_GetDeltaTime)
{
	Context.DeltaTime = 0.2f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 0.2f), TEXT("unexpected delta time"));
	return true;
}
#endif