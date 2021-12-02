// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

#define VULKAN_DYNAMICALLYLOADED					1
#define VULKAN_ENABLE_DUMP_LAYER					0
#define VULKAN_SHOULD_DEBUG_IN_DEVELOPMENT			1
#define VULKAN_SHOULD_ENABLE_DRAW_MARKERS			(UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
#define VULKAN_SIGNAL_UNIMPLEMENTED()				checkf(false, TEXT("Unimplemented vulkan functionality: %s"), __PRETTY_FUNCTION__)
#define VULKAN_SUPPORTS_AMD_BUFFER_MARKER			1

#define VULKAN_RHI_RAYTRACING 						(RHI_RAYTRACING)
#define VULKAN_SUPPORTS_SCALAR_BLOCK_LAYOUT			(VULKAN_RHI_RAYTRACING)

#if VULKAN_RHI_RAYTRACING
#	define UE_VK_API_VERSION						VK_API_VERSION_1_2
#else
#	define UE_VK_API_VERSION						VK_API_VERSION_1_1
#endif // VULKAN_RHI_RAYTRACING

#define ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(EnumMacro)

#define ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(EnumMacro) \

#define ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(EnumMacro) \
    EnumMacro(PFN_vkCmdWriteBufferMarkerAMD, vkCmdWriteBufferMarkerAMD) \
    EnumMacro(PFN_vkCmdSetCheckpointNV, vkCmdSetCheckpointNV) \
    EnumMacro(PFN_vkGetQueueCheckpointDataNV, vkGetQueueCheckpointDataNV) \
    EnumMacro(PFN_vkGetPhysicalDeviceProperties2KHR, vkGetPhysicalDeviceProperties2KHR) \
    EnumMacro(PFN_vkGetPhysicalDeviceFeatures2KHR, vkGetPhysicalDeviceFeatures2KHR) \
    EnumMacro(PFN_vkGetImageMemoryRequirements2KHR , vkGetImageMemoryRequirements2KHR) \
    EnumMacro(PFN_vkGetBufferMemoryRequirements2KHR , vkGetBufferMemoryRequirements2KHR) \
    EnumMacro(PFN_vkGetPhysicalDeviceMemoryProperties2, vkGetPhysicalDeviceMemoryProperties2) \
	EnumMacro(PFN_vkCreateRenderPass2KHR, vkCreateRenderPass2KHR) \
	EnumMacro(PFN_vkCmdBeginRenderPass2KHR, vkCmdBeginRenderPass2KHR) \
	EnumMacro(PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR, vkGetPhysicalDeviceFragmentShadingRatesKHR)

// and now, include the GenericPlatform class
#include "../VulkanGenericPlatform.h"

class FVulkanLinuxPlatform : public FVulkanGenericPlatform
{
public:
	static bool IsSupported();

	static void CheckDeviceDriver(uint32 DeviceIndex, EGpuVendorId VendorId, const VkPhysicalDeviceProperties& Props);
	static bool LoadVulkanLibrary();
	static bool LoadVulkanInstanceFunctions(VkInstance inInstance);
	static void FreeVulkanLibrary();

	static void GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions);
	static void GetInstanceLayers(TArray<const ANSICHAR*>& OutLayers) {}
	static void GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions);
	static void GetDeviceLayers(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutLayers) {}

	static void CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface);

	static bool ForceEnableDebugMarkers();

	static void WriteCrashMarker(const FOptionalVulkanDeviceExtensions& OptionalExtensions, VkCommandBuffer CmdBuffer, VkBuffer DestBuffer, const TArrayView<uint32>& Entries, bool bAdding);

protected:
	static void* VulkanLib;
	static bool bAttemptedLoad;
};

typedef FVulkanLinuxPlatform FVulkanPlatform;
