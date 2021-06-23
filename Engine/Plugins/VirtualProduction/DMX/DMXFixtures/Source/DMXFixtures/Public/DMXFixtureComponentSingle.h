// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXFixtureComponent.h"
#include "DMXFixtureComponentSingle.generated.h"

// Component that uses 1 DMX channel
UCLASS(ClassGroup = FixtureComponent, meta=(IsBlueprintBase = true))
class DMXFIXTURES_API UDMXFixtureComponentSingle : public UDMXFixtureComponent
{
	GENERATED_BODY()

public:
	UDMXFixtureComponentSingle();
		
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channels")
	FDMXChannelData DMXChannel;

	/** Initializes the interpolation range of the channels */
	virtual void Initialize() override;

	/** Gets the interpolation delta value (step) for this frame */
	UFUNCTION(BlueprintPure, Category = "DMX")
	float GetDMXInterpolatedStep() const;

	/** Gets the current interpolated value */
	UFUNCTION(BlueprintPure, Category = "DMX")
	float GetDMXInterpolatedValue() const;

	/** Gets the target value towards which the component interpolates */
	UFUNCTION(BlueprintPure, Category = "DMX")
	float GetDMXTargetValue() const;

	/** True if the target value is reached and no interpolation is required */
	UFUNCTION(BlueprintPure, Category = "DMX")
	bool IsDMXInterpolationDone() const;
	
	/** Returns the interpolated value */
	float GetInterpolatedValue(float Alpha) const;

	bool IsTargetValid(float Target);

	/** Sets the target value. Interpolates to the value if bUseInterpolation is true. */
	void SetTargetValue(float Value);

	/** Called to set the value. When interpolation is enabled this function is called by the plugin until the target value is reached, else just once. */
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX Component")
	void SetValueNoInterp(float NewValue);

	// DEPRECATED 4.27
public:	
	// DEPRECATED 4.27
	UE_DEPRECATED(4.27, "Replaced with SetChannelValue to be more expressive about the intent of the function and to avoid the duplicate method Push and SetTarget.")
	void Push(float Target);

	// DEPRECATED 4.27
	UE_DEPRECATED(4.27, "Replaced with SetChannelValue to be more expressive about the intent of the function.")
	void SetTarget(float Target);
};
