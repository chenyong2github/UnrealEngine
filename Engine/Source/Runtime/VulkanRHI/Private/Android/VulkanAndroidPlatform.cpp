// Copyright Epic Games, Inc. All Rights Reserved.

#include "VulkanAndroidPlatform.h"
#include "../VulkanRHIPrivate.h"
#include <dlfcn.h>
#include "Android/AndroidWindow.h"
#include "Android/AndroidPlatformFramePacer.h"
#include "Math/UnrealMathUtility.h"
#include "Android/AndroidPlatformMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "../VulkanExtensions.h"

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
int32 FVulkanAndroidPlatform::SuccessfulRefreshRateFrames = 1;
int32 FVulkanAndroidPlatform::UnsuccessfulRefreshRateFrames = 0;
TArray<TArray<ANSICHAR>> FVulkanAndroidPlatform::DebugVulkanDeviceLayers;
TArray<TArray<ANSICHAR>> FVulkanAndroidPlatform::DebugVulkanInstanceLayers;
int32 FVulkanAndroidPlatform::AFBCWorkaroundOption = 0;
int32 FVulkanAndroidPlatform::ASTCWorkaroundOption = 0;

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


void FVulkanAndroidPlatform::GetInstanceExtensions(FVulkanInstanceExtensionArray& OutExtensions)
{
	OutExtensions.Add(MakeUnique<FVulkanInstanceExtension>(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, VULKAN_EXTENSION_ENABLED, VULKAN_EXTENSION_NOT_PROMOTED));

	// VK_GOOGLE_display_timing (as instance extension?)
	OutExtensions.Add(MakeUnique<FVulkanInstanceExtension>(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, VULKAN_EXTENSION_ENABLED, VULKAN_EXTENSION_NOT_PROMOTED));
}

void FVulkanAndroidPlatform::GetInstanceLayers(TArray<const ANSICHAR*>& OutLayers)
{
#if !UE_BUILD_SHIPPING
	if (DebugVulkanInstanceLayers.IsEmpty())
	{
		TArray<FString> LayerNames;
		GConfig->GetArray(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("DebugVulkanInstanceLayers"), LayerNames, GEngineIni);

		if (!LayerNames.IsEmpty())
		{
			uint32 Index = 0;
			for (auto& LayerName : LayerNames)
			{
				TArray<ANSICHAR> LayerNameANSI{ TCHAR_TO_ANSI(*LayerName), LayerName.Len() + 1 };
				DebugVulkanInstanceLayers.Add(LayerNameANSI);
			}
		}
	}

	for (const TArray<ANSICHAR>& LayerName : DebugVulkanInstanceLayers)
	{
		OutLayers.Add(LayerName.GetData());
	}
#endif
}


static int32 GVulkanQcomRenderPassTransform = 0;
static FAutoConsoleVariableRef CVarVulkanQcomRenderPassTransform(
	TEXT("r.Vulkan.UseQcomRenderPassTransform"),
	GVulkanQcomRenderPassTransform,
	TEXT("UseQcomRenderPassTransform\n"),
	ECVF_ReadOnly
);

void FVulkanAndroidPlatform::GetDeviceExtensions(FVulkanDevice* Device, FVulkanDeviceExtensionArray& OutExtensions)
{
	OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, VULKAN_EXTENSION_ENABLED, VULKAN_EXTENSION_NOT_PROMOTED));
	OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, VULKAN_EXTENSION_ENABLED, VULKAN_EXTENSION_NOT_PROMOTED));
	OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME, VULKAN_SUPPORTS_ASTC_DECODE_MODE, VULKAN_EXTENSION_NOT_PROMOTED, DEVICE_EXT_FLAG_SETTER(HasEXTASTCDecodeMode)));
	//OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME, VULKAN_SUPPORTS_TEXTURE_COMPRESSION_ASTC_HDR, VK_API_VERSION_1_3, DEVICE_EXT_FLAG_SETTER(HasEXTTextureCompressionASTCHDR)));

	if (GVulkanQcomRenderPassTransform)
	{
		OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VK_QCOM_RENDER_PASS_TRANSFORM_EXTENSION_NAME, VULKAN_EXTENSION_ENABLED, VK_API_VERSION_1_3, DEVICE_EXT_FLAG_SETTER(HasQcomRenderPassTransform)));
	}

#if !UE_BUILD_SHIPPING
	// Layer name as extension
	OutExtensions.Add(MakeUnique<FVulkanDeviceExtension>(Device, VULKAN_MALI_LAYER_NAME, VULKAN_EXTENSION_ENABLED, VULKAN_EXTENSION_NOT_PROMOTED));
#endif
}

