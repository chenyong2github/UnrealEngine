// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VulkanLuminPlatform.h"
#include "../VulkanRHIPrivate.h"
#include <dlfcn.h>

// Vulkan function pointers
#define DEFINE_VK_ENTRYPOINTS(Type,Func) Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_ALL(DEFINE_VK_ENTRYPOINTS)

#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

static bool GFoundTegraGfxDebugger = false;

void* FVulkanLuminPlatform::VulkanLib = nullptr;
bool FVulkanLuminPlatform::bAttemptedLoad = false;
VkPhysicalDeviceSamplerYcbcrConversionFeatures FVulkanLuminPlatform::SamplerConversion;

bool FVulkanLuminPlatform::LoadVulkanLibrary()
{
	if (bAttemptedLoad)
	{
		return (VulkanLib != nullptr);
	}
	bAttemptedLoad = true;

	// try to load libvulkan.so
	VulkanLib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);

	if (VulkanLib == nullptr)
	{
		return false;
	}

	bool bFoundAllEntryPoints = true;

	// Initialize all of the entry points we have to query manually
#define GET_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = (Type)dlsym(VulkanLib, #Func);
	ENUM_VK_ENTRYPOINTS_BASE(GET_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_BASE(CHECK_VK_ENTRYPOINTS);
	if (!bFoundAllEntryPoints)
	{
		dlclose(VulkanLib);
		VulkanLib = nullptr;
		return false;
	}

	ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(GET_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(CHECK_VK_ENTRYPOINTS);
#endif

#undef GET_VK_ENTRYPOINTS
	return true;
}

bool FVulkanLuminPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);
	ENUM_VK_ENTRYPOINTS_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_INSTANCE(CHECK_VK_ENTRYPOINTS);

	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);

	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);


#if VULKAN_HAS_DEBUGGING_ENABLED
	//#todo-rco: Media textures are not working properly, quick workaround
	GValidationCvar->Set(2, ECVF_SetByCommandline);
#endif

	return bFoundAllEntryPoints;
}

void FVulkanLuminPlatform::FreeVulkanLibrary()
{
	if (VulkanLib != nullptr)
	{
#define CLEAR_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = nullptr;
		ENUM_VK_ENTRYPOINTS_ALL(CLEAR_VK_ENTRYPOINTS);

		dlclose(VulkanLib);
		VulkanLib = nullptr;
	}
	bAttemptedLoad = false;
}

void FVulkanLuminPlatform::CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface)
{
	OutSurface = nullptr;
	//VkLuminSurfaceCreateInfoKHR SurfaceCreateInfo;
	//FMemory::Memzero(SurfaceCreateInfo);
	//SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_Lumin_SURFACE_CREATE_INFO_KHR;
	//SurfaceCreateInfo.window = (ANativeWindow*)WindowHandle;

	//VERIFYVULKANRESULT(vkCreateLuminSurfaceKHR(Instance, &SurfaceCreateInfo, nullptr, OutSurface));
}

void FVulkanLuminPlatform::NotifyFoundInstanceLayersAndExtensions(const TArray<FString>& Layers, const TArray<FString>& Extensions)
{
	if (Extensions.Find(TEXT("VK_LAYER_NV_vgd")))
	{
		GFoundTegraGfxDebugger = true;
	}
}

void FVulkanLuminPlatform::NotifyFoundDeviceLayersAndExtensions(VkPhysicalDevice PhysicalDevice, const TArray<FString>& Layers, const TArray<FString>& Extensions)
{
	if (Extensions.Find(TEXT("VK_LAYER_NV_vgd")))
	{
		GFoundTegraGfxDebugger = true;
	}
}

void FVulkanLuminPlatform::GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (GFoundTegraGfxDebugger)
	{
		OutExtensions.Add("VK_LAYER_NV_vgd");
	}
}

void FVulkanLuminPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	if (GFoundTegraGfxDebugger)
	{
		OutExtensions.Add("VK_LAYER_NV_vgd");
	}
	// YCbCr requires BindMem2 and GetMemReqs2
	OutExtensions.Add(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
}

void FVulkanLuminPlatform::SetupFeatureLevels()
{
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES2] = SP_VULKAN_ES3_1_LUMIN;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = SP_VULKAN_ES3_1_LUMIN;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM4_REMOVED] = SP_NumPlatforms;
	GShaderPlatformForFeatureLevel[ERHIFeatureLevel::SM5] = SP_VULKAN_SM5_LUMIN;
}

bool FVulkanLuminPlatform::ForceEnableDebugMarkers()
{
	// Preventing VK_EXT_DEBUG_MARKER from being enabled on Lumin, because the device doesn't support it.
	return GFoundTegraGfxDebugger;
}

void FVulkanLuminPlatform::EnablePhysicalDeviceFeatureExtensions(VkDeviceCreateInfo& DeviceInfo)
{
	SamplerConversion.pNext = nullptr;
	SamplerConversion.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
	SamplerConversion.samplerYcbcrConversion = VK_TRUE;
	DeviceInfo.pNext = &SamplerConversion;
}

bool FVulkanLuminPlatform::RequiresMobileRenderer()
{
	return !FLuminPlatformMisc::ShouldUseDesktopVulkan();
}

VkBool32 FVulkanLuminPlatform::DebugReportFunction(
	VkDebugReportFlagsEXT			MsgFlags,
	VkDebugReportObjectTypeEXT		ObjType,
	uint64_t						SrcObject,
	size_t							Location,
	int32							MsgCode,
	const ANSICHAR*					LayerPrefix,
	const ANSICHAR*					Msg,
	void*							UserData)
{
	if (MsgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		if (!FCStringAnsi::Strcmp(LayerPrefix, "ParameterValidation"))
		{
			// Function called but its required extension has not been enabled.
			if (MsgCode == 0xa)
			{
				// We don't want to disable all messages that fall into this category, just the ones we wont/cant fix.

				// VK_EXT_debug_marker is not available on lumin unless running through the debugger.
				if (FCStringAnsi::Strfind(Msg, "VK_EXT_debug_marker") != nullptr)
				{
					return VK_FALSE;
				}
			}
		}
	}
	else if (MsgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		if (!FCStringAnsi::Strcmp(LayerPrefix, "ParameterValidation"))
		{
			// Struct from a private extension used.
			if (MsgCode == 0x9e1c40d)
			{
				// vkCreateImage: pCreateInfo->pNext chain includes a structure with unknown VkStructureType (1000027002); 
				// Allowed structures are [VkDedicatedAllocationImageCreateInfoNV, VkExternalMemoryImageCreateInfoKHR, VkExternalMemoryImageCreateInfoNV, VkImageFormatListCreateInfoKHR, VkImageSwapchainCreateInfoKHX].
				// The spec valid usage text states 'Each pNext member of any structure (including this one) in the pNext chain must be either NULL or a pointer to a valid instance of
				// VkDedicatedAllocationImageCreateInfoNV, VkExternalMemoryImageCreateInfoKHR, VkExternalMemoryImageCreateInfoNV, VkImageFormatListCreateInfoKHR, or VkImageSwapchainCreateInfoKHX'
				// (https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#VUID-VkImageCreateInfo-pNext-pNext) This warning is based on the Valid Usage documentation for version 69 of the Vulkan header.
				// It is possible that you are using a struct from a private extension or an extension that was added to a later version of the Vulkan header, in which case your use of pCreateInfo->pNext is perfectly valid
				// but is not guaranteed to work correctly with validation enabled.
				return VK_FALSE;
			}
		}
	}

	return VK_TRUE;
}
