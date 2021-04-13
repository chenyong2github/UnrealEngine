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
#include "Android/AndroidPlatformFramePacer.h"
#include "Math/UnrealMathUtility.h"
#include "Android/AndroidPlatformMisc.h"

// From VulklanSwapChain.cpp
extern int32 GVulkanCPURenderThreadFramePacer;
extern int32 GPrintVulkanVsyncDebug;

int32 GVulkanExtensionFramePacer = 1;
static FAutoConsoleVariableRef CVarVulkanExtensionFramePacer(
	TEXT("r.Vulkan.ExtensionFramePacer"),
	GVulkanExtensionFramePacer,
	TEXT("Whether to enable the google extension Framepacer for Vulkan (when available on device)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVulkanSupportsTimestampQueries(
	TEXT("r.Vulkan.SupportsTimestampQueries"),
	0,
	TEXT("State of Vulkan timestamp queries support on an Android device\n")
	TEXT("  0 = unsupported\n")
	TEXT("  1 = supported."),
	ECVF_SetByDeviceProfile
);

// Vulkan function pointers
#define DEFINE_VK_ENTRYPOINTS(Type,Func) Type VulkanDynamicAPI::Func = NULL;
ENUM_VK_ENTRYPOINTS_ALL(DEFINE_VK_ENTRYPOINTS)

#define VULKAN_MALI_LAYER_NAME "VK_LAYER_ARM_AGA"

void* FVulkanAndroidPlatform::VulkanLib = nullptr;
bool FVulkanAndroidPlatform::bAttemptedLoad = false;

#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
bool FVulkanAndroidPlatform::bHasGoogleDisplayTiming = false;
TUniquePtr<class FGDTimingFramePacer> FVulkanAndroidPlatform::GDTimingFramePacer;
#endif

TUniquePtr<struct FAndroidVulkanFramePacer> FVulkanAndroidPlatform::FramePacer;
int32 FVulkanAndroidPlatform::CachedFramePace = 60;
int32 FVulkanAndroidPlatform::CachedRefreshRate = 60;
int32 FVulkanAndroidPlatform::CachedSyncInterval = 1;

bool FVulkanAndroidPlatform::bSupportsUniformBufferPatching = false;

#define CHECK_VK_ENTRYPOINTS(Type,Func) if (VulkanDynamicAPI::Func == NULL) { bFoundAllEntryPoints = false; UE_LOG(LogRHI, Warning, TEXT("Failed to find entry point for %s"), TEXT(#Func)); }


#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
FGDTimingFramePacer::FGDTimingFramePacer(VkDevice InDevice, VkSwapchainKHR InSwapChain)
	: Device(InDevice)
	, SwapChain(InSwapChain)
{
	FMemory::Memzero(PresentTime);

	ZeroVulkanStruct(PresentTimesInfo, VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE);
	PresentTimesInfo.swapchainCount = 1;
	PresentTimesInfo.pTimes = &PresentTime;
}

// Used as a safety measure to prevent scheduling too far ahead in case of an error
static constexpr uint64 GMaxAheadSchedulingTimeNanosec = 500000000llu; // 0.5 sec.

static uint64 TimeNanoseconds()
{
#if PLATFORM_ANDROID || PLATFORM_LINUX
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
#else
#error VK_GOOGLE_display_timing requires TimeNanoseconds() implementation for this platform
#endif
}

void FGDTimingFramePacer::ScheduleNextFrame(uint32 InPresentID, int32 InFramePace, int32 InRefreshRate)
{
	UpdateSyncDuration(InFramePace, InRefreshRate);
	if (SyncDuration == 0)
	{
		if (GPrintVulkanVsyncDebug != 0)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT(" -- SyncDuration == 0"));
		}
		return;
	}

	const uint64 CpuPresentTime = TimeNanoseconds();

	PresentTime.presentID = InPresentID; // Still need to pass ID for proper history values

	PollPastFrameInfo();
	if (!LastKnownFrameInfo.bValid)
	{
		if (GPrintVulkanVsyncDebug != 0)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT(" -- LastKnownFrameInfo not valid"));
		}
		return;
	}

	const uint64 CpuTargetPresentTimeMin = CalculateMinPresentTime(CpuPresentTime);
	const uint64 CpuTargetPresentTimeMax = CalculateMaxPresentTime(CpuPresentTime);
	const uint64 GpuTargetPresentTime = (PredictLastScheduledFramePresentTime(InPresentID) + SyncDuration);

	const uint64 TargetPresentTime = CalculateNearestVsTime(LastKnownFrameInfo.ActualPresentTime, FMath::Clamp(GpuTargetPresentTime, CpuTargetPresentTimeMin, CpuTargetPresentTimeMax));
	LastScheduledPresentTime = TargetPresentTime;

	PresentTime.desiredPresentTime = (TargetPresentTime - HalfRefreshDuration);

	if (GPrintVulkanVsyncDebug != 0)
	{
		double cpuPMin = CpuTargetPresentTimeMin / 1000000000.0;
		double cpuPMax = CpuTargetPresentTimeMax / 1000000000.0;
		double gpuP = GpuTargetPresentTime / 1000000000.0;
		double desP = PresentTime.desiredPresentTime / 1000000000.0;
		double lastP = LastKnownFrameInfo.ActualPresentTime / 1000000000.0;
		double cpuDelta = 0.0;
		double cpuNow = CpuPresentTime / 1000000000.0;
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT(" -- ID: %u, desired %.3f, pred-gpu %.3f, pred-cpu-min %.3f, pred-cpu-max %.3f, last: %.3f, cpu-gpu-delta: %.3f, now-cpu %.3f"), PresentTime.presentID, desP, gpuP, cpuPMin, cpuPMax, lastP, cpuDelta, cpuNow);
	}
}

