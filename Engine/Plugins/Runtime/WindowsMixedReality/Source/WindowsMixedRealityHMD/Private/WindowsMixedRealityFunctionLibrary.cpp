// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityFunctionLibrary.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "WindowsMixedRealityHMD.h"

#include <functional>

UWindowsMixedRealityFunctionLibrary::UWindowsMixedRealityFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

WindowsMixedReality::FWindowsMixedRealityHMD* GetWindowsMixedRealityHMD() noexcept
{
	if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == FName("WindowsMixedRealityHMD")))
	{
		return static_cast<WindowsMixedReality::FWindowsMixedRealityHMD*>(GEngine->XRSystem.Get());
	}

	return nullptr;
}

FString UWindowsMixedRealityFunctionLibrary::GetVersionString()
{
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return FString();
	}

	return hmd->GetVersionString();
}

void UWindowsMixedRealityFunctionLibrary::ToggleImmersive(bool immersive)
{
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return;
	}

	hmd->EnableStereo(immersive);
}

bool UWindowsMixedRealityFunctionLibrary::IsCurrentlyImmersive()
{
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return false;
	}

	return hmd->IsCurrentlyImmersive();
}

bool UWindowsMixedRealityFunctionLibrary::IsDisplayOpaque()
{
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return true;
	}

	return hmd->IsDisplayOpaque();
}

void UWindowsMixedRealityFunctionLibrary::LockMouseToCenter(bool locked)
{
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return;
	}

	hmd->LockMouseToCenter(locked);
}

bool UWindowsMixedRealityFunctionLibrary::IsTrackingAvailable()
{
#if WITH_WINDOWS_MIXED_REALITY
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return false;
	}

	return hmd->IsTrackingAvailable();
#else
	return false;
#endif
}

FPointerPoseInfo UWindowsMixedRealityFunctionLibrary::GetPointerPoseInfo(EControllerHand hand)
{
	FPointerPoseInfo info;

#if WITH_WINDOWS_MIXED_REALITY

	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return info;
	}

	WindowsMixedReality::PointerPoseInfo p;
	hmd->GetPointerPose(hand, p);

	info.Origin = FVector(p.origin.x, p.origin.y, p.origin.z);
	info.Direction = FVector(p.direction.x, p.direction.y, p.direction.z);
	info.Up = FVector(p.up.x, p.up.y, p.up.z);
	info.Orientation = FQuat(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
#endif

	return info;
}