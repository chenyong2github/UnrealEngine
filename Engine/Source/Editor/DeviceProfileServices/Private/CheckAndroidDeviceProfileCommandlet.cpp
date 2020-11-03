// Copyright Epic Games, Inc. All Rights Reserved.

#include "CheckAndroidDeviceProfileCommandlet.h"
#include "IDeviceProfileSelectorModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCheckAndroidDeviceProfile, Log, All);

int32 UCheckAndroidDeviceProfileCommandlet::Main(const FString& RawCommandLine)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*RawCommandLine, Tokens, Switches, Params);

	IDeviceProfileSelectorModule* AndroidDeviceProfileSelector = FModuleManager::LoadModulePtr<IDeviceProfileSelectorModule>("AndroidDeviceProfileSelector");
	if (ensure(AndroidDeviceProfileSelector != nullptr))
	{
		TMap<FString, FString> DeviceParameters;
		DeviceParameters.Add(TEXT("GPUFamily"), Params.FindRef(TEXT("GPUFamily")));
		DeviceParameters.Add(TEXT("GLVersion"), Params.FindRef(TEXT("GLVersion")));
		DeviceParameters.Add(TEXT("VulkanAvailable"), Params.FindRef(TEXT("VulkanAvailable")));
		DeviceParameters.Add(TEXT("VulkanVersion"), Params.FindRef(TEXT("VulkanVersion")));
		DeviceParameters.Add(TEXT("AndroidVersion"), Params.FindRef(TEXT("AndroidVersion")));
		DeviceParameters.Add(TEXT("DeviceMake"),
			Tokens.Num() == 2 ? Tokens[0] :
			Params.FindRef(TEXT("DeviceMake")));
		DeviceParameters.Add(TEXT("DeviceModel"),
			Tokens.Num() == 1 ? Tokens[0] :
			Tokens.Num() == 2 ? Tokens[1] : 
			Params.FindRef(TEXT("DeviceModel")));
		DeviceParameters.Add(TEXT("DeviceBuildNumber"), Params.FindRef(TEXT("DeviceBuildNumber")));
		DeviceParameters.Add(TEXT("UsingHoudini"), Params.FindRef(TEXT("UsingHoudini")));
		DeviceParameters.Add(TEXT("Hardware"), Params.FindRef(TEXT("Hardware")));
		DeviceParameters.Add(TEXT("Chipset"), Params.FindRef(TEXT("Chipset")));

		FString ProfileName = AndroidDeviceProfileSelector->GetDeviceProfileName(DeviceParameters);

		UE_LOG(LogCheckAndroidDeviceProfile, Display, TEXT("Selected Device Profile: %s"), *ProfileName);
	}

	return 0;
}