void FGDTimingFramePacer::UpdateSyncDuration(int32 InFramePace, int32 InRefreshRate)
{
	if (FramePace == InFramePace)
	{
		return;
	}
	
	// It's possible we have requested a change in native refresh rate that has yet to take effect. However if we base the schedule for the next
	// frame on our intend native refresh rate, the exact number of vsyncs the extension has to wait is irrelevant and should never present earler
	// than intended.
	RefreshDuration = InRefreshRate > 0 ? FMath::DivideAndRoundNearest(1000000000ull, (uint64)InRefreshRate) : 0;
	ensure(RefreshDuration > 0);
	if (RefreshDuration == 0)
	{
		RefreshDuration = 16666667;
	}
	HalfRefreshDuration = (RefreshDuration / 2);


	FramePace = InFramePace;
	SyncDuration = InFramePace > 0 ? FMath::DivideAndRoundNearest(1000000000ull, (uint64)FramePace) : 0;

	if (SyncDuration > 0)
	{
		SyncDuration = (FMath::Max((SyncDuration + HalfRefreshDuration) / RefreshDuration, 1llu) * RefreshDuration);
	}
}

uint64 FGDTimingFramePacer::PredictLastScheduledFramePresentTime(uint32 CurrentPresentID) const
{
	const uint32 PredictFrameCount = (CurrentPresentID - LastKnownFrameInfo.PresentID - 1);
	// Use RefreshDuration for predicted frames and not SyncDuration for most optimistic prediction of future frames after last known (possible hitchy) frame.
	// Second parameter will be always >= than LastScheduledPresentTime if use SyncDuration.
	// It is possible that GPU will recover after hitch without any changes to a normal schedule but pessimistic planning will prevent this from happening.
	return FMath::Max(LastScheduledPresentTime, LastKnownFrameInfo.ActualPresentTime + (RefreshDuration * PredictFrameCount));
}

uint64 FGDTimingFramePacer::CalculateMinPresentTime(uint64 CpuPresentTime) const
{
	// Do not use delta on Android because already using CLOCK_MONOTONIC for CPU time which is also used in the extension.
	// Using delta will mostly work fine but there were problems in other projects. If GPU load changes quickly because
	// of the delta filter lag its value may be too high for current frame and cause pessimistic planning and stuttering.
	// Need additional time for testing to improve filtering.
	// Adding HalfRefreshDuration to produce round-up (ceil) in the final CalculateNearestVsTime()
	return (CpuPresentTime + HalfRefreshDuration);
}

uint64 FGDTimingFramePacer::CalculateMaxPresentTime(uint64 CpuPresentTime) const
{
	return (CpuPresentTime + GMaxAheadSchedulingTimeNanosec);
}

uint64 FGDTimingFramePacer::CalculateNearestVsTime(uint64 ActualPresentTime, uint64 TargetTime) const
{
	if (TargetTime > ActualPresentTime)
	{
		return (ActualPresentTime + ((TargetTime - ActualPresentTime) + HalfRefreshDuration) / RefreshDuration * RefreshDuration);
	}
	return ActualPresentTime;
}

