// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationValue.generated.h"

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationValue
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationValue() = default;
	FSoundModulationValue(float InValue, float InAttackTime, float InReleaseTime);

	/** Target value of the modulator. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Value"))
	float TargetValue = 1.0f;

#if WITH_EDITORONLY_DATA
	/** Target value of the modulator (in units if provided). */
	UPROPERTY(Transient, EditAnywhere, Category = General)
	float TargetUnitValue = 1.0f;
#endif // WITH_EDITORONLY_DATA

	/** Time it takes (in sec) to unitarily increase the bus value (from 0 to 1). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Attack Time (sec)", ClampMin = "0.0", UIMin = "0.0"))
	float AttackTime = 0.1f;

	/** Time it takes (in sec) to unitarily decrease the bus value (from 1 to 0). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Release Time (sec)", ClampMin = "0.0", UIMin = "0.0"))
	float ReleaseTime = 0.1f;

	/** Set current value (for resetting value state only as circumvents lerp, and may result in discontinuity). */
	void SetCurrentValue(float InValue);

	/** Current value lerping toward target */
	float GetCurrentValue() const;

	void Update(double Elapsed);

private:
	float Value = 1.0f;
};
