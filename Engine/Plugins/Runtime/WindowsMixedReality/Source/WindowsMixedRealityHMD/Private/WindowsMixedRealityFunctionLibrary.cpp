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

void UWindowsMixedRealityFunctionLibrary::SetFocusPointForFrame(FVector position)
{
#if WITH_WINDOWS_MIXED_REALITY
	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		return;
	}

	hmd->SetFocusPointForFrame(position);
#endif
}

// Temporary CVAR api for FocusPoint
// The blueprint function to run a ConsoleCommand can be used to issue this each frame.
// The command string would be something like:
// vr.SetFocusPointForFrame 102.4 -850.3 21.7
static void SetFocusPointForFrame(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
{
	Ar.Logf(ELogVerbosity::Error, TEXT("SetFocusPointForFrame DEPRECATED: please switch to using the SetFocusPointForFrame blueprint function in this library instead.  This command will be removed in 4.25."));

	const int ArgsNum = Args.Num();
	if (ArgsNum != 3)
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("SetFocusPointForFrame command parameter expects 3 parameters not %i.  Ignoring command."), ArgsNum);
	}

	FVector Position;
	for (int i = 0; i < 3; ++i)
	{
		if (!FCString::IsNumeric(*Args[i]))
		{
			Ar.Logf(ELogVerbosity::Error, TEXT("SetFocusPointForFrame command parameter %i, '%s' is not numeric.  Ignoring command."), i, *Args[i]);
			return;
		}
		const float Value = FCString::Atof(*Args[i]);

		check(i >= 0 && i <= 2);
		Position[i] = Value;
	}


	WindowsMixedReality::FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
	if (hmd == nullptr)
	{
		Ar.Logf(ELogVerbosity::Error, TEXT("SetFocusPointForFrame command called but not WindowsMixedRealityHMD found.  Ignoring command."));
		return;
	}

	//Ar.Logf(TEXT("WindowsMixedReality SetFocusPointForFrame setting to %0.2f,%0.2f,%0.2f."), Position.X, Position.Y, Position.Z);
	hmd->SetFocusPointForFrame(Position);
}

#define LOCTEXT_NAMESPACE "WindowsMixedReality"
static FAutoConsoleCommand CSetFocusPointForFrameCmd(
	TEXT("vr.SetFocusPointForFrame"),
	*LOCTEXT("CVarText_SetFocusPointForFrame",
		"Set the reference point for the stabilization plane on hololens 2. You must set it each frame to activate the feature for that frame.  DEPRECATED: please switch to using the SetFocusPointForFrame blueprint function in this library instead.").ToString(),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(SetFocusPointForFrame));
#undef LOCTEXT_NAMESPACE