void FGDTimingFramePacer::PollPastFrameInfo()
{
	for (;;)
	{
		// MUST call once with nullptr to get the count, or the API won't return any results at all.
		uint32 Count = 0;
		VkResult Result = VulkanDynamicAPI::vkGetPastPresentationTimingGOOGLE(Device, SwapChain, &Count, nullptr);
		checkf(Result == VK_SUCCESS, TEXT("vkGetPastPresentationTimingGOOGLE failed: %i"), Result);

		if (Count == 0)
		{
			break;
		}

		Count = 1;
		VkPastPresentationTimingGOOGLE PastPresentationTiming;
		Result = VulkanDynamicAPI::vkGetPastPresentationTimingGOOGLE(Device, SwapChain, &Count, &PastPresentationTiming);
		checkf(Result == VK_SUCCESS || Result == VK_INCOMPLETE, TEXT("vkGetPastPresentationTimingGOOGLE failed: %i"), Result);

		// If desiredPresentTime was too large for some reason driver may ignore this value to prevent long wait
		// Reset LastScheduledPresentTime in that case to be able to schedule on proper time
		if (PastPresentationTiming.actualPresentTime < PastPresentationTiming.desiredPresentTime)
		{
			UE_LOG(LogVulkanRHI, Warning, TEXT("PastPresentationTiming actualPresentTime is less than desiredPresentTime! Resetting LastScheduledPresentTime..."));
			LastScheduledPresentTime = 0;
		}

		LastKnownFrameInfo.PresentID = PastPresentationTiming.presentID;
		LastKnownFrameInfo.ActualPresentTime = PastPresentationTiming.actualPresentTime;
		LastKnownFrameInfo.bValid = true;
	}
}
#endif //VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING

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

	// Init frame pacer
	FramePacer = MakeUnique<FAndroidVulkanFramePacer>();
	FPlatformRHIFramePacer::Init(FramePacer.Get());

	FVulkanAndroidPlatform::bSupportsUniformBufferPatching = FAndroidMisc::GetDeviceMake() == FString("Oculus");

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

static int32 GVulkanQcomRenderPassTransform = 0;
static FAutoConsoleVariableRef CVarVulkanQcomRenderPassTransform(
	TEXT("r.Vulkan.UseQcomRenderPassTransform"),
	GVulkanQcomRenderPassTransform,
	TEXT("UseQcomRenderPassTransform\n"),
	ECVF_ReadOnly
);

