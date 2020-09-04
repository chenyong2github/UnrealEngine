// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppFramework.h"
#include "MagicLeapHMD.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "RenderingThread.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "RenderingThread.h"

#include "Lumin/CAPIShims/LuminAPISnapshot.h"

#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformDelegates.h"
#endif // PLATFORM_LUMIN

TArray<MagicLeap::IAppEventHandler*> FAppFramework::EventHandlers;
FCriticalSection FAppFramework::EventHandlersCriticalSection;

FAppFramework::FAppFramework()
{}

FAppFramework::~FAppFramework()
{
	Shutdown();
}

void FAppFramework::Startup()
{
	// Register application lifecycle delegates
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FAppFramework::ApplicationPauseDelegate);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FAppFramework::ApplicationResumeDelegate);

#if PLATFORM_LUMIN
	FLuminDelegates::DeviceHasReactivatedDelegate.AddRaw(this, &FAppFramework::OnDeviceActive);
	FLuminDelegates::DeviceWillEnterRealityModeDelegate.AddRaw(this, &FAppFramework::OnDeviceRealityMode);
	FLuminDelegates::DeviceWillGoInStandbyDelegate.AddRaw(this, &FAppFramework::OnDeviceStandby);
	FCoreDelegates::VRHeadsetLost.AddRaw(this, &FAppFramework::OnDeviceHeadposeLost);
	FCoreDelegates::VRHeadsetReconnected.AddRaw(this, &FAppFramework::OnDeviceActive);
#endif // PLATFORM_LUMIN

	bInitialized = true;

	saved_max_fps_ = 0.0f;
}

void FAppFramework::Shutdown()
{
	bInitialized = false;

	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);

#if PLATFORM_LUMIN
	FLuminDelegates::DeviceHasReactivatedDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillEnterRealityModeDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillGoInStandbyDelegate.RemoveAll(this);
#endif // PLATFORM_LUMIN
}

void FAppFramework::BeginUpdate()
{
#if WITH_MLSDK
	if (bInitialized)
	{
		FScopeLock Lock(&EventHandlersCriticalSection);
		for (auto EventHandler : EventHandlers)
		{
			EventHandler->OnAppTick();
		}
	}
#endif //WITH_MLSDK
}

void FAppFramework::ApplicationPauseDelegate()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework APP PAUSE ++++++"));

	if (GEngine)
	{
		saved_max_fps_ = GEngine->GetMaxFPS();
		// MaxFPS = 0 means uncapped. So we set it to something trivial like 10 to keep network connections alive.
		GEngine->SetMaxFPS(10.0f);

		APlayerController* PlayerController = GEngine->GetFirstLocalPlayerController(GWorld);
		if (PlayerController != nullptr)
		{
			PlayerController->SetPause(true);
		}
	}

	FScopeLock Lock(&EventHandlersCriticalSection);
	for (auto EventHandler : EventHandlers)
	{
		EventHandler->OnAppPause();
	}

	// Pause rendering
	PauseRendering(true);
}

void FAppFramework::ApplicationResumeDelegate()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework APP RESUME ++++++"));

	// Resume rendering
	PauseRendering(false);

	if (GEngine)
	{
		APlayerController* PlayerController = GEngine->GetFirstLocalPlayerController(GWorld);
		if (PlayerController != nullptr)
		{
			PlayerController->SetPause(false);
		}

		GEngine->SetMaxFPS(saved_max_fps_);
	}

	FScopeLock Lock(&EventHandlersCriticalSection);
	for (auto EventHandler : EventHandlers)
	{
		EventHandler->OnAppResume();
	}
}

void FAppFramework::OnApplicationStart()
{
	FScopeLock Lock(&EventHandlersCriticalSection);
	for (auto EventHandler : EventHandlers)
	{
		EventHandler->OnAppStart();
	}
}

void FAppFramework::OnApplicationShutdown()
{
	FScopeLock Lock(&EventHandlersCriticalSection);
	for (auto EventHandler : EventHandlers)
	{
		EventHandler->OnAppShutDown();
	}
}

void FAppFramework::OnDeviceActive()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework DEVICE ACTIVE ++++++"));
	PauseRendering(false);
}

