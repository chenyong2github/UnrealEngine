// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusOpenXRHMD.h"
#include "OpenXRCore.h"
#include "OpenXRPlatformRHI.h"
#include "DefaultSpectatorScreenController.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_ANDROID
#include "Android/AndroidPlatformMisc.h"
#include <dlfcn.h> 
#endif //PLATFORM_ANDROID

DEFINE_LOG_CATEGORY(LogOculusOpenXRPlugin);

bool FOculusOpenXRHMD::GetCustomLoader(PFN_xrGetInstanceProcAddr* OutGetProcAddr)
{
#if PLATFORM_ANDROID
	// clear errors
	dlerror();

	LoaderHandle = FPlatformProcess::GetDllHandle(TEXT("libopenxr_loader.so"));
	if (LoaderHandle == nullptr)
	{
		UE_LOG(LogOculusOpenXRPlugin, Error, TEXT("Unable to load libopenxr_loader.so, error %s"), ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	// clear errors
	dlerror();

	PFN_xrGetInstanceProcAddr xrGetInstanceProcAddrPtr = (PFN_xrGetInstanceProcAddr)FPlatformProcess::GetDllExport(LoaderHandle, TEXT("xrGetInstanceProcAddr"));
	if (xrGetInstanceProcAddrPtr == nullptr)
	{
		UE_LOG(LogOculusOpenXRPlugin, Error, TEXT("Unable to load OpenXR xrGetInstanceProcAddr, error %s"), ANSI_TO_TCHAR(dlerror()));
		return false;
	}
	*OutGetProcAddr = xrGetInstanceProcAddrPtr;

	extern struct android_app* GNativeAndroidApp;
	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
	xrGetInstanceProcAddrPtr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
	if (xrInitializeLoaderKHR == nullptr)
	{
		UE_LOG(LogOculusOpenXRPlugin, Error, TEXT("Unable to load OpenXR xrInitializeLoaderKHR"));
		return false;
	}
	XrLoaderInitInfoAndroidKHR LoaderInitializeInfoAndroid;
	LoaderInitializeInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
	LoaderInitializeInfoAndroid.next = NULL;
	LoaderInitializeInfoAndroid.applicationVM = GNativeAndroidApp->activity->vm;
	LoaderInitializeInfoAndroid.applicationContext = GNativeAndroidApp->activity->clazz;
	XR_ENSURE(xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&LoaderInitializeInfoAndroid));

	UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Loaded Oculus OpenXR Loader"));
	return true;
#else //PLATFORM_ANDROID
	return false;
#endif //PLATFORM_ANDROID
}

bool FOculusOpenXRHMD::IsStandaloneStereoOnlyDevice()
{
#if PLATFORM_ANDROID
	return FAndroidMisc::GetDeviceMake() == FString("Oculus");
#else
	return false;
#endif
}

bool FOculusOpenXRHMD::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	return true;
}

bool FOculusOpenXRHMD::GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR GetInteractionProfile"));
	return true;
}

bool FOculusOpenXRHMD::GetSpectatorScreenController(FHeadMountedDisplayBase* InHMDBase, TUniquePtr<FDefaultSpectatorScreenController>& OutSpectatorScreenController)
{
#if PLATFORM_ANDROID
	OutSpectatorScreenController = nullptr;
	return true;
#else // PLATFORM_ANDROID
	OutSpectatorScreenController = MakeUnique<FDefaultSpectatorScreenController>(InHMDBase);
	return false;
#endif // PLATFORM_ANDROID
}

void FOculusOpenXRHMD::AddActions(XrInstance Instance, TFunction<XrAction(XrActionType InActionType, const FName& InName, const TArray<XrPath>& InSubactionPaths)> AddAction)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR AddActions"));
	return;
}

void FOculusOpenXRHMD::OnEvent(XrSession InSession, const XrEventDataBaseHeader* InHeader)
{
	return;
}

const void* FOculusOpenXRHMD::OnCreateInstance(class IOpenXRHMDPlugin* InPlugin, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnCreateInstance"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnGetSystem(XrInstance InInstance, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnGetSystem"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnCreateSession"));
#if PLATFORM_ANDROID
	if (GRHISupportsRHIThread && GIsThreadedRendering && GUseRHIThread_InternalUseOnly)
	{
		SetRHIThreadEnabled(false, false);
	}
#endif // PLATFORM_ANDROID
	return InNext;
}

const void* FOculusOpenXRHMD::OnBeginSession(XrSession InSession, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnBeginSession"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnBeginFrame(XrSession InSession, XrTime DisplayTime, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnBeginFrame"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnBeginProjectionView(XrSession InSession, int32 InLayerIndex, int32 InViewIndex, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnBeginProjectionView"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnBeginDepthInfo(XrSession InSession, int32 InLayerIndex, int32 InViewIndex, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnBeginDepthInfo"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnEndProjectionLayer(XrSession InSession, int32 InLayerIndex, const void* InNext, XrCompositionLayerFlags& OutFlags)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnEndProjectionLayer"));

	// XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT is required right now because the Oculus mobile runtime blends using alpha otherwise,
	// and we don't have proper inverse alpha support in OpenXR yet (once OpenXR supports inverse alpha, or we change the runtime behavior, remove this)
	OutFlags |= XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
	return InNext;
}

const void* FOculusOpenXRHMD::OnEndFrame(XrSession InSession, XrTime DisplayTime, const TArray<XrSwapchainSubImage> InColorImages, const TArray<XrSwapchainSubImage> InDepthImages, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnEndFrame"));
	return InNext;
}

const void* FOculusOpenXRHMD::OnSyncActions(XrSession InSession, const void* InNext)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR OnSyncActions"));
	return InNext;
}

void FOculusOpenXRHMD::PostSyncActions(XrSession InSession)
{
	//UE_LOG(LogOculusOpenXRPlugin, Log, TEXT("Oculus OpenXR PostSyncActions"));
	return;
}

IMPLEMENT_MODULE(FOculusOpenXRHMD, OculusOpenXRHMD)