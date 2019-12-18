// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VulkanWindowsPlatform.h"
#include "../VulkanRHIPrivate.h"
#include "../VulkanDevice.h"

// Disable warning about forward declared enumeration without a type, since the D3D specific enums are not used in this translation unit
#pragma warning(push)
#pragma warning(disable : 4471)
#include "amd_ags.h"
#pragma warning(pop)

#include "Windows/AllowWindowsPlatformTypes.h"
static HMODULE GVulkanDLLModule = nullptr;

static PFN_vkGetInstanceProcAddr GGetInstanceProcAddr = nullptr;

// Vulkan function pointers
#define DEFINE_VK_ENTRYPOINTS(Type,Func) VULKANRHI_API Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_ALL(DEFINE_VK_ENTRYPOINTS)

#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

#pragma warning(push)
#pragma warning(disable : 4191) // warning C4191: 'type cast': unsafe conversion
bool FVulkanWindowsPlatform::LoadVulkanLibrary()
{
#if VULKAN_HAS_DEBUGGING_ENABLED
	if (GValidationCvar->GetInt() > 0)
	{
		const bool bUseSDK = FParse::Param(FCommandLine::Get(), TEXT("vulkansdk"));
		if (!bUseSDK)
		{
			const FString PreviousEnvVar = FPlatformMisc::GetEnvironmentVariable(TEXT("VK_LAYER_PATH"));
			if (PreviousEnvVar.IsEmpty())
			{
				// Change behavior of loading Vulkan layers by setting environment variable "VK_LAYER_PATH" to UE4 specific directory
				FString VulkanLayerPath = FPaths::EngineDir();
#if PLATFORM_64BITS
				VulkanLayerPath.Append(TEXT("Binaries/ThirdParty/Windows/Vulkan/Win64"));
#else
				VulkanLayerPath.Append(TEXT("Binaries/ThirdParty/Windows/Vulkan/Win32"));
#endif
				FPlatformMisc::SetEnvironmentVar(TEXT("VK_LAYER_PATH"), *VulkanLayerPath);
			}
		}
	}
#endif // VULKAN_HAS_DEBUGGING_ENABLED

	// Try to load the vulkan dll, as not everyone has the sdk installed
	GVulkanDLLModule = ::LoadLibraryW(TEXT("vulkan-1.dll"));

	if (GVulkanDLLModule)
	{
#define GET_VK_ENTRYPOINTS(Type,Func) VulkanDynamicAPI::Func = (Type)FPlatformProcess::GetDllExport(GVulkanDLLModule, L#Func);
		ENUM_VK_ENTRYPOINTS_BASE(GET_VK_ENTRYPOINTS);

		bool bFoundAllEntryPoints = true;
		ENUM_VK_ENTRYPOINTS_BASE(CHECK_VK_ENTRYPOINTS);
		if (!bFoundAllEntryPoints)
		{
			FreeVulkanLibrary();
			return false;
		}

		ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(GET_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
		ENUM_VK_ENTRYPOINTS_OPTIONAL_BASE(CHECK_VK_ENTRYPOINTS);
#endif

		ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(GET_VK_ENTRYPOINTS);
		ENUM_VK_ENTRYPOINTS_PLATFORM_BASE(CHECK_VK_ENTRYPOINTS);

#undef GET_VK_ENTRYPOINTS

		return true;
	}

	return false;
}

bool FVulkanWindowsPlatform::LoadVulkanInstanceFunctions(VkInstance inInstance)
{
	if (!GVulkanDLLModule)
	{
		return false;
	}

	GGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)FPlatformProcess::GetDllExport(GVulkanDLLModule, TEXT("vkGetInstanceProcAddr"));

	if (!GGetInstanceProcAddr)
	{
		return false;
	}

	bool bFoundAllEntryPoints = true;
#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }

	// Initialize all of the entry points we have to query manually
#define GETINSTANCE_VK_ENTRYPOINTS(Type, Func) VulkanDynamicAPI::Func = (Type)VulkanDynamicAPI::vkGetInstanceProcAddr(inInstance, #Func);
	ENUM_VK_ENTRYPOINTS_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_INSTANCE(CHECK_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_SURFACE_INSTANCE(CHECK_VK_ENTRYPOINTS);
	if (!bFoundAllEntryPoints)
	{
		FreeVulkanLibrary();
		return false;
	}

	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
#if UE_BUILD_DEBUG
	ENUM_VK_ENTRYPOINTS_OPTIONAL_INSTANCE(CHECK_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_OPTIONAL_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);
#endif

	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(GETINSTANCE_VK_ENTRYPOINTS);
	ENUM_VK_ENTRYPOINTS_PLATFORM_INSTANCE(CHECK_VK_ENTRYPOINTS);
#undef GET_VK_ENTRYPOINTS

	return true;
}
#pragma warning(pop) // restore 4191

void FVulkanWindowsPlatform::FreeVulkanLibrary()
{
	if (GVulkanDLLModule != nullptr)
	{
		::FreeLibrary(GVulkanDLLModule);
		GVulkanDLLModule = nullptr;
	}
}

#include "Windows/HideWindowsPlatformTypes.h"



void FVulkanWindowsPlatform::GetInstanceExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	// windows surface extension
	OutExtensions.Add(VK_KHR_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

#if VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE
	// Required by Fullscreen
	OutExtensions.Add(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
#endif
}


void FVulkanWindowsPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	const bool bAllowVendorDevice = !FParse::Param(FCommandLine::Get(), TEXT("novendordevice"));

#if VULKAN_SUPPORTS_DRIVER_PROPERTIES
	OutExtensions.Add(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	OutExtensions.Add(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
#endif
	if (GGPUCrashDebuggingEnabled)
	{
#if VULKAN_SUPPORTS_AMD_BUFFER_MARKER
		if (VendorId == EGpuVendorId::Amd && bAllowVendorDevice)
		{
			OutExtensions.Add(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
		}
#endif
#if VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT
		if (VendorId == EGpuVendorId::Nvidia && bAllowVendorDevice)
		{
			OutExtensions.Add(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
		}
#endif
	}

#if VULKAN_SUPPORTS_COLOR_CONVERSIONS
	// YCbCr requires BindMem2 and GetMemReqs2
	OutExtensions.Add(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE
	// Fullscreen requires Instance capabilities2
	OutExtensions.Add(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
#endif
}

void FVulkanWindowsPlatform::CreateSurface(void* WindowHandle, VkInstance Instance, VkSurfaceKHR* OutSurface)
{
	VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo;
	ZeroVulkanStruct(SurfaceCreateInfo, VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
	SurfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
	SurfaceCreateInfo.hwnd = (HWND)WindowHandle;
	VERIFYVULKANRESULT(VulkanDynamicAPI::vkCreateWin32SurfaceKHR(Instance, &SurfaceCreateInfo, VULKAN_CPU_ALLOCATOR, OutSurface));
}

bool FVulkanWindowsPlatform::SupportsDeviceLocalHostVisibleWithNoPenalty(EGpuVendorId VendorId)
{
	static bool bIsWin10 = FWindowsPlatformMisc::VerifyWindowsVersion(10, 0) /*Win10*/;
	return (VendorId == EGpuVendorId::Amd && bIsWin10);
}


void FVulkanWindowsPlatform::WriteCrashMarker(const FOptionalVulkanDeviceExtensions& OptionalExtensions, VkCommandBuffer CmdBuffer, VkBuffer DestBuffer, const TArrayView<uint32>& Entries, bool bAdding)
{
	ensure(Entries.Num() <= GMaxCrashBufferEntries);

	if (OptionalExtensions.HasAMDBufferMarker)
	{
		// AMD API only allows updating one entry at a time. Assume buffer has entry 0 as num entries
		VulkanDynamicAPI::vkCmdWriteBufferMarkerAMD(CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, DestBuffer, 0, Entries.Num());
		if (bAdding)
		{
			int32 LastIndex = Entries.Num() - 1;
			// +1 size as entries start at index 1
			VulkanDynamicAPI::vkCmdWriteBufferMarkerAMD(CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, DestBuffer, (1 + LastIndex) * sizeof(uint32), Entries[LastIndex]);
		}
	}
	else if (OptionalExtensions.HasNVDiagnosticCheckpoints)
	{
		if (bAdding)
		{
			int32 LastIndex = Entries.Num() - 1;
			uint32 Value = Entries[LastIndex];
			VulkanDynamicAPI::vkCmdSetCheckpointNV(CmdBuffer, (void*)(size_t)Value);
		}
	}
}

void FVulkanWindowsPlatform::CheckDeviceDriver(uint32 DeviceIndex, EGpuVendorId VendorId, const VkPhysicalDeviceProperties& Props)
{
	const bool bAllowVendorDevice = !FParse::Param(FCommandLine::Get(), TEXT("novendordevice"));
	if (VendorId == EGpuVendorId::Amd && bAllowVendorDevice)
	{
		AGSGPUInfo AmdGpuInfo;
		AGSContext* AmdAgsContext = nullptr;
		if (agsInit(&AmdAgsContext, nullptr, &AmdGpuInfo) == AGS_SUCCESS)
		{
			const char* Version = AmdGpuInfo.radeonSoftwareVersion;
			if (DeviceIndex < (uint32)AmdGpuInfo.numDevices && Version && *Version)
			{
				auto& DeviceInfo = AmdGpuInfo.devices[DeviceIndex];
				bool bIsPreGCN = DeviceInfo.asicFamily == AGSDeviceInfo::AsicFamily_PreGCN;
				if (DeviceInfo.asicFamily != AGSDeviceInfo::AsicFamily_Unknown)
				{
					// "Major.Minor.Revision"
					do
					{
						int32 MajorVersion = FCStringAnsi::Atoi(Version);
						while (*Version >= '0' && *Version <= '9')
						{
							++Version;
						}

						if (*Version != '.')
						{
							break;
						}
						++Version;

						int32 MinorVersion = FCStringAnsi::Atoi(Version);
						while (*Version >= '0' && *Version <= '9')
						{
							++Version;
						}

						if (*Version != '.')
						{
							break;
						}
						++Version;

						int32 RevisionVersion = FCStringAnsi::Atoi(Version);

						if (MajorVersion > 0)
						{
							if (MajorVersion < 18)
							{
								// Blacklist drivers older than 18.xx.xx drivers
								FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("There are known issues with older Vulkan Radeon drivers; the recommended version is 19.4.1; please try updating your driver to that version."), TEXT("Vulkan driver version"));
								FPlatformMisc::RequestExitWithStatus(true, 1);
							}
							else if (WITH_EDITOR)
							{
								bool bBadVersion = false;
								if (MajorVersion == 19)
								{
									if (MinorVersion < 4 || (MinorVersion == 4 && RevisionVersion < 1))
									{
										bBadVersion = true;
									}
								}
								else if (MajorVersion == 18)
								{
									if (MinorVersion > 12 || (MinorVersion == 12 && RevisionVersion >= 2))
									{
										bBadVersion = true;
									}
								}

								if (bBadVersion)
								{
									// Blacklist drivers between 18.12.2 and 19.2.1, as they as it introduced an issue with Slate windows/Vulkan viewports on the editor; 19.3.x also have crashes
									FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("There are known issues with Vulkan on the editor with the some \nRadeon drivers; the recommended version is 19.4.1: please try updating your driver to that version."), TEXT("Vulkan driver version"));
									FPlatformMisc::RequestExitWithStatus(true, 1);
								}
							}
						}
					}
					while (0);

					GRHIDeviceIsAMDPreGCNArchitecture = GRHIDeviceIsAMDPreGCNArchitecture || bIsPreGCN;
					if (GRHIDeviceIsAMDPreGCNArchitecture)
					{
						UE_LOG(LogVulkanRHI, Log, TEXT("AMD Pre GCN architecture detected, some driver workarounds will be in place"));
					}
					UE_LOG(LogVulkanRHI, Display, TEXT("AMD User Driver Version = %s"), ANSI_TO_TCHAR(AmdGpuInfo.radeonSoftwareVersion));
				}
			}

			agsDeInit(AmdAgsContext);
		}
	}
	else if (VendorId == EGpuVendorId::Nvidia)
	{
		if (GRHIAdapterName.Contains(TEXT("RTX 20")))
		{
			UNvidiaDriverVersion NvidiaVersion;
			static_assert(sizeof(NvidiaVersion) == sizeof(Props.driverVersion), "Mismatched Nvidia pack driver version!");
			NvidiaVersion.Packed = Props.driverVersion;

			if (NvidiaVersion.Major < 430)
			{
				// Workaround a crash on 20xx family
				UE_LOG(LogVulkanRHI, Warning, TEXT("Nvidia 20xx family of GPUs have a known crash on drivers < 430. Compatibility mode (slow!) will now be enabled"));

				extern TAutoConsoleVariable<int32> GRHIThreadCvar;
				GRHIThreadCvar->SetWithCurrentPriority(0);
				IConsoleVariable* BypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RHICmdBypass"));
				BypassVar->SetWithCurrentPriority(1);
			}
		}
	}
}
