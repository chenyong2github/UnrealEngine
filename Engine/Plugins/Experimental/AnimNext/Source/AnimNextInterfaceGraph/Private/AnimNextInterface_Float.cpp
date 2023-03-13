// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextInterface_Float.h"
#include "AnimNextInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextInterface_Float)

#define LOCTEXT_NAMESPACE "AnimNextInterface"

bool UAnimNextInterface_Float_Multiply::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;
	
	check(Inputs.Num() > 0);

	bool bResult = true;
	float Intermediate = 0.0f;
	float& OutResult = Context.GetResult<float>();
	
	for(const TScriptInterface<IAnimNextInterface>& Input : Inputs)
	{
		bResult &= Interface::GetDataSafe(Input, Context, Intermediate);

		OutResult *= Intermediate;
	}

	return bResult;
}

bool UAnimNextInterface_Float_InterpTo::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;

	float& OutResult = Context.GetResult<float>();
	const float DeltaTime = Context.GetDeltaTime();
	
	float CurrentValue = 0.0f;
	bool bResult = Interface::GetDataSafe(Current, Context, CurrentValue);

	float TargetValue = 0.0f;
	bResult &= Interface::GetDataSafe(Target, Context, TargetValue);

	float SpeedValue = 0.0f;
	bResult &= Interface::GetDataSafe(Speed, Context, SpeedValue);

	OutResult = FMath::FInterpConstantTo(CurrentValue, TargetValue, DeltaTime, SpeedValue);

	return bResult;
}

bool UAnimNextInterface_Float_SpringInterp::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;
	
	FAnimNextInterface_Float_SpringInterpState& State = Context.GetState<FAnimNextInterface_Float_SpringInterpState>(this, 0);
	const float DeltaTime = Context.GetDeltaTime();

	float TargetValue = 0.0f;
	bool bResult = Interface::GetDataSafe(Target, Context, TargetValue);

	float TargetRateValue = 0.0f;
	bResult &= Interface::GetDataSafe(TargetRate, Context, TargetRateValue);

	float SmoothingTimeValue = 0.0f;
	bResult &= Interface::GetDataSafe(SmoothingTime, Context, SmoothingTimeValue);

	float DampingRatioValue = 0.0f;
	bResult &= Interface::GetDataSafe(DampingRatio, Context, DampingRatioValue);

	FMath::SpringDamperSmoothing(
		State.Value,
		State.ValueRate,
		TargetValue,
		TargetRateValue,
		DeltaTime,
		SmoothingTimeValue,
		DampingRatioValue);

	return bResult;
}


#undef LOCTEXT_NAMESPACE
