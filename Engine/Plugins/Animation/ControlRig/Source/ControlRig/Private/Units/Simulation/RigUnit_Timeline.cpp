// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Timeline.h"
#include "Units/RigUnitContext.h"

FRigUnit_Timeline_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		Time = AccumulatedValue = 0.f;
		return;
	}

	Time = AccumulatedValue = AccumulatedValue + Context.DeltaTime * Speed;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_Timeline)
{
	Context.DeltaTime = 1.f;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Time, 1.f), TEXT("unexpected time"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Time, 1.f), TEXT("unexpected time"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Time, 2.f), TEXT("unexpected time"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Time, 3.f), TEXT("unexpected time"));
	Unit.Speed = 0.5f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Time, 3.5f), TEXT("unexpected time"));
	return true;
}

#endif