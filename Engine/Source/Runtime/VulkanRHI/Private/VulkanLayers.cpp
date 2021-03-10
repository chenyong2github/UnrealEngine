// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanLayers.cpp: Vulkan device layers implementation.
=============================================================================*/

#include "VulkanRHIPrivate.h"
#include "IHeadMountedDisplayModule.h"
#include "IHeadMountedDisplayVulkanExtensions.h"
#include "VulkanRHIBridge.h"
namespace VulkanRHIBridge
{
	extern TArray<const ANSICHAR*> InstanceExtensions;
	extern TArray<const ANSICHAR*> InstanceLayers;
	extern TArray<const ANSICHAR*> DeviceExtensions;
	extern TArray<const ANSICHAR*> DeviceLayers;
}
#if VULKAN_HAS_DEBUGGING_ENABLED
bool GRenderDocFound = false;
#endif

#if VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"
#endif

#if VULKAN_HAS_DEBUGGING_ENABLED
TAutoConsoleVariable<int32> GValidationCvar(
	TEXT("r.Vulkan.EnableValidation"),
	0,
	TEXT("0 to disable validation layers (default)\n")
	TEXT("1 to enable errors\n")
	TEXT("2 to enable errors & warnings\n")
	TEXT("3 to enable errors, warnings & performance warnings\n")
	TEXT("4 to enable errors, warnings, performance & information messages\n")
	TEXT("5 to enable all messages"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> GStandardValidationCvar(
	TEXT("r.Vulkan.StandardValidation"),
	2,
	TEXT("2 to use VK_LAYER_KHRONOS_validation (default) if available\n")
	TEXT("1 to use VK_LAYER_LUNARG_standard_validation if available, or \n")
	TEXT("0 to use individual validation layers (removed)"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> GGPUValidationCvar(
	TEXT("r.Vulkan.GPUValidation"),
	0,
	TEXT("2 to use enable GPU assisted validation AND extra binding slot when using validation layers\n")
	TEXT("1 to use enable GPU assisted validation when using validation layers, or\n")
	TEXT("0 to not use (default)"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

#if VULKAN_ENABLE_DRAW_MARKERS
	#define RENDERDOC_LAYER_NAME				"VK_LAYER_RENDERDOC_Capture"
#endif

#define KHRONOS_STANDARD_VALIDATION_LAYER_NAME	"VK_LAYER_KHRONOS_validation"
#define STANDARD_VALIDATION_LAYER_NAME			"VK_LAYER_LUNARG_standard_validation"

#endif // VULKAN_HAS_DEBUGGING_ENABLED

// Instance Extensions to enable for all platforms
static const ANSICHAR* GInstanceExtensions[] =
{
#if VULKAN_SUPPORTS_EXTERNAL_MEMORY
	VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
#endif
#if VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#endif
#if VULKAN_SUPPORTS_VALIDATION_CACHE
	VK_EXT_VALIDATION_CACHE_EXTENSION_NAME,
#endif
	nullptr
};

// Device Extensions to enable
static const ANSICHAR* GDeviceExtensions[] =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,

	//"VK_KHX_device_group",
#if VULKAN_SUPPORTS_MAINTENANCE_LAYER1
	VK_KHR_MAINTENANCE1_EXTENSION_NAME,
#endif

#if VULKAN_SUPPORTS_MAINTENANCE_LAYER2
	VK_KHR_MAINTENANCE2_EXTENSION_NAME,
#endif

#if VULKAN_SUPPORTS_VALIDATION_CACHE
	VK_EXT_VALIDATION_CACHE_EXTENSION_NAME,
#endif

#if VULKAN_SUPPORTS_MEMORY_BUDGET
	VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
#endif

#if VULKAN_SUPPORTS_SCALAR_BLOCK_LAYOUT
	VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
#endif


#if VULKAN_SUPPORTS_MEMORY_PRIORITY
	VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
#endif

#if VULKAN_SUPPORTS_SEPARATE_DEPTH_STENCIL_LAYOUTS
	// If we decide to support separate depth-stencil transitions, enable this.
	//VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
	//VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME,
#endif

	//VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
	
#if VULKAN_SUPPORTS_BUFFER_64BIT_ATOMICS
	VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME,
#endif

	VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,

	nullptr
};

TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > FVulkanDynamicRHI::HMDVulkanExtensions;

struct FLayerExtension
{
	FLayerExtension()
	{
		FMemory::Memzero(LayerProps);
	}

	void AddUniqueExtensionNames(TArray<FString>& Out)
	{
		for (int32 ExtIndex = 0; ExtIndex < ExtensionProps.Num(); ++ExtIndex)
		{
			Out.AddUnique(ANSI_TO_TCHAR(ExtensionProps[ExtIndex].extensionName));
		}
	}

	void AddAnsiExtensionNames(TArray<const char*>& Out)
	{
		for (int32 ExtIndex = 0; ExtIndex < ExtensionProps.Num(); ++ExtIndex)
		{
			Out.AddUnique(ExtensionProps[ExtIndex].extensionName);
		}
	}

	VkLayerProperties LayerProps;
	TArray<VkExtensionProperties> ExtensionProps;
};


static void ErrorPotentialBadInstallation(const ANSICHAR* VkFunction, const ANSICHAR* Filename, uint32 Line)
{
	UE_LOG(LogVulkanRHI, Error, TEXT("%s failed\n at %s:%u\nThis typically means Vulkan is not properly set up in your system; try running vulkaninfo from the Vulkan SDK."), ANSI_TO_TCHAR(VkFunction), ANSI_TO_TCHAR(Filename), Line);
}

#define VERIFYVULKANRESULT_INIT(VkFunction)		{ const VkResult ScopedResult = VkFunction; \
													if (ScopedResult == VK_ERROR_INITIALIZATION_FAILED) { ErrorPotentialBadInstallation(#VkFunction, __FILE__, __LINE__); } \
													else if (ScopedResult < VK_SUCCESS) { VulkanRHI::VerifyVulkanResult(ScopedResult, #VkFunction, __FILE__, __LINE__); }}

static inline void EnumerateInstanceExtensionProperties(const ANSICHAR* LayerName, FLayerExtension& OutLayer)
{
	uint32 Count = 0;
	VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateInstanceExtensionProperties(LayerName, &Count, nullptr));
	if (Count > 0)
	{
		OutLayer.ExtensionProps.Empty(Count);
		OutLayer.ExtensionProps.AddUninitialized(Count);
		VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateInstanceExtensionProperties(LayerName, &Count, OutLayer.ExtensionProps.GetData()));
	}
}

static inline void EnumerateDeviceExtensionProperties(VkPhysicalDevice Device, const ANSICHAR* LayerName, FLayerExtension& OutLayer)
{
	uint32 Count = 0;
	VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateDeviceExtensionProperties(Device, LayerName, &Count, nullptr));
	if (Count > 0)
	{
		OutLayer.ExtensionProps.Empty(Count);
		OutLayer.ExtensionProps.AddUninitialized(Count);
		VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateDeviceExtensionProperties(Device, LayerName, &Count, OutLayer.ExtensionProps.GetData()));
	}
}


static inline void TrimDuplicates(TArray<const ANSICHAR*>& Array)
{
	for (int32 OuterIndex = Array.Num() - 1; OuterIndex >= 0; --OuterIndex)
	{
		bool bFound = false;
		for (int32 InnerIndex = OuterIndex - 1; InnerIndex >= 0; --InnerIndex)
		{
			if (!FCStringAnsi::Strcmp(Array[OuterIndex], Array[InnerIndex]))
			{
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			Array.RemoveAtSwap(OuterIndex, 1, false);
		}
	}
}

static inline int32 FindLayerIndexInList(const TArray<FLayerExtension>& List, const char* LayerName)
{
	// 0 is reserved for NULL/instance
	for (int32 Index = 1; Index < List.Num(); ++Index)
	{
		if (!FCStringAnsi::Strcmp(List[Index].LayerProps.layerName, LayerName))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

static inline bool FindLayerInList(const TArray<FLayerExtension>& List, const char* LayerName)
{
	return FindLayerIndexInList(List, LayerName) != INDEX_NONE;
}

static inline bool FindLayerExtensionInList(const TArray<FLayerExtension>& List, const char* ExtensionName, const char*& FoundLayer)
{
	for (int32 Index = 0; Index < List.Num(); ++Index)
	{
		for (int32 ExtIndex = 0; ExtIndex < List[Index].ExtensionProps.Num(); ++ExtIndex)
		{
			if (!FCStringAnsi::Strcmp(List[Index].ExtensionProps[ExtIndex].extensionName, ExtensionName))
			{
				FoundLayer = List[Index].LayerProps.layerName;
				return true;
			}
		}
	}

	return false;
}

static inline bool FindLayerExtensionInList(const TArray<FLayerExtension>& List, const char* ExtensionName)
{
	const char* Dummy = nullptr;
	return FindLayerExtensionInList(List, ExtensionName, Dummy);
}


void FVulkanDynamicRHI::GetInstanceLayersAndExtensions(TArray<const ANSICHAR*>& OutInstanceExtensions, TArray<const ANSICHAR*>& OutInstanceLayers, bool& bOutDebugUtils)
{
	bOutDebugUtils = false;

	TArray<FLayerExtension> GlobalLayerExtensions;
	// 0 is reserved for NULL/instance
	GlobalLayerExtensions.AddDefaulted();

	// Global extensions
	EnumerateInstanceExtensionProperties(nullptr, GlobalLayerExtensions[0]);

	TArray<FString> FoundUniqueExtensions;
	TArray<FString> FoundUniqueLayers;
	for (int32 Index = 0; Index < GlobalLayerExtensions[0].ExtensionProps.Num(); ++Index)
	{
		FoundUniqueExtensions.AddUnique(ANSI_TO_TCHAR(GlobalLayerExtensions[0].ExtensionProps[Index].extensionName));
	}

	{
		TArray<VkLayerProperties> GlobalLayerProperties;
		uint32 InstanceLayerCount = 0;
		VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateInstanceLayerProperties(&InstanceLayerCount, nullptr));
		if (InstanceLayerCount > 0)
		{
			GlobalLayerProperties.AddZeroed(InstanceLayerCount);
			VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateInstanceLayerProperties(&InstanceLayerCount, &GlobalLayerProperties[GlobalLayerProperties.Num() - InstanceLayerCount]));
		}

		for (int32 Index = 0; Index < GlobalLayerProperties.Num(); ++Index)
		{
			FLayerExtension* Layer = new(GlobalLayerExtensions) FLayerExtension;
			Layer->LayerProps = GlobalLayerProperties[Index];
			EnumerateInstanceExtensionProperties(GlobalLayerProperties[Index].layerName, *Layer);
			Layer->AddUniqueExtensionNames(FoundUniqueExtensions);
			FoundUniqueLayers.AddUnique(ANSI_TO_TCHAR(GlobalLayerProperties[Index].layerName));
		}
	}

	UE_LOG(LogVulkanRHI, Display, TEXT("- Found %d instance layers"), FoundUniqueLayers.Num());
	if (FoundUniqueLayers.Num() > 0)
	{
		FoundUniqueLayers.Sort();
		for (const FString& Name : FoundUniqueLayers)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("* %s"), *Name);
		}
	}

	UE_LOG(LogVulkanRHI, Display, TEXT("- Found %d instance extensions"), FoundUniqueExtensions.Num());
	if (FoundUniqueExtensions.Num() > 0)
	{
		FoundUniqueExtensions.Sort();
		for (const FString& Name : FoundUniqueExtensions)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("* %s"), *Name);
		}
	}

	FVulkanPlatform::NotifyFoundInstanceLayersAndExtensions(FoundUniqueLayers, FoundUniqueExtensions);

	bool bGfxReconstructOrVkTrace = false;
	if (FParse::Param(FCommandLine::Get(), TEXT("vktrace")))
	{
		const char* GfxReconstructName = "VK_LAYER_LUNARG_gfxreconstruct";
		if (FindLayerInList(GlobalLayerExtensions, GfxReconstructName))
		{
			OutInstanceLayers.Add(GfxReconstructName);
			bGfxReconstructOrVkTrace = true;
		}
		else
		{
			const char* VkTraceName = "VK_LAYER_LUNARG_vktrace";
			if (FindLayerInList(GlobalLayerExtensions, VkTraceName))
			{
				OutInstanceLayers.Add(VkTraceName);
				bGfxReconstructOrVkTrace = true;
			}
		}
	}

#if VULKAN_HAS_DEBUGGING_ENABLED
	if (FParse::Param(FCommandLine::Get(), TEXT("vulkanapidump")))
	{
		if (bGfxReconstructOrVkTrace)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Can't enable api_dump when GfxReconstruct/VkTrace is enabled"));
		}
		else
		{
			const char* VkApiDumpName = "VK_LAYER_LUNARG_api_dump";
			bool bApiDumpFound = FindLayerInList(GlobalLayerExtensions, VkApiDumpName);
			if (bApiDumpFound)
			{
				OutInstanceLayers.Add(VkApiDumpName);
				FPlatformMisc::SetEnvironmentVar(TEXT("VK_APIDUMP_LOG_FILENAME"), TEXT("vk_apidump.txt"));
				FPlatformMisc::SetEnvironmentVar(TEXT("VK_APIDUMP_DETAILED"), TEXT("true"));
				FPlatformMisc::SetEnvironmentVar(TEXT("VK_APIDUMP_FLUSH"), TEXT("true"));
				FPlatformMisc::SetEnvironmentVar(TEXT("VK_APIDUMP_OUTPUT_FORMAT"), TEXT("text"));
			}
			else
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find Vulkan instance layer %s"), ANSI_TO_TCHAR(VkApiDumpName));
			}
		}
	}

	// At this point the CVar holds the final value
#if VULKAN_HAS_DEBUGGING_ENABLED
	const int32 VulkanValidationOption = GValidationCvar.GetValueOnAnyThread();
	if (!bGfxReconstructOrVkTrace && VulkanValidationOption > 0)
	{
		bool bSkipStandard = false;
		bool bStandardAvailable = false;
		if (GStandardValidationCvar.GetValueOnAnyThread() != 0)
		{
			if (GStandardValidationCvar.GetValueOnAnyThread() == 2)
			{
				bStandardAvailable = FindLayerInList(GlobalLayerExtensions, KHRONOS_STANDARD_VALIDATION_LAYER_NAME);
				if (bStandardAvailable)
				{
					OutInstanceLayers.Add(KHRONOS_STANDARD_VALIDATION_LAYER_NAME);
				}
				else
				{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
					//#todo-rco: We don't package DLLs so if this fails it means no DLL was found anywhere, so don't try to load standard validation layers
					UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find Vulkan instance validation layer %s;  Do you have the Vulkan SDK Installed?"), TEXT(STANDARD_VALIDATION_LAYER_NAME));
					bSkipStandard = true;
#else
					UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find Vulkan instance validation layer %s"), TEXT(STANDARD_VALIDATION_LAYER_NAME));
#endif
				}
			}
			else
			{
				bStandardAvailable = FindLayerInList(GlobalLayerExtensions, STANDARD_VALIDATION_LAYER_NAME);
				if (bStandardAvailable)
				{
					OutInstanceLayers.Add(STANDARD_VALIDATION_LAYER_NAME);
				}
				else
				{
					UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find Vulkan instance validation layer %s"), TEXT(STANDARD_VALIDATION_LAYER_NAME));
				}
			}
		}
	}
#endif

#if VULKAN_SUPPORTS_DEBUG_UTILS
	if (!bGfxReconstructOrVkTrace && VulkanValidationOption > 0)
	{
		const char* FoundDebugUtilsLayer = nullptr;
		bOutDebugUtils = FindLayerExtensionInList(GlobalLayerExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, FoundDebugUtilsLayer);
		if (bOutDebugUtils && *FoundDebugUtilsLayer)
		{
			OutInstanceLayers.Add(FoundDebugUtilsLayer);
		}
	}
#endif
#endif	// VULKAN_HAS_DEBUGGING_ENABLED

	// Check to see if the HMD requires any specific Vulkan extensions to operate
	if (IHeadMountedDisplayModule::IsAvailable())
	{
		HMDVulkanExtensions = IHeadMountedDisplayModule::Get().GetVulkanExtensions();
	
		if (HMDVulkanExtensions.IsValid())
		{
			if (!HMDVulkanExtensions->GetVulkanInstanceExtensionsRequired(OutInstanceExtensions))
			{
				UE_LOG(LogVulkanRHI, Warning, TEXT("Trying to use Vulkan with an HMD, but required extensions aren't supported!"));
			}
		}
	}

	// Check for layers added outside the RHI (eg plugins)
	for (const ANSICHAR* VulkanBridgeLayer : VulkanRHIBridge::InstanceLayers)
	{
		if (FindLayerInList(GlobalLayerExtensions, VulkanBridgeLayer))
		{
			OutInstanceLayers.Add(VulkanBridgeLayer);
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find VulkanRHIBridge instance layer '%s'"), ANSI_TO_TCHAR(VulkanBridgeLayer));
		}
	}

	TArray<const ANSICHAR*> PlatformExtensions;
	FVulkanPlatform::GetInstanceExtensions(PlatformExtensions);

	for (const ANSICHAR* PlatformExtension : PlatformExtensions)
	{
		if (FindLayerExtensionInList(GlobalLayerExtensions, PlatformExtension))
		{
			OutInstanceExtensions.Add(PlatformExtension);
		}
	}

	for (int32 j = 0; GInstanceExtensions[j] != nullptr; j++)
	{
		if (FindLayerExtensionInList(GlobalLayerExtensions, GInstanceExtensions[j]))
		{
			OutInstanceExtensions.Add(GInstanceExtensions[j]);
		}
	}

	// Check for extensions added outside the RHI (eg plugins)
	for (const ANSICHAR* VulkanBridgeExtension : VulkanRHIBridge::InstanceExtensions)
	{
		if (FindLayerExtensionInList(GlobalLayerExtensions, VulkanBridgeExtension))
		{
			OutInstanceExtensions.Add(VulkanBridgeExtension);
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find VulkanRHIBridge instance extension '%s'"), ANSI_TO_TCHAR(VulkanBridgeExtension));
		}
	}

#if VULKAN_SUPPORTS_DEBUG_UTILS
	if (!bGfxReconstructOrVkTrace && bOutDebugUtils && FindLayerExtensionInList(GlobalLayerExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
	{
		OutInstanceExtensions.Add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
#endif
#if VULKAN_HAS_DEBUGGING_ENABLED
	if (!bGfxReconstructOrVkTrace && !bOutDebugUtils && VulkanValidationOption > 0)
	{
		if (FindLayerExtensionInList(GlobalLayerExtensions, VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
		{
			OutInstanceExtensions.Add(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}
	}

	if (VulkanValidationOption > 0 && !bGfxReconstructOrVkTrace)
	{
#if VULKAN_HAS_VALIDATION_FEATURES
		if (FindLayerExtensionInList(GlobalLayerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) && GGPUValidationCvar.GetValueOnAnyThread() != 0)
		{
			OutInstanceExtensions.Add(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
		}
#endif
	}
#endif

	if (OutInstanceLayers.Num() > 0)
	{
		TrimDuplicates(OutInstanceLayers);
		UE_LOG(LogVulkanRHI, Display, TEXT("Using instance layers"));
		for (const ANSICHAR* Layer : OutInstanceLayers)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("* %s"), ANSI_TO_TCHAR(Layer));
		}
	}
	else
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("Not using instance layers"));
	}

	if (OutInstanceExtensions.Num() > 0)
	{
		TrimDuplicates(OutInstanceExtensions);
		UE_LOG(LogVulkanRHI, Display, TEXT("Using instance extensions"));
		for (const ANSICHAR* Extension : OutInstanceExtensions)
		{
			UE_LOG(LogVulkanRHI, Display, TEXT("* %s"), ANSI_TO_TCHAR(Extension));
		}
	}
	else
	{
		UE_LOG(LogVulkanRHI, Display, TEXT("Not using instance extensions"));
	}
}

void FVulkanDevice::GetDeviceExtensionsAndLayers(VkPhysicalDevice Gpu, EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutDeviceExtensions, TArray<const ANSICHAR*>& OutDeviceLayers, TArray<FString>& OutAllDeviceExtensions, TArray<FString>& OutAllDeviceLayers, bool& bOutDebugMarkers)
{
	bOutDebugMarkers = false;

	TArray<FLayerExtension> DeviceLayerExtensions;
	// 0 is reserved for regular device
	DeviceLayerExtensions.AddDefaulted();
	{
		uint32 Count = 0;
		TArray<VkLayerProperties> Properties;
		VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateDeviceLayerProperties(Gpu, &Count, nullptr));
		Properties.AddZeroed(Count);
		VERIFYVULKANRESULT_INIT(VulkanRHI::vkEnumerateDeviceLayerProperties(Gpu, &Count, Properties.GetData()));
		check(Count == Properties.Num());
		for (const VkLayerProperties& Property : Properties)
		{
			DeviceLayerExtensions[DeviceLayerExtensions.AddDefaulted()].LayerProps = Property;
		}
	}

	TArray<FString> FoundUniqueLayers;
	TArray<FString> FoundUniqueExtensions;

	for (int32 Index = 0; Index < DeviceLayerExtensions.Num(); ++Index)
	{
		if (Index == 0)
		{
			EnumerateDeviceExtensionProperties(Gpu, nullptr, DeviceLayerExtensions[Index]);
		}
		else
		{
			FoundUniqueLayers.AddUnique(ANSI_TO_TCHAR(DeviceLayerExtensions[Index].LayerProps.layerName));
			EnumerateDeviceExtensionProperties(Gpu, DeviceLayerExtensions[Index].LayerProps.layerName, DeviceLayerExtensions[Index]);
		}

		DeviceLayerExtensions[Index].AddUniqueExtensionNames(FoundUniqueExtensions);
	}

	FoundUniqueLayers.Sort();
	OutAllDeviceLayers = FoundUniqueLayers;

	FoundUniqueExtensions.Sort();
	OutAllDeviceExtensions = FoundUniqueExtensions;

	FVulkanPlatform::NotifyFoundDeviceLayersAndExtensions(Gpu, FoundUniqueLayers, FoundUniqueExtensions);

	TArray<FString> UniqueUsedDeviceExtensions;
	auto AddDeviceLayers = [&](const char* LayerName)
	{
		int32 LayerIndex = FindLayerIndexInList(DeviceLayerExtensions, LayerName);
		if (LayerIndex != INDEX_NONE)
		{
			DeviceLayerExtensions[LayerIndex].AddUniqueExtensionNames(UniqueUsedDeviceExtensions);
		}
	};

#if VULKAN_HAS_DEBUGGING_ENABLED
	GRenderDocFound = false;
	#if VULKAN_ENABLE_DRAW_MARKERS
	{
		int32 LayerIndex = FindLayerIndexInList(DeviceLayerExtensions, RENDERDOC_LAYER_NAME);
		if (LayerIndex != INDEX_NONE)
		{
			GRenderDocFound = true;
			DeviceLayerExtensions[LayerIndex].AddUniqueExtensionNames(UniqueUsedDeviceExtensions);
		}
	}
	#endif

	// Verify that all requested debugging device-layers are available. Skip validation layers under RenderDoc
	const int32 VulkanValidationOption = GValidationCvar.GetValueOnAnyThread();
	if (!GRenderDocFound && VulkanValidationOption > 0)
	{
		// Path for older drivers
		bool bStandardAvailable = false;
		if (GStandardValidationCvar.GetValueOnAnyThread() != 0)
		{
			bStandardAvailable = FindLayerInList(DeviceLayerExtensions, STANDARD_VALIDATION_LAYER_NAME);
			if (bStandardAvailable)
			{
				OutDeviceLayers.Add(STANDARD_VALIDATION_LAYER_NAME);
			}
		}
	}
#endif	// VULKAN_HAS_DEBUGGING_ENABLED

	// Check for layers added outside the RHI (eg plugins)
	for (const ANSICHAR* VulkanBridgeLayer : VulkanRHIBridge::DeviceLayers)
	{
		if (FindLayerInList(DeviceLayerExtensions, VulkanBridgeLayer))
		{
			OutDeviceLayers.Add(VulkanBridgeLayer);
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find VulkanRHIBridge device layer '%s'"), ANSI_TO_TCHAR(VulkanBridgeLayer));
		}
	}

	if (FVulkanDynamicRHI::HMDVulkanExtensions.IsValid())
	{
		if (!FVulkanDynamicRHI::HMDVulkanExtensions->GetVulkanDeviceExtensionsRequired( Gpu, OutDeviceExtensions))
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT( "Trying to use Vulkan with an HMD, but required extensions aren't supported on the selected device!"));
		}
	}

	// Now gather the actually used extensions based on the enabled layers
	TArray<const ANSICHAR*> AvailableExtensions;
	{
		// All global
		for (int32 ExtIndex = 0; ExtIndex < DeviceLayerExtensions[0].ExtensionProps.Num(); ++ExtIndex)
		{
			AvailableExtensions.Add(DeviceLayerExtensions[0].ExtensionProps[ExtIndex].extensionName);
		}

		// Now only find enabled layers
		for (int32 LayerIndex = 0; LayerIndex < OutDeviceLayers.Num(); ++LayerIndex)
		{
			// Skip 0 as it's the null layer
			int32 FindLayerIndex;
			for (FindLayerIndex = 1; FindLayerIndex < DeviceLayerExtensions.Num(); ++FindLayerIndex)
			{
				if (!FCStringAnsi::Strcmp(DeviceLayerExtensions[FindLayerIndex].LayerProps.layerName, OutDeviceLayers[LayerIndex]))
				{
					break;
				}
			}

			if (FindLayerIndex < DeviceLayerExtensions.Num())
			{
				DeviceLayerExtensions[FindLayerIndex].AddAnsiExtensionNames(AvailableExtensions);
			}
		}
	}
	TrimDuplicates(AvailableExtensions);

	auto ListContains = [](const TArray<const ANSICHAR*>& InList, const ANSICHAR* Name)
	{
		for (const ANSICHAR* Element : InList)
		{
			if (!FCStringAnsi::Strcmp(Element, Name))
			{
				return true;
			}
		}

		return false;
	};

	// Now go through the actual requested lists
	TArray<const ANSICHAR*> PlatformExtensions;
	FVulkanPlatform::GetDeviceExtensions(VendorId, PlatformExtensions);
	for (const ANSICHAR* PlatformExtension : PlatformExtensions)
	{
		if (ListContains(AvailableExtensions, PlatformExtension))
		{
			OutDeviceExtensions.Add(PlatformExtension);
		}
	}

	for (uint32 Index = 0; Index < UE_ARRAY_COUNT(GDeviceExtensions) && GDeviceExtensions[Index] != nullptr; ++Index)
	{
		if (ListContains(AvailableExtensions, GDeviceExtensions[Index]))
		{
			OutDeviceExtensions.Add(GDeviceExtensions[Index]);
		}
	}
	
	// Check for extensions added outside the RHI (eg plugins)
	for (const ANSICHAR* VulkanBridgeExtension : VulkanRHIBridge::DeviceExtensions)
	{
		if (ListContains(AvailableExtensions, VulkanBridgeExtension))
		{
			OutDeviceExtensions.Add(VulkanBridgeExtension);
		}
		else
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("Unable to find VulkanRHIBridge device extension '%s'"), ANSI_TO_TCHAR(VulkanBridgeExtension));
		}
	}