void FVulkanAndroidPlatform::GetDeviceLayers(TArray<const ANSICHAR*>& OutLayers)
{
#if !UE_BUILD_SHIPPING
	if (DebugVulkanDeviceLayers.IsEmpty())
	{
		TArray<FString> LayerNames;
		GConfig->GetArray(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("DebugVulkanDeviceLayers"), LayerNames, GEngineIni);

		if (!LayerNames.IsEmpty())
		{
			uint32 Index = 0;
			for (auto& LayerName : LayerNames)
			{
				TArray<ANSICHAR> LayerNameANSI{ TCHAR_TO_ANSI(*LayerName), LayerName.Len() + 1 };
				DebugVulkanDeviceLayers.Add(LayerNameANSI);
			}
		}
	}

	for (auto& LayerName : DebugVulkanDeviceLayers)
	{
		OutLayers.Add(LayerName.GetData());
	}
#endif
}

void FVulkanAndroidPlatform::NotifyFoundDeviceLayersAndExtensions(VkPhysicalDevice PhysicalDevice, const TArray<const ANSICHAR*>& Layers, const TArray<const ANSICHAR*>& Extensions)
{
#if VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
	FVulkanAndroidPlatform::bHasGoogleDisplayTiming = Extensions.ContainsByPredicate([](const ANSICHAR* Key)
		{
			return Key && !FCStringAnsi::Strcmp(Key, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
		});
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

bool FVulkanAndroidPlatform::FramePace(FVulkanDevice& Device, VkSwapchainKHR Swapchain, uint32 PresentID, VkPresentInfoKHR& Info)
{
	bool bVsyncMultiple = true;
	int32 CurrentFramePace = FAndroidPlatformRHIFramePacer::GetFramePace();
	if (CurrentFramePace != 0)
	{
		int32 CurrentRefreshRate = FAndroidMisc::GetNativeDisplayRefreshRate();

		bool bRefreshRateInvalid = (CurrentRefreshRate != CachedRefreshRate);
		bool bTryChangingRefreshRate = (bRefreshRateInvalid && (SuccessfulRefreshRateFrames > 0 || UnsuccessfulRefreshRateFrames > 1000));

		if (bRefreshRateInvalid)
		{
			SuccessfulRefreshRateFrames = 0;
			UnsuccessfulRefreshRateFrames++;
		}
		else
		{
			SuccessfulRefreshRateFrames++;
			UnsuccessfulRefreshRateFrames = 0;
		}

		// Cache refresh rate and sync interval.
		// Only try to change the refresh rate immediately if we're successfully running at the desired rate,
		// or periodically if not successfully running at the desired rate
		if (CurrentFramePace != CachedFramePace || bTryChangingRefreshRate)
		{
			CachedFramePace = CurrentFramePace;
			if (FramePacer->SupportsFramePaceInternal(CurrentFramePace, CachedRefreshRate, CachedSyncInterval))
			{
				FAndroidMisc::SetNativeDisplayRefreshRate(CachedRefreshRate);
			}
			else
			{
				// Desired frame pace not supported, save current refresh rate to prevent logspam.
				CachedRefreshRate = CurrentRefreshRate;
			}
			UnsuccessfulRefreshRateFrames = 0;
			SuccessfulRefreshRateFrames = 0;
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

//
// Test whether we should enable workarounds for textures
// Arm GPUs use an optimization "Arm FrameBuffer Compression - AFBC" that can significanly inflate (~5x) uncompressed texture memory requirements
// For now AFBC and similar optimizations can be disabled by using VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT or VK_IMAGE_USAGE_STORAGE_BIT flags on a texture
// On Adreno GPUs ASTC textures with optimial tiling may require 8x more memory
//
void FVulkanAndroidPlatform::SetupImageMemoryRequirementWorkaround(const FVulkanDevice& InDevice)
{
	AFBCWorkaroundOption = 0;
	ASTCWorkaroundOption = 0;

	VkImageCreateInfo ImageCreateInfo;
	ZeroVulkanStruct(ImageCreateInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	ImageCreateInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	ImageCreateInfo.arrayLayers = 1;
	ImageCreateInfo.extent = {128, 128, 1};
	ImageCreateInfo.mipLevels = 8;
	ImageCreateInfo.flags = 0;
	ImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ImageCreateInfo.queueFamilyIndexCount = 0;
	ImageCreateInfo.pQueueFamilyIndices = nullptr;
	ImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// AFBC workarounds
	{
		const VkFormatFeatureFlags FormatFlags = InDevice.GetFormatProperties()[VK_FORMAT_B8G8R8A8_UNORM].optimalTilingFeatures;

		VkImage Image0;
		VkMemoryRequirements Image0Mem;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &Image0));
		VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), Image0, &Image0Mem);
		VulkanRHI::vkDestroyImage(InDevice.GetInstanceHandle(), Image0, VULKAN_CPU_ALLOCATOR);

		VkImage ImageMutable;
		VkMemoryRequirements ImageMutableMem;
		ImageCreateInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &ImageMutable));
		VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), ImageMutable, &ImageMutableMem);
		VulkanRHI::vkDestroyImage(InDevice.GetInstanceHandle(), ImageMutable, VULKAN_CPU_ALLOCATOR);

		VkImage ImageStorage;
		VkMemoryRequirements ImageStorageMem;
		if ((FormatFlags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0)
		{
			ImageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
			ImageCreateInfo.flags = 0;
		}
		VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &ImageStorage));
		VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), ImageStorage, &ImageStorageMem);
		VulkanRHI::vkDestroyImage(InDevice.GetInstanceHandle(), ImageStorage, VULKAN_CPU_ALLOCATOR);

		const float MEM_SIZE_THRESHOLD = 1.5f;
		const float IMAGE0_SIZE = (float)Image0Mem.size;

		if (ImageMutableMem.size * MEM_SIZE_THRESHOLD < IMAGE0_SIZE)
		{
			AFBCWorkaroundOption = 1;
		}
		else if (ImageStorageMem.size * MEM_SIZE_THRESHOLD < IMAGE0_SIZE)
		{
			AFBCWorkaroundOption = 2;
		}

		if (AFBCWorkaroundOption != 0)
		{
			UE_LOG(LogRHI, Display, TEXT("Enabling workaround to reduce memory requrement for BGRA textures (%s flag). 128x128 - 8 Mips BGRA texture: %u KiB -> %u KiB"),
				AFBCWorkaroundOption == 1 ? TEXT("MUTABLE") : TEXT("STORAGE"),
				Image0Mem.size / 1024,
				AFBCWorkaroundOption == 1 ? ImageMutableMem.size / 1024 : ImageStorageMem.size / 1024
			);
		}
	}

	// ASTC workarounds
	{
		ImageCreateInfo.flags = 0;
		ImageCreateInfo.format = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		ImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImage ImageOptimal_ASTC;
		VkMemoryRequirements ImageOptimalMem_ASTC;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &ImageOptimal_ASTC));
		VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), ImageOptimal_ASTC, &ImageOptimalMem_ASTC);
		VulkanRHI::vkDestroyImage(InDevice.GetInstanceHandle(), ImageOptimal_ASTC, VULKAN_CPU_ALLOCATOR);

		ImageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;

		VkImage ImageLinear_ASTC;
		VkMemoryRequirements ImageLinearMem_ASTC;
		VERIFYVULKANRESULT(VulkanRHI::vkCreateImage(InDevice.GetInstanceHandle(), &ImageCreateInfo, VULKAN_CPU_ALLOCATOR, &ImageLinear_ASTC));
		VulkanRHI::vkGetImageMemoryRequirements(InDevice.GetInstanceHandle(), ImageLinear_ASTC, &ImageLinearMem_ASTC);
		VulkanRHI::vkDestroyImage(InDevice.GetInstanceHandle(), ImageLinear_ASTC, VULKAN_CPU_ALLOCATOR);


		const float MEM_SIZE_THRESHOLD = 2.0f;
		const float ImageOptimal_SIZE = (float)ImageOptimalMem_ASTC.size;

		if (ImageLinearMem_ASTC.size * MEM_SIZE_THRESHOLD <= ImageOptimal_SIZE)
		{
			ASTCWorkaroundOption = 1;

			UE_LOG(LogRHI, Display, TEXT("Enabling workaround to reduce memory requrement for ASTC textures (VK_IMAGE_TILING_LINEAR). 128x128 - 8 Mips ASTC_8x8 texture: %u KiB -> %u KiB"),
				ImageOptimalMem_ASTC.size / 1024,
				ImageLinearMem_ASTC.size / 1024
			);
		}
	}
}

