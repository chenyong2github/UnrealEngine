// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationValue.generated.h"


USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationValue
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationValue();
	FSoundModulationValue(float InValue, float InAttackTime, float InReleaseTime);

	/** Target value of the modulator. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Value", ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float TargetValue;

	/** Time it takes (in sec) to unitarily increase the bus value (from 0 to 1). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Attack Time (sec)", ClampMin = "0.0", UIMin = "0.0"))
	float AttackTime;

	/** Time it takes (in sec) to unitarily decrease the bus value (from 1 to 0). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Release Time (sec)", ClampMin = "0.0", UIMin = "0.0"))
	float ReleaseTime;

	/** Set current value (for resetting value state only as circumvents lerp, and may result in discontinuity). */
	void SetCurrentValue(float InValue);

	/** Current value lerping toward target */
	float GetCurrentValue() const;

	void Update(float Elapsed);

private:
	float Value;
};
