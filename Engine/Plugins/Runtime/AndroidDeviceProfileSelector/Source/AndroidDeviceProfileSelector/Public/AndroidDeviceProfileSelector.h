// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ANDROIDDEVICEPROFILESELECTOR_API FAndroidDeviceProfileSelector
{
public:
	static FString FindMatchingProfile(const FString& GPUFamily, const FString& GLVersion, const FString& AndroidVersion, const FString& DeviceMake, const FString& DeviceModel, const FString& DeviceBuildNumber, const FString& VulkanAvailable, const FString& VulkanVersion, const FString& UsingHoudini, const FString& Hardware, const FString& Chipset, const FString& ProfileName);
	static int32 GetNumProfiles();
};
