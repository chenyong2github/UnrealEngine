// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Kalman.h"
#include "Units/RigUnitContext.h"

void FRigUnit_KalmanFloat::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(BufferSize <= 0)
	{
		if (Context.State == EControlRigState::Init)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("BufferSize is too small."));
		}

		Result = Value;
		return;
	}

	int32 MaxSize = FMath::Clamp<int32>(BufferSize, 1, 512);
	if (Context.State == EControlRigState::Init)
	{
		Buffer.Reset();
		Buffer.Reserve(MaxSize);
	}
	else
	{
		if(Buffer.Num() < MaxSize)
		{
			Buffer.Add(Value);
			LastInsertIndex = 0;
		}
		else
		{
			Buffer[LastInsertIndex++] = Value;
			if(LastInsertIndex == Buffer.Num())
			{
				LastInsertIndex = 0;
			}
		}

		Result = 0.f;
		for(const float F : Buffer)
		{
			Result += F;
		}

		Result = Result / float(Buffer.Num());
	}
}

void FRigUnit_KalmanVector::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(BufferSize <= 0)
	{
		if (Context.State == EControlRigState::Init)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("BufferSize is too small."));
		}

		Result = Value;
		return;
	}

	int32 MaxSize = FMath::Clamp<int32>(BufferSize, 1, 512);
	if (Context.State == EControlRigState::Init)
	{
		Buffer.Reset();
		Buffer.Reserve(MaxSize);
	}
	else
	{
		if(Buffer.Num() < MaxSize)
		{
			Buffer.Add(Value);
			LastInsertIndex = 0;
		}
		else
		{
			Buffer[LastInsertIndex++] = Value;
			if(LastInsertIndex == Buffer.Num())
			{
				LastInsertIndex = 0;
			}
		}

		Result = FVector::ZeroVector;
		for(const FVector& F : Buffer)
		{
			Result += F;
		}

		Result = Result / float(Buffer.Num());
	}
}

void FRigUnit_KalmanTransform::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(BufferSize <= 0)
	{
		if (Context.State == EControlRigState::Init)
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("BufferSize is too small."));
		}

		Result = Value;
		return;
	}

	int32 MaxSize = FMath::Clamp<int32>(BufferSize, 1, 512);
	if (Context.State == EControlRigState::Init)
	{
		Buffer.Reset();
		Buffer.Reserve(MaxSize);
	}
	else
	{
		if(Buffer.Num() < MaxSize)
		{
			Buffer.Add(Value);
			LastInsertIndex = 0;
		}
		else
		{
			Buffer[LastInsertIndex++] = Value;
			if(LastInsertIndex == Buffer.Num())
			{
				LastInsertIndex = 0;
			}
		}

		FVector Location = FVector::ZeroVector;
		FVector AxisX = FVector::ZeroVector;
		FVector AxisY = FVector::ZeroVector;
		FVector Scale = FVector::ZeroVector;
		
		for(const FTransform& F : Buffer)
		{
			Location += F.GetLocation();
			AxisX += F.TransformVectorNoScale(FVector(1.f, 0.f, 0.f));
			AxisY += F.TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
			AxisY += F.GetLocation();
			Scale += F.GetScale3D();
		}

		Location = Location / float(Buffer.Num());
		AxisX = (AxisX / float(Buffer.Num())).GetSafeNormal();
		AxisY = (AxisY / float(Buffer.Num())).GetSafeNormal();
		Scale = Scale / float(Buffer.Num());

		Result.SetLocation(Location);
		Result.SetRotation(FRotationMatrix::MakeFromXY(AxisX, AxisY).ToQuat());
		Result.SetScale3D(Scale);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Units/RigUnitTest.h"

IMPLEMENT_RIGUNIT_AUTOMATION_TEST(FRigUnit_KalmanFloat)
{
	Unit.Value = 1.f;
	Unit.BufferSize = 4;
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected average result"));
	InitAndExecute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected average result"));
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 1.f), TEXT("unexpected average result"));
	Unit.Value = 4.f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 2.f), TEXT("unexpected average result"));
	Unit.Value = 6.f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 3.f), TEXT("unexpected average result"));
	Unit.Value = 5.f;
	Execute();
	AddErrorIfFalse(FMath::IsNearlyEqual(Unit.Result, 4.f), TEXT("unexpected average result"));
	return true;
}

#endif