void FAppFramework::OnDeviceRealityMode()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework DEVICE REALITY MODE ++++++"));
	PauseRendering(true);
}

void FAppFramework::OnDeviceStandby()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework DEVICE STANDBY ++++++"));
	PauseRendering(true);
}

void FAppFramework::OnDeviceHeadposeLost()
{
	UE_LOG(LogMagicLeap, Log, TEXT("+++++++ ML AppFramework DEVICE HEADPOSE LOST ++++++"));
	PauseRendering(true);
}

void FAppFramework::PauseRendering(bool bPause)
{
	FMagicLeapHMD * const HMD = GEngine ? (GEngine->XRSystem ? static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice()) : nullptr) : nullptr;
	if (HMD)
	{
		HMD->PauseRendering(bPause);
	}
}

const FTrackingFrame* FAppFramework::GetCurrentFrame() const
{
	FMagicLeapHMD * const hmd = GEngine ? (GEngine->XRSystem ? static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice()) : nullptr) : nullptr;
	return hmd ? &(hmd->GetCurrentFrame()) : nullptr;
}

const FTrackingFrame* FAppFramework::GetOldFrame() const
{
	FMagicLeapHMD * const hmd = GEngine ? (GEngine->XRSystem ? static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice()) : nullptr) : nullptr;
	return hmd ? &(hmd->GetOldFrame()) : nullptr;
}

uint32 FAppFramework::GetViewportCount() const
{
#if WITH_MLSDK
	const FTrackingFrame *frame = GetOldFrame();
	return frame ? frame->FrameInfo.num_virtual_cameras : 2;
#else
	return 1;
#endif //WITH_MLSDK
}

float FAppFramework::GetWorldToMetersScale() const
{
	const FTrackingFrame *frame = GetCurrentFrame();

	// If the frame is not ready, return the default system scale.
	if (!frame)
	{
		return GWorld ? GWorld->GetWorldSettings()->WorldToMeters : 100.0f;
	}

	return frame->WorldToMetersScale;
}

FTransform FAppFramework::GetCurrentFrameUpdatePose() const
{
	const FTrackingFrame* frame = GetCurrentFrame();
	return frame ? frame->RawPose : FTransform::Identity;
}

#if WITH_MLSDK
bool FAppFramework::GetTransform(const MLCoordinateFrameUID& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const
{
	const FTrackingFrame* frame = GetCurrentFrame();
	if (frame == nullptr)
	{
		OutReason = EMagicLeapTransformFailReason::InvalidTrackingFrame;
		return false;
	}

	MLTransform transform = MagicLeap::kIdentityTransform;
	MLResult Result = MLSnapshotGetTransform(frame->Snapshot, &Id, &transform);
	if (Result == MLResult_Ok)
	{
		OutTransform = MagicLeap::ToFTransform(transform, GetWorldToMetersScale());
		if (OutTransform.ContainsNaN())
		{
			OutReason = EMagicLeapTransformFailReason::NaNsInTransform;
			return false;
		}
		// Unreal crashes if the incoming quaternion is not normalized.
		if (!OutTransform.GetRotation().IsNormalized())
		{
			FQuat rotation = OutTransform.GetRotation();
			rotation.Normalize();
			OutTransform.SetRotation(rotation);
		}
		OutReason = EMagicLeapTransformFailReason::None;
		return true;
	}
	else if (Result == MLSnapshotResult_PoseNotFound)
	{
		OutReason = EMagicLeapTransformFailReason::PoseNotFound;
	}
	else
	{
		OutReason = EMagicLeapTransformFailReason::CallFailed;
	}

	return false;
}
#endif //WITH_MLSDK

void FAppFramework::AddEventHandler(MagicLeap::IAppEventHandler* EventHandler)
{
	FScopeLock Lock(&EventHandlersCriticalSection);
	EventHandlers.Add(EventHandler);
}

void FAppFramework::RemoveEventHandler(MagicLeap::IAppEventHandler* EventHandler)
{
	FScopeLock Lock(&EventHandlersCriticalSection);
	EventHandlers.Remove(EventHandler);
}
