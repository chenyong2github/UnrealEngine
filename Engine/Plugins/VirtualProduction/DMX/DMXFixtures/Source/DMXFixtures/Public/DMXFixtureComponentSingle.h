// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXFixtureComponent.h"
#include "DMXFixtureComponentSingle.generated.h"

// Component that uses 1 DMX channel
UCLASS(ClassGroup = FixtureComponent, meta=(IsBlueprintBase = true), HideCategories = ("Variable", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Sockets"))
class DMXFIXTURES_API UDMXFixtureComponentSingle : public UDMXFixtureComponent
{
	GENERATED_BODY()

public:

	UDMXFixtureComponentSingle();
	int NumChannels;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channels")
	FDMXChannelData DMXChannel;

	// Functions-----------------------------------------
	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXInterpolatedStep();

	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXInterpolatedValue();

	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXTargetValue();

	UFUNCTION(BlueprintPure, Category = "DMX")
	bool DMXIsInterpolationDone();
	
	float RemapValue(float Alpha);
	bool IsTargetValid(float Target);
	void Push(float Target);
	void SetTarget(float Target);

	// Overrides
	virtual void InitCells(int NCells) override;
	virtual void SetRangeValue() override;

	// Blueprint event
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX Component")
	void SetComponent(float NewValue);

};