void FVulkanAndroidPlatform::SetImageMemoryRequirementWorkaround(VkImageCreateInfo& ImageCreateInfo)
{
	if (AFBCWorkaroundOption != 0 &&
		ImageCreateInfo.imageType == VK_IMAGE_TYPE_2D && 
		ImageCreateInfo.format == VK_FORMAT_B8G8R8A8_UNORM && 
		ImageCreateInfo.mipLevels >= 8) // its worth enabling for 128x128 and up
	{
		if (AFBCWorkaroundOption == 1)
		{
			ImageCreateInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		}
		else if (AFBCWorkaroundOption == 2)
		{
			ImageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
	}

	// Use ASTC workaround for textures ASTC_6x6 and ASTC_8x8 with mips and size up to 128x128
	if (ASTCWorkaroundOption != 0 &&
		ImageCreateInfo.imageType == VK_IMAGE_TYPE_2D &&
		(ImageCreateInfo.format >= VK_FORMAT_ASTC_6x6_UNORM_BLOCK && ImageCreateInfo.format <= VK_FORMAT_ASTC_8x8_SRGB_BLOCK) &&
		(ImageCreateInfo.mipLevels > 1 && ImageCreateInfo.extent.width <= 128 && ImageCreateInfo.extent.height <= 128))
	{
		ImageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
	}
}