#if VULKAN_ENABLE_DRAW_MARKERS && VULKAN_HAS_DEBUGGING_ENABLED
	if (!bOutDebugMarkers &&
		(((GRenderDocFound || VulkanValidationOption == 0) && ListContains(AvailableExtensions, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) || FVulkanPlatform::ForceEnableDebugMarkers()))
	{
		// HACK: Lumin Nvidia driver unofficially supports this extension, but will return false if we try to load it explicitly.
#if !PLATFORM_LUMIN
		OutDeviceExtensions.Add(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
#endif
		bOutDebugMarkers = true;
	}
#endif

	if (OutDeviceExtensions.Num() > 0)
	{
		TrimDuplicates(OutDeviceExtensions);
	}

	if (OutDeviceLayers.Num() > 0)
	{
		TrimDuplicates(OutDeviceLayers);
	}
}

static inline bool HasExtension(const TArray<const ANSICHAR*> InExtensions, const ANSICHAR* InName)
{
	return InExtensions.ContainsByPredicate(
		[&InName](const ANSICHAR* Extension) -> bool
		{
			return FCStringAnsi::Strcmp(Extension, InName) == 0;
		}
	);
};

void FOptionalVulkanInstanceExtensions::Setup(const TArray<const ANSICHAR*>& InstanceExtensions)
{
	check(Packed == 0);

#if VULKAN_SUPPORTS_EXTERNAL_MEMORY
	HasKHRExternalMemoryCapabilities = HasExtension(InstanceExtensions, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2
	HasKHRGetPhysicalDeviceProperties2 = HasExtension(InstanceExtensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif
}

void FOptionalVulkanDeviceExtensions::Setup(const TArray<const ANSICHAR*>& DeviceExtensions)
{
	check(Packed == 0);

#if VULKAN_SUPPORTS_MAINTENANCE_LAYER1
	HasKHRMaintenance1 = HasExtension(DeviceExtensions, VK_KHR_MAINTENANCE1_EXTENSION_NAME);
#endif
#if VULKAN_SUPPORTS_MAINTENANCE_LAYER2
	HasKHRMaintenance2 = HasExtension(DeviceExtensions, VK_KHR_MAINTENANCE2_EXTENSION_NAME);
#endif
	//HasMirrorClampToEdge = HasExtension(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	HasKHRDedicatedAllocation = HasExtension(DeviceExtensions, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) && HasExtension(DeviceExtensions, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_VALIDATION_CACHE
	HasEXTValidationCache = HasExtension(DeviceExtensions, VK_EXT_VALIDATION_CACHE_EXTENSION_NAME);
#endif

	bool bHasAnyCrashExtension = false;
#if VULKAN_SUPPORTS_AMD_BUFFER_MARKER
	if (GGPUCrashDebuggingEnabled)
	{
		HasAMDBufferMarker = HasExtension(DeviceExtensions, VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
		bHasAnyCrashExtension = bHasAnyCrashExtension || HasAMDBufferMarker;
	}
#endif

#if VULKAN_SUPPORTS_NV_DIAGNOSTICS
	if (GGPUCrashDebuggingEnabled)
	{
		HasNVDiagnosticCheckpoints = HasExtension(DeviceExtensions, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
		HasNVDeviceDiagnosticConfig = HasExtension(DeviceExtensions, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
		bHasAnyCrashExtension = bHasAnyCrashExtension || (HasNVDeviceDiagnosticConfig && HasNVDiagnosticCheckpoints);
	}
#endif

	if (GGPUCrashDebuggingEnabled && !bHasAnyCrashExtension)
	{
		UE_LOG(LogVulkanRHI, Warning, TEXT("Tried to enable GPU crash debugging but no extension found! Will use local tracepoints."));
	}

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	HasYcbcrSampler = HasExtension(DeviceExtensions, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) && HasExtension(DeviceExtensions, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) && HasExtension(DeviceExtensions, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_MEMORY_PRIORITY
	HasMemoryPriority = HasExtension(DeviceExtensions, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
	if (FParse::Param(FCommandLine::Get(), TEXT("disablememorypriority")))
	{
		HasMemoryPriority = 0;
	}
#else
	HasMemoryPriority = 0;
#endif

#if VULKAN_SUPPORTS_MEMORY_BUDGET
	HasMemoryBudget = HasExtension(DeviceExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (FParse::Param(FCommandLine::Get(), TEXT("disablememorybudget")))
	{
		HasMemoryBudget = 0;
	}
#else
	HasMemoryBudget = 0;
#endif

#if VULKAN_SUPPORTS_ASTC_DECODE_MODE
	HasEXTASTCDecodeMode = HasExtension(DeviceExtensions, VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME);
#else
	HasEXTASTCDecodeMode = 0;
#endif

#if VULKAN_SUPPORTS_DRIVER_PROPERTIES
	HasDriverProperties = HasExtension(DeviceExtensions, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FRAGMENT_DENSITY_MAP
	HasEXTFragmentDensityMap = HasExtension(DeviceExtensions, VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FRAGMENT_DENSITY_MAP2
	HasEXTFragmentDensityMap2 = HasExtension(DeviceExtensions, VK_EXT_FRAGMENT_DENSITY_MAP_2_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FRAGMENT_SHADING_RATE
	// TODO: the VK_KHR_fragment_shading_rate extension is dependent on vkCreateRenderPass2, VkRenderPassCreateInfo2, VkAttachmentDescription2 and VkSubpassDescription2.
	// Disabling this path for now; adding this support in a later checkin.

	// HasKHRFragmentShadingRate = HasExtension(DeviceExtensions, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_MULTIVIEW
	HasKHRMultiview = HasExtension(DeviceExtensions, VK_KHR_MULTIVIEW_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE
	HasEXTFullscreenExclusive = HasExtension(DeviceExtensions, VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
#endif

	HasKHRImageFormatList = HasExtension(DeviceExtensions, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);

#if VULKAN_SUPPORTS_QCOM_RENDERPASS_TRANSFORM
	HasQcomRenderPassTransform = HasExtension(DeviceExtensions, VK_QCOM_RENDER_PASS_TRANSFORM_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_BUFFER_64BIT_ATOMICS
	HasAtomicInt64 = HasExtension(DeviceExtensions, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_SCALAR_BLOCK_LAYOUT
	HasScalarBlockLayoutFeatures = HasExtension(DeviceExtensions, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME);
#endif
}

void FVulkanDynamicRHI::SetupValidationRequests()
{
#if VULKAN_HAS_DEBUGGING_ENABLED
	int32 VulkanValidationOption = GValidationCvar.GetValueOnAnyThread();

	// Command line overrides Cvar
	if (FParse::Param(FCommandLine::Get(), TEXT("vulkandebug")))
	{
		// Match D3D and GL
		GValidationCvar->Set(2, ECVF_SetByCommandline);
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("vulkanvalidation="), VulkanValidationOption))
	{
		GValidationCvar->Set(VulkanValidationOption, ECVF_SetByCommandline);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("gpuvalidation")))
	{
		if (GValidationCvar->GetInt() < 2)
		{
			GValidationCvar->Set(2, ECVF_SetByCommandline);
		}
		GGPUValidationCvar->Set(2, ECVF_SetByCommandline);
	}
#endif
}
