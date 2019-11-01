// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

//#define VK_USE_PLATFORM_ANDROID_KHR					1

#define VULKAN_ENABLE_DUMP_LAYER					0
#define VULKAN_DYNAMICALLYLOADED					1
#define VULKAN_SHOULD_ENABLE_DRAW_MARKERS			(UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
#define VULKAN_USE_IMAGE_ACQUIRE_FENCES				0
#define VULKAN_SUPPORTS_COLOR_CONVERSIONS			1
#define VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT	0
#define VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2	0
#define VULKAN_SUPPORTS_DEDICATED_ALLOCATION		0
#define VULKAN_SUPPORTS_GPU_CRASH_DUMPS				1


#define ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(EnumMacro)

#define ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(EnumMacro) \
	EnumMacro(PFN_vkCreateSamplerYcbcrConversionKHR, vkCreateSamplerYcbcrConversionKHR) \
	EnumMacro(PFN_vkDestroySamplerYcbcrConversionKHR, vkDestroySamplerYcbcrConversionKHR)

#define ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(EnumMacro)

#include "../VulkanLoader.h"

// and now, include the GenericPlatform class
#include "../VulkanGenericPlatform.h"



class FVulkanLuminPlatform : public FVulkanGenericPlatform
{
public:
	static bool LoadVulkanLibrary();
	static bool LoadVulkanInstanceFunctions(VkInstance inInstance);
	static void FreeVulkanLibrary();

	static void NotifyFoundInstanceLayersAndExtensions(const TArray<FString>& Layers, const TArray<FString>& Extensions);
	static void NotifyFoundDeviceLayersAndExtensions(VkPhysicalDevice PhysicalDevice, const TArray<FString>& Layers, const TArray<FString>& Extensions);

	static void GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions);
	static void GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions);

	static void CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface);

	static bool SupportsBCTextureFormats() { return false; }
	static bool SupportsASTCTextureFormats() { return true; }
	static bool SupportsQuerySurfaceProperties() { return false; }

	static bool RequiresMobileRenderer();

	static void SetupFeatureLevels();

	static bool SupportsStandardSwapchain() { return false; }
	static EPixelFormat GetPixelFormatForNonDefaultSwapchain() { return PF_R8G8B8A8; }

	static bool ForceEnableDebugMarkers();

	static bool HasUnifiedMemory() { return true; }

	static void EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo);

	static bool RequiresWaitingForFrameCompletionEvent() { return false; }

	static VkBool32 DebugReportFunction(VkDebugReportFlagsEXT MsgFlags, VkDebugReportObjectTypeEXT ObjType, uint64_t SrcObject, size_t Location, int32 MsgCode, const ANSICHAR* LayerPrefix, const ANSICHAR* Msg, void* UserData);

protected:
	static void* VulkanLib;
	static bool bAttemptedLoad;
	static VkPhysicalDeviceSamplerYcbcrConversionFeatures SamplerConversion;
};

typedef FVulkanLuminPlatform FVulkanPlatform;
