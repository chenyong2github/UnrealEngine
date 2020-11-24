// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "CheckAndroidDeviceProfileCommandlet.generated.h"

/*
 * CheckAndroidDeviceProfile
 *   Commandlet that runs the rules in [/Script/AndroidDeviceProfileSelector.AndroidDeviceProfileMatchingRules]
 *      against the supplied parameters and outputs the device profile that will be matched.
 * Usage:
 * ProjectName -run=DeviceProfileServices.CheckAndroidDeviceProfile DeviceModel
 * ProjectName -run=DeviceProfileServices.CheckAndroidDeviceProfile DeviceMake DeviceModel
 * ProjectName -run=DeviceProfileServices.CheckAndroidDeviceProfile [-DeviceMake=...] [-DeviceModel=...] [-GPUFamily=...] [-GLVersion=...]
	  [-VulkanAvailable=True|False] [-VulkanVersion=...] [-AndroidVersion=...] [-DeviceBuildNumber=...] [-UsingHoudini=True|False] [-Hardware=...] [-Chipset=...]
 */
UCLASS()
class UCheckAndroidDeviceProfileCommandlet
	: public UCommandlet
{
	GENERATED_BODY()
public:

	//~ Begin UCommandlet Interface

	virtual int32 Main(const FString& Params) override;

	//~ End UCommandlet Interface
private:

};
