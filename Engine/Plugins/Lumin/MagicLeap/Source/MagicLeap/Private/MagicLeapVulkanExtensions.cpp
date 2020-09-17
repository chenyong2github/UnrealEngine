// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapVulkanExtensions.h"
#include "Lumin/CAPIShims/LuminAPIRemote.h"

#if !PLATFORM_LUMIN
#include "MagicLeapGraphics.h"
#include "AppFramework.h"
#endif // !PLATFORM_LUMIN

#include "MagicLeapHelperVulkan.h"

#if PLATFORM_WINDOWS
struct FMagicLeapVulkanExtensionsImpl
{
	TArray<VkExtensionProperties> InstanceExtensions;
	TArray<VkExtensionProperties> DeviceExtensions;
};
#endif

FMagicLeapVulkanExtensions::FMagicLeapVulkanExtensions()
#if PLATFORM_WINDOWS
: ImpPtr(new FMagicLeapVulkanExtensionsImpl())
#endif // PLATFORM_WINDOWS
{
}

FMagicLeapVulkanExtensions::~FMagicLeapVulkanExtensions()
{
#if PLATFORM_WINDOWS
	delete ImpPtr;
	ImpPtr = nullptr;
#endif // PLATFORM_WINDOWS
}

bool FMagicLeapVulkanExtensions::GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out)
{
#if PLATFORM_LUMIN
	return FMagicLeapHelperVulkan::GetVulkanInstanceExtensionsRequired(Out);
#else
#if PLATFORM_WINDOWS && WITH_MLSDK

	// Retrieve the extensions we need for MLRemote only once.
	if (ImpPtr->InstanceExtensions.Num() == 0)
	{
		uint32_t Count = 0;
		MLResult Result = MLRemoteEnumerateRequiredVkInstanceExtensions(nullptr, &Count);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkInstanceExtensions failed with status %d"), Result);
			return false;
		}

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

	// Give pointers from storage
	for (auto& Extension : ImpPtr->InstanceExtensions)
	{
		Out.Add(Extension.extensionName);
	}
#endif //(PLATFORM_WINDOWS && WITH_MLSDK)
	return true;
#endif // PLATFORM_LUMIN
}

bool FMagicLeapVulkanExtensions::GetVulkanDeviceExtensionsRequired(struct VkPhysicalDevice_T* pPhysicalDevice, TArray<const ANSICHAR*>& Out)
{
#if PLATFORM_LUMIN
	return FMagicLeapHelperVulkan::GetVulkanDeviceExtensionsRequired(pPhysicalDevice, Out);
#else
#if PLATFORM_WINDOWS && WITH_MLSDK

	// Retrieve the extensions we need for MLRemote only once.
	if (ImpPtr->DeviceExtensions.Num() == 0)
	{
		uint32_t Count = 0;
		MLResult Result = MLRemoteEnumerateRequiredVkDeviceExtensions(nullptr, &Count);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteEnumerateRequiredVkDeviceExtensions failed with status %d"), Result);
			return false;
		}

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

	// Give pointers from storage
	for (auto& Extension : ImpPtr->DeviceExtensions)
	{
		Out.Add(Extension.extensionName);
	}
#endif // (PLATFORM_WINDOWS && WITH_MLSDK)
	return true;
#endif // PLATFORM_LUMIN
}
