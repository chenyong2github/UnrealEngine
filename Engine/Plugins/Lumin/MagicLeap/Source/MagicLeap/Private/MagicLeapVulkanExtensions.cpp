// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapVulkanExtensions.h"
#include "Lumin/CAPIShims/LuminAPIRemote.h"

#if !PLATFORM_LUMIN
#include "MagicLeapGraphics.h"
#include "AppFramework.h"
#endif // !PLATFORM_LUMIN

#include "MagicLeapHelperVulkan.h"

struct FMagicLeapVulkanExtensions::Implementation
{
#if PLATFORM_WINDOWS
	TArray<VkExtensionProperties> InstanceExtensions;
	TArray<VkExtensionProperties> DeviceExtensions;
#endif
};

FMagicLeapVulkanExtensions::FMagicLeapVulkanExtensions() {}
FMagicLeapVulkanExtensions::~FMagicLeapVulkanExtensions() {}

bool FMagicLeapVulkanExtensions::GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out)
{
	if (!ImpPtr.IsValid())
	{
		ImpPtr.Reset(new Implementation);
	}
#if PLATFORM_LUMIN
	return FMagicLeapHelperVulkan::GetVulkanInstanceExtensionsRequired(Out);
#else
#if (PLATFORM_WINDOWS && WITH_MLSDK)
	// Interrogate the extensions we need for MLRemote.
	TArray<VkExtensionProperties> Extensions;
	{
		MLResult Result = MLResult_Ok;
		uint32_t Count = 0;
		Result = MLRemoteEnumerateRequiredVkInstanceExtensions(nullptr, &Count);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkInstanceExtensions failed with status %d"), Result);
			return false;
		}
		ImpPtr->InstanceExtensions.Empty();
		if (Count > 0)
		{
			ImpPtr->InstanceExtensions.AddDefaulted(Count);
			Result = MLRemoteEnumerateRequiredVkInstanceExtensions(ImpPtr->InstanceExtensions.GetData(), &Count);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkInstanceExtensions failed with status %d"), Result);
				return false;
			}
		}
	}
	for (auto & Extension : ImpPtr->InstanceExtensions)
	{
		Out.Add(Extension.extensionName);
	}
#endif //(PLATFORM_WINDOWS && WITH_MLSDK)
	return true;
#endif // PLATFORM_LUMIN
}

bool FMagicLeapVulkanExtensions::GetVulkanDeviceExtensionsRequired(struct VkPhysicalDevice_T *pPhysicalDevice, TArray<const ANSICHAR*>& Out)
{
	if (!ImpPtr.IsValid())
	{
		ImpPtr.Reset(new Implementation);
	}
#if PLATFORM_LUMIN
	return FMagicLeapHelperVulkan::GetVulkanDeviceExtensionsRequired(pPhysicalDevice, Out);
#else
#if (PLATFORM_WINDOWS && WITH_MLSDK)
	// Interrogate the extensions we need for MLRemote.
	TArray<VkExtensionProperties> Extensions;
	{
		MLResult Result = MLResult_Ok;
		uint32_t Count = 0;
		Result = MLRemoteEnumerateRequiredVkDeviceExtensions(nullptr, &Count);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkDeviceExtensions failed with status %d"), Result);
			return false;
		}
		ImpPtr->DeviceExtensions.Empty();
		if (Count > 0)
		{
			ImpPtr->DeviceExtensions.AddDefaulted(Count);
			Result = MLRemoteEnumerateRequiredVkDeviceExtensions(ImpPtr->DeviceExtensions.GetData(), &Count);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkDeviceExtensions failed with status %d"), Result);
				return false;
			}
		}
	}
	for (auto & Extension : ImpPtr->DeviceExtensions)
	{
		Out.Add(Extension.extensionName);
	}
#endif // (PLATFORM_WINDOWS && WITH_MLSDK)
	return true;
#endif // PLATFORM_LUMIN
}
