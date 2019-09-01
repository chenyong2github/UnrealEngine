// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationValue.generated.h"


namespace AudioModulation
{
	using BusMixId = uint32;
	extern const BusMixId InvalidBusMixId;

	using BusId = uint32;
	extern const BusId InvalidBusId;

	using LFOId = uint32;
	extern const LFOId InvalidLFOId;
} // namespace AudioModulation

USTRUCT(BlueprintType)
struct FSoundModulationValue
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationValue();
	FSoundModulationValue(float InValue, float InAttackTime, float InReleaseTime);

	/** Target value of the modulator. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Value", ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float TargetValue;

	/** Time it takes (in sec) to unitarily increase the bus value (from 0 to 1). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Attack Time", ClampMin = "0.0", UIMin = "0.0"))
	float AttackTime;

	/** Time it takes (in sec) to unitarily decrease the bus value (from 1 to 0). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Release Time", ClampMin = "0.0", UIMin = "0.0"))
	float ReleaseTime;

	/** Current value lerping toward target */
	float GetCurrentValue() const;

	void Update(float Elapsed);

private:
	float Value;
};
