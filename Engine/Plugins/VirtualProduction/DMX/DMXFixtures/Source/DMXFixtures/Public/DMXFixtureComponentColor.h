// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXFixtureComponent.h"
#include "DMXFixtureComponentColor.generated.h"

// Specific class to handle color mixing using 4 channels (rgb, cmy, rgbw)
UCLASS(ClassGroup = DMXFixtureComponent, meta = (IsBlueprintBase = true), HideCategories = ("Variable", "Tags", "Activation", "Cooking", "ComponentReplication", "AssetUserData", "Collision", "Sockets"))
class DMXFIXTURES_API UDMXFixtureComponentColor : public UDMXFixtureComponent
{
	GENERATED_BODY()
	
public:

	UDMXFixtureComponentColor();

	unsigned int BitResolution;

	UPROPERTY(EditAnywhere, Category = "DMX Channel")
	FDMXAttributeName ChannelName1;
	UPROPERTY(EditAnywhere, Category = "DMX Channel")
	FDMXAttributeName ChannelName2;
	UPROPERTY(EditAnywhere, Category = "DMX Channel")
	FDMXAttributeName ChannelName3;
	UPROPERTY(EditAnywhere, Category = "DMX Channel")
	FDMXAttributeName ChannelName4;

	TArray<FLinearColor> TargetColorArray;
	FLinearColor* CurrentTargetColorRef;

	bool IsColorValid(FLinearColor NewColor);
	void SetTargetColor(FLinearColor NewColor);

	// Overrides
	virtual void InitCells(int NCells) override;
	virtual void SetBitResolution(TMap<FDMXAttributeName, EDMXFixtureSignalFormat> Map) override;
	virtual void SetCurrentCell(int Index) override;
	
	// Blueprint event
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX")
	void SetComponent(FLinearColor NewColor);

};