void FVulkanAndroidPlatform::GetDeviceExtensions(EGpuVendorId VendorId, TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(VK_KHR_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
	OutExtensions.Add(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);

	OutExtensions.Add(VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME);

	if (GVulkanQcomRenderPassTransform)
	{
		OutExtensions.Add(VK_QCOM_RENDER_PASS_TRANSFORM_EXTENSION_NAME);
	}

#if VULKAN_SUPPORTS_FRAGMENT_DENSITY_MAP
	OutExtensions.Add(VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FRAGMENT_DENSITY_MAP2
	OutExtensions.Add(VK_EXT_FRAGMENT_DENSITY_MAP_2_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_MULTIVIEW
	OutExtensions.Add(VK_KHR_MULTIVIEW_EXTENSION_NAME);
#endif

#if VULKAN_SUPPORTS_FRAGMENT_SHADING_RATE
	OutExtensions.Add(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
#endif

#if !UE_BUILD_SHIPPING
	OutExtensions.Add(VULKAN_MALI_LAYER_NAME);
#endif
}

void FVulkanAndroidPlatform::NotifyFoundDeviceLayersAndExtensions(VkPhysicalDevice PhysicalDevice, const TArray<FString>& Layers, const TArray<FString>& Extensions)
{
#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
	FVulkanAndroidPlatform::bHasGoogleDisplayTiming = Extensions.Contains(TEXT(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME));
	UE_LOG(LogVulkanRHI, Log, TEXT("bHasGoogleDisplayTiming = %d"), FVulkanAndroidPlatform::bHasGoogleDisplayTiming);
#endif
}

bool FVulkanAndroidPlatform::SupportsTimestampRenderQueries()
{
	// standalone devices have newer drivers where timestamp render queries work.
	return (CVarVulkanSupportsTimestampQueries.GetValueOnAnyThread() == 1);
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

void FVulkanAndroidPlatform::SetupMaxRHIFeatureLevelAndShaderPlatform(ERHIFeatureLevel::Type InRequestedFeatureLevel)
{
	if (!GIsEditor &&
		(FVulkanPlatform::RequiresMobileRenderer() || 
		InRequestedFeatureLevel == ERHIFeatureLevel::ES3_1 ||
		FParse::Param(FCommandLine::Get(), TEXT("featureleveles31"))))
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;
		GMaxRHIShaderPlatform = SP_VULKAN_ES3_1_ANDROID;
	}
	else
	{
		GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;
		GMaxRHIShaderPlatform = SP_VULKAN_SM5_ANDROID;
	}
}

bool FVulkanAndroidPlatform::SupportsUniformBufferPatching()
{
	// Only Allow it on ( Oculus + Vulkan + Android ) devices for now to reduce the impact on general system
	// So far, the feature is designed on top of emulated UBs.
	return !UseRealUBsOptimization(true) && bSupportsUniformBufferPatching;
}

bool FVulkanAndroidPlatform::FramePace(FVulkanDevice& Device, VkSwapchainKHR Swapchain, uint32 PresentID, VkPresentInfoKHR& Info)
{
	bool bVsyncMultiple = true;
	int32 CurrentFramePace = FAndroidPlatformRHIFramePacer::GetFramePace();
	if (CurrentFramePace != 0)
	{
		int32 CurrentRefreshRate = FAndroidMisc::GetNativeDisplayRefreshRate();

		// cache refresh rate and sync interval
		if (CurrentFramePace != CachedFramePace || CurrentRefreshRate != CachedRefreshRate)
		{
			CachedFramePace = CurrentFramePace;
			FramePacer->SupportsFramePaceInternal(CurrentFramePace, CachedRefreshRate, CachedSyncInterval);
			FAndroidMisc::SetNativeDisplayRefreshRate(CachedRefreshRate);
		}

		if (CachedSyncInterval != 0)
		{
			// Multiple of sync interval, use directly
			bVsyncMultiple = true;
		}
		else
		{
			// Unsupported frame rate. Set to higher refresh rate and use CPU frame pacer to limit to desired frame pace
			// indicate that the RHI should perform CPU frame pacing to handle the requested frame rate
			bVsyncMultiple = false;
		}
	}

#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
	if (GVulkanExtensionFramePacer && bHasGoogleDisplayTiming)
	{
		check(GDTimingFramePacer);
		GDTimingFramePacer->ScheduleNextFrame(PresentID, CurrentFramePace, CachedRefreshRate);
		Info.pNext = GDTimingFramePacer->GetPresentTimesInfo();
	}
#endif
	return bVsyncMultiple;
}

VkResult FVulkanAndroidPlatform::CreateSwapchainKHR(VkDevice Device, const VkSwapchainCreateInfoKHR* CreateInfo, const VkAllocationCallbacks* Allocator, VkSwapchainKHR* Swapchain)
{
	VkResult Result = VulkanRHI::vkCreateSwapchainKHR(Device, CreateInfo, Allocator, Swapchain);

#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
	if (GVulkanExtensionFramePacer && FVulkanAndroidPlatform::bHasGoogleDisplayTiming)
	{
		GDTimingFramePacer = MakeUnique<FGDTimingFramePacer>(Device, *Swapchain);
		GVulkanCPURenderThreadFramePacer = 0;
	}
#endif
	return Result;
}

void FVulkanAndroidPlatform::DestroySwapchainKHR(VkDevice Device, VkSwapchainKHR Swapchain, const VkAllocationCallbacks* Allocator)
{
	VulkanRHI::vkDestroySwapchainKHR(Device, Swapchain, Allocator);
}


bool FAndroidVulkanFramePacer::SupportsFramePaceInternal(int32 QueryFramePace, int32& OutRefreshRate, int32& OutSyncInterval)
{
	TArray<int32> RefreshRates = FAndroidMisc::GetSupportedNativeDisplayRefreshRates();
	RefreshRates.Sort();

	FString DebugString = TEXT("Supported Refresh Rates:");
	for (int32 RefreshRate : RefreshRates)
	{
		DebugString += FString::Printf(TEXT(" %d"), RefreshRate);
	}
	UE_LOG(LogRHI, Log, TEXT("%s"), *DebugString);

	for (int32 Rate : RefreshRates)
	{
		if ((Rate % QueryFramePace) == 0)
		{
			UE_LOG(LogRHI, Log, TEXT("Supports %d using refresh rate %d and sync interval %d"), QueryFramePace, Rate, Rate / QueryFramePace);
			OutRefreshRate = Rate;
			OutSyncInterval = Rate / QueryFramePace;
			return true;
		}
	}

	// check if we want to use CPU frame pacing at less than a multiple of supported refresh rate
	if (FAndroidPlatformRHIFramePacer::CVarSupportNonVSyncMultipleFrameRates.GetValueOnAnyThread() == 1)
	{
		for (int32 Rate : RefreshRates)
		{
			if (Rate > QueryFramePace)
			{
				UE_LOG(LogRHI, Log, TEXT("Supports %d using refresh rate %d with CPU frame pacing"), QueryFramePace, Rate);
				OutRefreshRate = Rate;
				OutSyncInterval = 0;
				return true;
			}
		}
	}

	OutRefreshRate = QueryFramePace;
	OutSyncInterval = 0;
	return false;
}

bool FAndroidVulkanFramePacer::SupportsFramePace(int32 QueryFramePace)
{
	int32 TempRefreshRate, TempSyncInterval;
	return SupportsFramePaceInternal(QueryFramePace, TempRefreshRate, TempSyncInterval);
}

#endif
