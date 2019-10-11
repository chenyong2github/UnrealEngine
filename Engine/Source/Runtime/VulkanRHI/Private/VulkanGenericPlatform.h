// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "Containers/ArrayView.h"
#include "RHI.h"	// for GShaderPlatformForFeatureLevel and its friends

struct FOptionalVulkanDeviceExtensions;
class FVulkanDevice;

// the platform interface, and empty implementations for platforms that don't need em
class FVulkanGenericPlatform 
{
public:
	static bool IsSupported() { return true; }
	static void CheckDeviceDriver(uint32 DeviceIndex, EGpuVendorId VendorId, const VkPhysicalDeviceProperties& Props) {}

	static bool LoadVulkanLibrary() { return true; }
	static bool LoadVulkanInstanceFunctions(VkInstance inInstance) { return true; }
	static void FreeVulkanLibrary() {}

	// Called after querying all the available extensions and layers
	static void NotifyFoundInstanceLayersAndExtensions(const TArray<FString>& Layers, const TArray<FString>& Extensions) {}
	static void NotifyFoundDeviceLayersAndExtensions(VkPhysicalDevice PhysicalDevice, const TArray<FString>& Layers, const TArray<FString>& Extensions) {}

	// Array of required extensions for the platform (Required!)
	static void GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions);
	static void GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions);

	// create the platform-specific surface object - required
	static void CreateSurface(VkSurfaceKHR* OutSurface);

	// most platforms support BC* but not ASTC*
	static bool SupportsBCTextureFormats() { return true; }
	static bool SupportsASTCTextureFormats() { return false; }

	// most platforms can query the surface for the present mode, and size, etc
	static bool SupportsQuerySurfaceProperties() { return true; }

	static void SetupFeatureLevels();

	static bool SupportsStandardSwapchain() { return true; }
	static EPixelFormat GetPixelFormatForNonDefaultSwapchain()
	{
		checkf(0, TEXT("Platform Requires Standard Swapchain!"));
		return PF_Unknown;
	}

	static bool SupportsTimestampRenderQueries() { return true; }

	static bool RequiresMobileRenderer() { return false; }

	// bInit=1 called at RHI init time, bInit=0 at RHI deinit time
	static void OverridePlatformHandlers(bool bInit) {}

	// Some platforms have issues with the access flags for the Present layout
	static bool RequiresPresentLayoutFix() { return false; }

	static bool ForceEnableDebugMarkers() { return false; }

	static bool SupportsDeviceLocalHostVisibleWithNoPenalty(EGpuVendorId VendorId) { return false; }

	static bool HasUnifiedMemory() { return false; }

	static bool RegisterGPUWork() { return true; }

	static void WriteCrashMarker(const FOptionalVulkanDeviceExtensions& OptionalExtensions, VkCommandBuffer CmdBuffer, VkBuffer DestBuffer, const TArrayView<uint32>& Entries, bool bAdding) {}

	// Allow the platform code to restrict the device features
	static void RestrictEnabledPhysicalDeviceFeatures(VkPhysicalDeviceFeatures& InOutFeaturesToEnable)
	{ 
		// Disable everything sparse-related
		InOutFeaturesToEnable.shaderResourceResidency	= VK_FALSE;
		InOutFeaturesToEnable.shaderResourceMinLod		= VK_FALSE;
		InOutFeaturesToEnable.sparseBinding				= VK_FALSE;
		InOutFeaturesToEnable.sparseResidencyBuffer		= VK_FALSE;
		InOutFeaturesToEnable.sparseResidencyImage2D	= VK_FALSE;
		InOutFeaturesToEnable.sparseResidencyImage3D	= VK_FALSE;
		InOutFeaturesToEnable.sparseResidency2Samples	= VK_FALSE;
		InOutFeaturesToEnable.sparseResidency4Samples	= VK_FALSE;
		InOutFeaturesToEnable.sparseResidency8Samples	= VK_FALSE;
		InOutFeaturesToEnable.sparseResidencyAliased	= VK_FALSE;
	}

	// Some platforms only support real or non-real UBs, so this function can optimize it out
	static bool UseRealUBsOptimization(bool bCodeHeaderUseRealUBs) { return bCodeHeaderUseRealUBs; }

	static bool SupportParallelRenderingTasks() { return true; }

	/** The status quo is false, so the default is chosen to not change it. As platforms opt in it may be better to flip the default. */
	static bool SupportsDynamicResolution() { return false; }

	// Allow platforms to add extension features to the DeviceInfo pNext chain
	static void EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo) {}

	static bool RequiresSwapchainGeneralInitialLayout() { return false; }
	
	// Allow platforms to do extra work on present
	static VkResult Present(VkQueue Queue, VkPresentInfoKHR& PresentInfo);

	// Ensure the last frame completed on the GPU
	static bool RequiresWaitingForFrameCompletionEvent() { return true; }

	// Does the platform allow a nullptr Pixelshader on the pipeline
	static bool SupportsNullPixelShader() { return true; }

	// Does the platform require resolve attachments in its MSAA renderpasses
	static bool RequiresRenderPassResolveAttachments() { return false; }

	// Checks if the PSO cache matches the expected vulkan device properties
	static bool PSOBinaryCacheMatches(FVulkanDevice* Device, const TArray<uint8>& DeviceCache);

	// Will create the correct format from a generic pso filename
	static FString CreatePSOBinaryCacheFilename(FVulkanDevice* Device, FString CacheFilename);

	// Gathers a list of pso cache filenames to attempt to load
	static TArray<FString> GetPSOCacheFilenames();

	// Return VK_FALSE if platform wants to suppress the given debug report from the validation layers, VK_TRUE to print it.
	static VkBool32 DebugReportFunction(VkDebugReportFlagsEXT MsgFlags, VkDebugReportObjectTypeEXT ObjType, uint64_t SrcObject, size_t Location, int32 MsgCode, const ANSICHAR* LayerPrefix, const ANSICHAR* Msg, void* UserData) { return VK_TRUE; }
};
