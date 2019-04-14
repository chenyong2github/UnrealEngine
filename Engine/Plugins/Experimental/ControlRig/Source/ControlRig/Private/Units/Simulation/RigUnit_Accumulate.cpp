// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Accumulate.h"
#include "Units/RigUnitContext.h"

void FRigUnit_AccumulateFloatAdd::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else if(bIntegrateDeltaTime)
	{
		Result = AccumulatedValue = AccumulatedValue + Increment * Context.DeltaTime;
	}
	else
	{
		Result = AccumulatedValue = AccumulatedValue + Increment;
	}
}

void FRigUnit_AccumulateVectorAdd::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else if (bIntegrateDeltaTime)
	{
		Result = AccumulatedValue = AccumulatedValue + Increment * Context.DeltaTime;
	}
	else
	{
		Result = AccumulatedValue = AccumulatedValue + Increment;
	}
}

void FRigUnit_AccumulateFloatMul::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		float Factor = Multiplier;
		if (bIntegrateDeltaTime)
		{
			Factor = FMath::Lerp<float>(1.f, Factor, Context.DeltaTime);
		}
		Result = AccumulatedValue = AccumulatedValue * Factor;
	}
}

void FRigUnit_AccumulateVectorMul::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		FVector Factor = Multiplier;
		if (bIntegrateDeltaTime)
		{
			Factor = FMath::Lerp<FVector>(FVector::OneVector, Factor, Context.DeltaTime);
		}
		Result = AccumulatedValue = AccumulatedValue * Factor;
	}
}

void FRigUnit_AccumulateQuatMul::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		FQuat Factor = Multiplier;
		if (bIntegrateDeltaTime)
		{
			Factor = FQuat::Slerp(FQuat::Identity, Factor, Context.DeltaTime);
		}
		if (bFlipOrder)
		{
			Result = AccumulatedValue = Factor * AccumulatedValue;
		}
		else
		{
			Result = AccumulatedValue = AccumulatedValue * Factor;
		}
	}
}

void FRigUnit_AccumulateTransformMul::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		FTransform Factor = Multiplier;
		if (bIntegrateDeltaTime)
		{
			Factor.SetTranslation(FMath::Lerp<FVector>(FVector::OneVector, Factor.GetTranslation(), Context.DeltaTime));
			Factor.SetRotation(FQuat::Slerp(FQuat::Identity, Factor.GetRotation(), Context.DeltaTime));
			Factor.SetScale3D(FMath::Lerp<FVector>(FVector::OneVector, Factor.GetScale3D(), Context.DeltaTime));
		}
		if (bFlipOrder)
		{
			Result = AccumulatedValue = Factor * AccumulatedValue;
		}
		else
		{
			Result = AccumulatedValue = AccumulatedValue * Factor;
		}
	}
}

void FRigUnit_AccumulateFloatLerp::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		Result = AccumulatedValue = FMath::Lerp<float>(AccumulatedValue, TargetValue, FMath::Clamp<float>(bIntegrateDeltaTime ? Context.DeltaTime * Blend : Blend, 0.f, 1.f));
	}
}

void FRigUnit_AccumulateVectorLerp::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		Result = AccumulatedValue = FMath::Lerp<FVector>(AccumulatedValue, TargetValue, FMath::Clamp<float>(bIntegrateDeltaTime ? Context.DeltaTime * Blend : Blend, 0.f, 1.f));
	}
}

void FRigUnit_AccumulateQuatLerp::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		Result = AccumulatedValue = FQuat::Slerp(AccumulatedValue, TargetValue, FMath::Clamp<float>(bIntegrateDeltaTime ? Context.DeltaTime * Blend : Blend, 0.f, 1.f));
	}
}

void FRigUnit_AccumulateTransformLerp::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Result = AccumulatedValue = InitialValue;
	}
	else
	{
		float B = FMath::Clamp<float>(bIntegrateDeltaTime ? Context.DeltaTime * Blend : Blend, 0.f, 1.f);
		AccumulatedValue.SetTranslation(FMath::Lerp<FVector>(AccumulatedValue.GetTranslation(), TargetValue.GetTranslation(), B));
		AccumulatedValue.SetRotation(FQuat::Slerp(AccumulatedValue.GetRotation(), TargetValue.GetRotation(), B));
		AccumulatedValue.SetScale3D(FMath::Lerp<FVector>(AccumulatedValue.GetScale3D(), TargetValue.GetScale3D(), B));
		Result = AccumulatedValue;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateFloatAdd)
{
	Context.DeltaTime = 0.5f;

	Unit.Increment = 1.f;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 2.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 3.f), TEXT("unexpected accumulate result"));
	Unit.bIntegrateDeltaTime = true;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 3.5f), TEXT("unexpected accumulate result"));
	return true;
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateVectorAdd)
{
	Context.DeltaTime = 0.5f;

	Unit.Increment = FVector(1.0f, 0.f, 0.f);
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 1.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 1.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 2.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 3.f), TEXT("unexpected accumulate result"));
	Unit.bIntegrateDeltaTime = true;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 3.5f), TEXT("unexpected accumulate result"));
	return true;
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateFloatMul)
{
	Unit.Multiplier = 2.f;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 2.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 2.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 4.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 8.f), TEXT("unexpected accumulate result"));
	return true;
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateVectorMul)
{
	Unit.Multiplier = FVector(2.f, 2.f, 2.f);
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 2.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 2.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 4.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 8.f), TEXT("unexpected accumulate result"));
	return true;
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateFloatLerp)
{
	Unit.InitialValue = 0.f;
	Unit.TargetValue = 8.f;
	Unit.Blend = 0.5f;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 4.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 4.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 6.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 7.f), TEXT("unexpected accumulate result"));
	return true;
}

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_AccumulateVectorLerp)
{
	Unit.InitialValue = FVector::ZeroVector;
	Unit.TargetValue = FVector(8.f, 0.f, 0.f);
	Unit.Blend = 0.5f;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 4.f), TEXT("unexpected accumulate result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 4.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 6.f), TEXT("unexpected accumulate result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result.X, 7.f), TEXT("unexpected accumulate result"));
	return true;
}

#endif