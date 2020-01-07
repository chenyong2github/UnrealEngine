// Copyright Epic Games, Inc. All Rights Reserved.

#include "AndroidDeviceProfileSelectorModule.h"
#include "AndroidDeviceProfileSelector.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FAndroidDeviceProfileSelectorModule, AndroidDeviceProfileSelector);

DEFINE_LOG_CATEGORY_STATIC(LogAndroidDPSelector, Log, All)

void FAndroidDeviceProfileSelectorModule::StartupModule()
{
}

void FAndroidDeviceProfileSelectorModule::ShutdownModule()
{
}

const FString FAndroidDeviceProfileSelectorModule::GetRuntimeDeviceProfileName()
{
	// We are not expecting this module to have GetRuntimeDeviceProfileName called directly.
	// Android ProfileSelectorModule runtime is now in FAndroidDeviceProfileSelectorRuntimeModule.
	// Use GetDeviceProfileName.
	checkNoEntry();
	return FString();
}

const FString FAndroidDeviceProfileSelectorModule::GetDeviceProfileName(const TMap<FString, FString>& DeviceParameters)
{
	FString ProfileName; 

	// Pull out required device parameters:
	FString GPUFamily = DeviceParameters.FindChecked("GPUFamily");
	FString GLVersion = DeviceParameters.FindChecked("GLVersion");
	FString VulkanAvailable = DeviceParameters.FindChecked("VulkanAvailable");
	FString VulkanVersion = DeviceParameters.FindChecked("VulkanVersion");
	FString AndroidVersion = DeviceParameters.FindChecked("AndroidVersion");
	FString DeviceMake = DeviceParameters.FindChecked("DeviceMake");
	FString DeviceModel = DeviceParameters.FindChecked("DeviceModel");
	FString DeviceBuildNumber = DeviceParameters.FindChecked("DeviceBuildNumber");
	FString UsingHoudini = DeviceParameters.FindChecked("UsingHoudini");
	FString Hardware = DeviceParameters.FindChecked("Hardware");
	FString Chipset = DeviceParameters.FindChecked("Chipset");

	UE_LOG(LogAndroidDPSelector, Log, TEXT("Checking %d rules from DeviceProfile ini file."), FAndroidDeviceProfileSelector::GetNumProfiles() );
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  Default profile: %s"), *ProfileName);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  GpuFamily: %s"), *GPUFamily);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  GlVersion: %s"), *GLVersion);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  VulkanAvailable: %s"), *VulkanAvailable);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  VulkanVersion: %s"), *VulkanVersion);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  AndroidVersion: %s"), *AndroidVersion);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  DeviceMake: %s"), *DeviceMake);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  DeviceModel: %s"), *DeviceModel);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  DeviceBuildNumber: %s"), *DeviceBuildNumber);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  UsingHoudini: %s"), *UsingHoudini);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  Hardware: %s"), *Hardware);
	UE_LOG(LogAndroidDPSelector, Log, TEXT("  Chipset: %s"), *Chipset);

	ProfileName = FAndroidDeviceProfileSelector::FindMatchingProfile(GPUFamily, GLVersion, AndroidVersion, DeviceMake, DeviceModel, DeviceBuildNumber, VulkanAvailable, VulkanVersion, UsingHoudini, Hardware, Chipset, ProfileName);

	UE_LOG(LogAndroidDPSelector, Log, TEXT("Selected Device Profile: [%s]"), *ProfileName);

	return ProfileName;
}
