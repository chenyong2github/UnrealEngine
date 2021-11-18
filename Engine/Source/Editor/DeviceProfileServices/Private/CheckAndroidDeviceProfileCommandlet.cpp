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
		TMap<FName, FString> DeviceParameters;
		DeviceParameters.Add(FName(TEXT("SRC_GPUFamily")), Params.FindRef(TEXT("GPUFamily")));
		DeviceParameters.Add(FName(TEXT("SRC_GLVersion")), Params.FindRef(TEXT("GLVersion")));
		DeviceParameters.Add(FName(TEXT("SRC_VulkanAvailable")), Params.FindRef(TEXT("VulkanAvailable")));
		DeviceParameters.Add(FName(TEXT("SRC_VulkanVersion")), Params.FindRef(TEXT("VulkanVersion")));
		DeviceParameters.Add(FName(TEXT("SRC_AndroidVersion")), Params.FindRef(TEXT("AndroidVersion")));
		DeviceParameters.Add(FName(TEXT("SRC_DeviceMake")),
			Tokens.Num() == 2 ? Tokens[0] :
			Params.FindRef(TEXT("DeviceMake")));
		DeviceParameters.Add(FName(TEXT("SRC_DeviceModel")),
			Tokens.Num() == 1 ? Tokens[0] :
			Tokens.Num() == 2 ? Tokens[1] : 
			Params.FindRef(TEXT("DeviceModel")));
		DeviceParameters.Add(FName(TEXT("SRC_DeviceBuildNumber")), Params.FindRef(TEXT("DeviceBuildNumber")));
		DeviceParameters.Add(FName(TEXT("SRC_UsingHoudini")), Params.FindRef(TEXT("UsingHoudini")));
		DeviceParameters.Add(FName(TEXT("SRC_Hardware")), Params.FindRef(TEXT("Hardware")));
		DeviceParameters.Add(FName(TEXT("SRC_Chipset")), Params.FindRef(TEXT("Chipset")));
		DeviceParameters.Add(FName(TEXT("SRC_HMDSystemName")), Params.FindRef(TEXT("HMDSystemName")));
		DeviceParameters.Add(FName(TEXT("SRC_TotalPhysicalGB")), Params.FindRef(TEXT("TotalPhysicalGB")));

		AndroidDeviceProfileSelector->SetSelectorProperties(DeviceParameters);
		FString ProfileName = AndroidDeviceProfileSelector->GetDeviceProfileName();

		UE_LOG(LogCheckAndroidDeviceProfile, Display, TEXT("Selected Device Profile: %s"), *ProfileName);
	}

	return 0;
}
