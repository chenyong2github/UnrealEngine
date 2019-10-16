// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulationValue.h"

#include "SoundModulationProxy.h"


namespace AudioModulation
{
	const FBusMixId InvalidBusMixId = INDEX_NONE;
	const FBusId    InvalidBusId    = INDEX_NONE;
	const FBusId    InvalidLFOId    = INDEX_NONE;
} // namespace AudioModulation

FSoundModulationValue::FSoundModulationValue()
	: TargetValue(1.0f)
	, AttackTime(0.1f)
	, ReleaseTime(0.1f)
	, Value(1.0f)
{
}

FSoundModulationValue::FSoundModulationValue(float InValue, float InAttackTime, float InReleaseTime)
	: TargetValue(InValue)
	, AttackTime(InAttackTime)
	, ReleaseTime(InReleaseTime)
	, Value(InValue)
{
}

float FSoundModulationValue::GetCurrentValue() const
{
	// Current does not require update to be called if
	// value is attacking or releasing and time is 0
	if (AttackTime <= 0.0f && Value < TargetValue)
	{
		return TargetValue;
	}

	if (ReleaseTime <= 0.0f && Value > TargetValue)
	{
		return TargetValue;
	}

	return Value;
}

void FSoundModulationValue::Update(float Elapsed)
{
	// Attacking
	if (Value < TargetValue)
	{
		if (AttackTime > 0.0f)
		{
			Value = Value + (Elapsed / AttackTime);
			Value = FMath::Min(Value, TargetValue);
		}
		else
		{
			Value = TargetValue;
		}
	}
	// Releasing
	else if (Value > TargetValue)
	{
		if (ReleaseTime > 0.0f)
		{
			Value = Value - (Elapsed / ReleaseTime);
			Value = FMath::Max(Value, TargetValue);
		}
		else
		{
			Value = TargetValue;
		}
	}
}