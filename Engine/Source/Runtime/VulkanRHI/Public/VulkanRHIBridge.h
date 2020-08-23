// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanRHIBridge.h: Utils to interact with the inner RHI.
=============================================================================*/

#pragma once 

class FVulkanDynamicRHI;
class FVulkanDevice;

namespace VulkanRHIBridge
{

	VULKANRHI_API void AddEnabledInstanceExtensionsAndLayers(const TArray<const ANSICHAR*>& InInstanceExtensions, const TArray<const ANSICHAR*>& InInstanceLayers);
	VULKANRHI_API void AddEnabledDeviceExtensionsAndLayers(const TArray<const ANSICHAR*>& InDeviceExtensions, const TArray<const ANSICHAR*>& InDeviceLayers);


	VULKANRHI_API uint64 GetInstance(FVulkanDynamicRHI*);

	VULKANRHI_API FVulkanDevice* GetDevice(FVulkanDynamicRHI*);

	// Returns a VkDevice
	VULKANRHI_API uint64 GetLogicalDevice(FVulkanDevice*);

	// Returns a VkDeviceVkPhysicalDevice
	VULKANRHI_API uint64 GetPhysicalDevice(FVulkanDevice*);
}
