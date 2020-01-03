// Copyright Epic Games, Inc. All Rights Reserved.

#ifndef PLATFORM_LUMIN
	#define	PLATFORM_LUMIN	0
#endif

#ifndef PLATFORM_LUMINGL4
	#define	PLATFORM_LUMINGL4	0
#endif

//#todo-Lumin: Remove this define when it becomes untangled from Android
#if (!defined(PLATFORM_LUMIN) || (!PLATFORM_LUMIN))
#include "VulkanAndroidPlatform.h"
#include "../VulkanRHIPrivate.h"
#include <dlfcn.h>
#include "Android/AndroidWindow.h"

// Vulkan function pointers
#define DEFINE_VK_ENTRYPOINTS(Type,Func) Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_ALL(DEFINE_VK_ENTRYPOINTS)


void* FVulkanAndroidPlatform::VulkanLib = nullptr;
bool FVulkanAndroidPlatform::bAttemptedLoad = false;

#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

bool FVulkanAndroidPlatform::LoadVulkanLibrary()
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

#define GET_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = (Type)dlsym(VulkanLib, #Func);

	ENUM_VK_ENTRYPOINTS_BASE(GET_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_BASE(CHECK_VK_ENTRYPOINTS);
	if (!bFoundAllEntryPoints)
	{
		dlclose(VulkanLib);
		VulkanLib = nullptr;
		return false;
	}

	ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(GET_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(CHECK_VK_ENTRYPOINTS);
#endif

#undef GET_VK_ENTRYPOINTS

	return true;
}

bool FVulkanAndroidPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	bool bFoundAllEntryPoints = true;

#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);

	ENUM_VK_ENTRYPOINTS_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_INSTANCE(CHECK_VK_ENTRYPOINTS);

	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(CHECK_VK_ENTRYPOINTS);

	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);

	if (!bFoundAllEntryPoints)
	{
		return false;
	}

	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(CHECK_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);
#endif

#undef GETINSTANCE_VK_ENTRYPOINTS

	return true;
}

void FVulkanAndroidPlatform::FreeVulkanLibrary()
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

#undef CHECK_VK_ENTRYPOINTS

void FVulkanAndroidPlatform::CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface)
{
	// don't use cached window handle coming from VulkanViewport, as it could be gone by now
	WindowHandle = FAndroidWindow::GetHardwareWindow_EventThread();
	if (WindowHandle == NULL)
	{

		// Sleep if the hardware window isn't currently available.
		// The Window may not exist if the activity is pausing/resuming, in which case we make this thread wait
		FPlatformMisc::LowLevelOutputDebugString(TEXT("Waiting for Native window in FVulkanAndroidPlatform::CreateSurface"));
		WindowHandle = FAndroidWindow::WaitForHardwareWindow();

		if (WindowHandle == NULL)
		{
			FPlatformMisc::LowLevelOutputDebugString(TEXT("Aborting FVulkanAndroidPlatform::CreateSurface, FAndroidWindow::WaitForHardwareWindow() returned null"));
			return;
		}
	}

	VkAndroidSurfaceCreateInfoKHR SurfaceCreateInfo;
	ZeroVulkanStruct(SurfaceCreateInfo, VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR);
	SurfaceCreateInfo.window = (ANativeWindow*)WindowHandle;

	VERIFYVULKANRESULT(VulkanDynamicAPI::vkCreateAndroidSurfaceKHR(Instance, &SurfaceCreateInfo, VULKAN_CPU_ALLOCATOR, OutSurface));
}



void FVulkanAndroidPlatform::GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(VK_KHR_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
}

void FVulkanAndroidPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(VK_KHR_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
	OutExtensions.Add(VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME);
}

bool FVulkanAndroidPlatform::SupportsStandardSwapchain()
{
	if (FPlatformMisc::IsStandaloneStereoOnlyDevice())
	{
		return false;
	}
	else
	{
		return FVulkanGenericPlatform::SupportsStandardSwapchain();
	}
}

EPixelFormat FVulkanAndroidPlatform::GetPixelFormatForNonDefaultSwapchain()
{
	if (FPlatformMisc::IsStandaloneStereoOnlyDevice())
	{
		return PF_R8G8B8A8;
	}
	else
	{
		return FVulkanGenericPlatform::GetPixelFormatForNonDefaultSwapchain();
	}
}

bool FVulkanAndroidPlatform::SupportsTimestampRenderQueries()
{
	// standalone devices have newer drivers where timestamp render queries work.
	return FPlatformMisc::IsStandaloneStereoOnlyDevice();
}

void FVulkanAndroidPlatform::OverridePlatformHandlers(bool bInit)
{
	if (bInit)
	{
		FPlatformMisc::SetOnReInitWindowCallback(FVulkanDynamicRHI::RecreateSwapChain);
		FPlatformMisc::SetOnReleaseWindowCallback(FVulkanDynamicRHI::DestroySwapChain);
		FPlatformMisc::SetOnPauseCallback(FVulkanDynamicRHI::SavePipelineCache);
	}
	else
	{
		FPlatformMisc::SetOnReInitWindowCallback(nullptr);
		FPlatformMisc::SetOnReleaseWindowCallback(nullptr);
		FPlatformMisc::SetOnPauseCallback(nullptr);
	}
}

#endif
