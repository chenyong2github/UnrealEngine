// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanRHIPrivate.h"
#include "VulkanGenericPlatform.h"
#include "HAL/FileManager.h"

void FVulkanGenericPlatform::SetupFeatureLevels()
{
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2] = SP_VULKAN_PCES3_1;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = SP_VULKAN_PCES3_1;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = SP_VULKAN_SM5;
}

bool FVulkanGenericPlatform::PSOBinaryCacheMatches(FVulkanDevice* Device, const TArray<uint8>& DeviceCache)
{
	if (DeviceCache.Num() > 4)
	{
		uint32* Data = (uint32*)DeviceCache.GetData();
		uint32 HeaderSize = *Data++;
		// 16 is HeaderSize + HeaderVersion
		if (HeaderSize == 16 + VK_UUID_SIZE)
		{
			uint32 HeaderVersion = *Data++;
			if (HeaderVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
			{
				uint32 VendorID = *Data++;
				const VkPhysicalDeviceProperties& DeviceProperties = Device->GetDeviceProperties();
				if (VendorID == DeviceProperties.vendorID)
				{
					uint32 DeviceID = *Data++;
					if (DeviceID == DeviceProperties.deviceID)
					{
						uint8* Uuid = (uint8*)Data;
						if (FMemory::Memcmp(DeviceProperties.pipelineCacheUUID, Uuid, VK_UUID_SIZE) == 0)
						{
							// This particular binary cache matches this device
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

FString FVulkanGenericPlatform::CreatePSOBinaryCacheFilename(FVulkanDevice* Device, FString CacheFilename)
{
	const VkPhysicalDeviceProperties& DeviceProperties = Device->GetDeviceProperties();
	FString BinaryCacheAppendage = FString::Printf(TEXT(".%x.%x"), DeviceProperties.vendorID, DeviceProperties.deviceID);
	if (!CacheFilename.EndsWith(BinaryCacheAppendage))
	{
		CacheFilename += BinaryCacheAppendage;
	}

	return CacheFilename;
}

TArray<FString> FVulkanGenericPlatform::GetPSOCacheFilenames()
{
	TArray<FString> CacheFilenames;
	FString StagedCacheDirectory = FPaths::ProjectDir() / TEXT("Build") / TEXT("ShaderCaches") / FPlatformProperties::IniPlatformName();

	// look for any staged caches
	TArray<FString> StagedCaches;
	IFileManager::Get().FindFiles(StagedCaches, *StagedCacheDirectory, TEXT("cache"));
	// FindFiles returns the filenames without directory, so prepend the stage directory
	for (const FString& Filename : StagedCaches)
	{
		CacheFilenames.Add(StagedCacheDirectory / Filename);
	}

	return CacheFilenames;
}

VkResult FVulkanGenericPlatform::Present(VkQueue Queue, VkPresentInfoKHR& PresentInfo)
{
	return VulkanRHI::vkQueuePresentKHR(Queue, &PresentInfo);
}
