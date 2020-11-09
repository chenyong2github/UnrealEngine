// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXFixtureComponent.h"
#include "DMXFixtureComponentDouble.generated.h"

// Component that uses 2 DMX channels
UCLASS(ClassGroup = FixtureComponent, meta=(IsBlueprintBase = true), HideCategories = ("Variable", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Sockets"))
class DMXFIXTURES_API UDMXFixtureComponentDouble : public UDMXFixtureComponent
{
	GENERATED_BODY()

public:

	UDMXFixtureComponentDouble();

	// Parameters---------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channels")
	FDMXChannelData DMXChannel1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channels")
	FDMXChannelData DMXChannel2;

	// To iterate channels and avoid code duplication
	int NumChannels;
	TArray<FDMXChannelData*> ChannelRefs;

	// Functions-----------------------------------------
	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXInterpolatedStep(int ChannelIndex);

	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXInterpolatedValue(int ChannelIndex);

	UFUNCTION(BlueprintPure, Category = "DMX")
	float DMXTargetValue(int ChannelIndex);

	UFUNCTION(BlueprintPure, Category = "DMX")
	bool DMXIsInterpolationDone(int ChannelIndex);

	float RemapValue(int ChannelIndex, float Alpha);
	bool IsTargetValid(int ChannelIndex, float Target);
	void Push(int ChannelIndex, float Target);
	void SetTarget(int ChannelIndex, float Target);

	// Overrides
	virtual void InitCells(int NCells) override;
	virtual void SetRangeValue() override;

	// Sets both values of the component
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX", Meta = (DeprecatedProperty, DeprecationMessage = "Deprecated 4.26. Use SetComponentChannel1 and SetComponentChannel2 instead."))
	void SetComponent(float Channel1Value, float Channel2Value);

	// Sets first value of the component
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX")
	void SetComponentChannel1(float Channel1Valuee);

	// Sets second value of the component
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX")
	void SetComponentChannel2(float Channel2Value);
};
