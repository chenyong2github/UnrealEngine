// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/SubclassOf.h"
#include "InputCoreTypes.h"
#include "Engine/StreamableManager.h"
#include "Templates/SharedPointer.h"
#include "CommonInputSubsystem.h"
#include "CommonInputBaseTypes.h"

#include "Engine/DataTable.h"
#include "CommonInputSettings.generated.h"

UCLASS(config = Game, defaultconfig)
class COMMONINPUT_API UCommonInputSettings : public UObject
{
	GENERATED_BODY()

public:
	UCommonInputSettings(const FObjectInitializer& Initializer);

	// Called to load CommonUISetting data, if bAutoLoadData if set to false then game code must call LoadData().
	void LoadData();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    
    // Called to check that the data we have previously attempted to load is actually loaded and will attempt to load if it is not.
    void ValidateData();

	FDataTableRowHandle GetDefaultClickAction() const;
	FDataTableRowHandle GetDefaultBackAction() const;

	void GetCurrentPlatformDefaults(ECommonInputType& OutDefaultInputType, FName& OutDefaultGamepadName) const;
	FCommonInputPlatformBaseData GetCurrentPlatform() const;

	bool GetEnableInputMethodThrashingProtection() const { return bEnableInputMethodThrashingProtection; }

	int32 GetInputMethodThrashingLimit() const { return InputMethodThrashingLimit; }

	double GetInputMethodThrashingWindowInSeconds() const {	return InputMethodThrashingWindowInSeconds;	}

	double GetInputMethodThrashingCooldownInSeconds() const { return InputMethodThrashingCooldownInSeconds; }

	bool GetAllowOutOfFocusDeviceInput() const { return bAllowOutOfFocusDeviceInput; }

private:
	/** Create a derived asset from UCommonUIInputData to store Input data for your game.*/
	UPROPERTY(config, EditAnywhere, Category = "Input")
	TSoftClassPtr<UCommonUIInputData> InputData;

	UPROPERTY(config, EditAnywhere, EditFixedSize, Category = "Input", Meta = (GetOptions = GetRegisteredPlatforms))
	TMap<FName, FCommonInputPlatformBaseData> CommonInputPlatformData;

	UPROPERTY(config, EditAnywhere, Category = "Thrashing Settings")
	bool bEnableInputMethodThrashingProtection = true;

	UPROPERTY(config, EditAnywhere, Category = "Thrashing Settings")
	int32 InputMethodThrashingLimit = 30;

	UPROPERTY(config, EditAnywhere, Category = "Thrashing Settings")
	double InputMethodThrashingWindowInSeconds = 3.0;

	UPROPERTY(config, EditAnywhere, Category = "Thrashing Settings")
	double InputMethodThrashingCooldownInSeconds = 1.0;

	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bAllowOutOfFocusDeviceInput = false;

	UFUNCTION()
	static const TArray<FName>& GetRegisteredPlatforms();

private:
	void LoadInputData();

	bool bInputDataLoaded;

	UPROPERTY(Transient)
	TSubclassOf<UCommonUIInputData> InputDataClass;

	UPROPERTY(Transient)
	FCommonInputPlatformBaseData CurrentPlatform;
};
