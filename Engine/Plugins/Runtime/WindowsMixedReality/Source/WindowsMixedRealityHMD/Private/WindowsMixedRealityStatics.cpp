// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityStatics.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "WindowsMixedRealityHMD.h"

#if WITH_EDITOR
#include "WindowsMixedRealityRuntimeSettings.h"
#endif

namespace WindowsMixedReality
{
	//declare static delegate
	FWindowsMixedRealityStatics::FOnConfigureGesturesDelegate FWindowsMixedRealityStatics::OnConfigureGesturesDelegate;
	FDelegateHandle FWindowsMixedRealityStatics::ConfigureGesturesHandle;

	FWindowsMixedRealityStatics::FOnGetXRSystemFlagsDelegate FWindowsMixedRealityStatics::OnGetXRSystemFlagsDelegate;
	FDelegateHandle FWindowsMixedRealityStatics::GetXRSystemFlagsHandle;

	FWindowsMixedRealityStatics::FOnTogglePlayDelegate FWindowsMixedRealityStatics::OnTogglePlayDelegate;
	FDelegateHandle FWindowsMixedRealityStatics::TogglePlayDelegateHandle;

	FWindowsMixedRealityStatics::FOnGetHandJointTransformDelegate FWindowsMixedRealityStatics::OnGetHandJointTransformDelegate;
	FDelegateHandle FWindowsMixedRealityStatics::GetHandJointTransformDelegateHandle;

	FWindowsMixedRealityHMD* GetWindowsMixedRealityHMD() noexcept
	{
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == FName("WindowsMixedRealityHMD")))
		{
			return static_cast<FWindowsMixedRealityHMD*>(GEngine->XRSystem.Get());
		}

		return nullptr;
	}

	bool FWindowsMixedRealityStatics::SupportsSpatialInput()
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->SupportsSpatialInput();
		}

		return false;
	}

#if WITH_WINDOWS_MIXED_REALITY
	bool FWindowsMixedRealityStatics::SupportsHandTracking()
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->SupportsHandTracking();
		}

		return false;
	}

	bool FWindowsMixedRealityStatics::SupportsHandedness()
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->SupportsHandedness();
		}

		return false;
	}

	HMDTrackingStatus FWindowsMixedRealityStatics::GetControllerTrackingStatus(HMDHand hand)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetControllerTrackingStatus(hand);
		}

		return HMDTrackingStatus::NotTracked;
	}

	bool FWindowsMixedRealityStatics::GetControllerOrientationAndPosition(HMDHand hand, FRotator & OutOrientation, FVector & OutPosition)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetControllerOrientationAndPosition(hand, OutOrientation, OutPosition);
		}

		return false;
	}

	bool FWindowsMixedRealityStatics::PollInput()
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->PollInput();
		}

		return false;
	}

	bool FWindowsMixedRealityStatics::PollHandTracking()
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->PollHandTracking();
		}

		return false;
	}

	HMDInputPressState FWindowsMixedRealityStatics::GetPressState(
		HMDHand hand,
		HMDInputControllerButtons button)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetPressState(hand, button);
		}

		return HMDInputPressState::NotApplicable;
	}

	float FWindowsMixedRealityStatics::GetAxisPosition(HMDHand hand, HMDInputControllerAxes axis)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetAxisPosition(hand, axis);
		}

		return 0.0f;
	}

	void FWindowsMixedRealityStatics::SubmitHapticValue(HMDHand hand, float value)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			hmd->SubmitHapticValue(hand, value);
		}
	}
#endif

	// Remoting
	EXRDeviceConnectionResult::Type FWindowsMixedRealityStatics::ConnectToRemoteHoloLens(FString remoteIP, unsigned int bitrate, bool isHoloLens1)
	{
#if !PLATFORM_HOLOLENS
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();
		if (hmd != nullptr)
		{
			if (!remoteIP.Contains("."))
			{
				// If the given ip is not valid, try using it as a port for a listen connection to the remoting player.
				int port = FCString::Atoi(*remoteIP);
				remoteIP = TEXT("0.0.0.0");
				hmd->ConnectToRemoteHoloLens(*remoteIP, bitrate, isHoloLens1, port, true);
			}
			else
			{
				FString address, portStr;
				int port;
				if (remoteIP.Split(TEXT(":"), &address, &portStr))
				{
					port = FCString::Atoi(*portStr);
				}
				else
				{
					address = remoteIP;
					port = 8265;
				}
				hmd->ConnectToRemoteHoloLens(*address, bitrate, isHoloLens1, port);
			}
		}
		else
		{
			if (!GEngine->XRSystem.IsValid())
			{
#if WITH_EDITOR
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString(TEXT("Cannot Connect, see log for Errors.")), FLinearColor::Red);
#endif
				UE_LOG(LogWmrHmd, Error, TEXT("ConnectToRemoteHoloLens XRSystem is not valid. Perhaps it failed to start up?  Cannot Connect."));
			}
			else if (GEngine->XRSystem->GetSystemName() != FName("WindowsMixedRealityHMD"))
			{
#if WITH_EDITOR
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString(TEXT("Cannot Connect, see log for Errors.")), FLinearColor::Red);
#endif
				UE_LOG(LogWmrHmd, Error, TEXT("ConnectToRemoteHoloLens XRSystem SystemName is %s, not WindowsMixedRealityHMD.  Cannot Connect.  Perhaps you want to disable other XR plugins, adjust the priorities of XR plugins, deactivate other XR hardware, or run with -hmd=WindowsMixedRealityHMD in your editor commandline?"), *GEngine->XRSystem->GetSystemName().ToString());
			}
		}
#else
#if WITH_EDITOR
		UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString(TEXT("Cannot Connect, see log for Errors.")), FLinearColor::Red);
#endif
		UE_LOG(LogWmrHmd, Error, TEXT("FWindowsMixedRealityStatics::ConnectToRemoteHoloLens() is doing nothing because PLATFORM_HOLOLENS. (You don't 'remote' when running on device or emulated device.)"));
#endif
		return EXRDeviceConnectionResult::Success;
	}

	void FWindowsMixedRealityStatics::DisconnectFromRemoteHoloLens()
	{
#if !PLATFORM_HOLOLENS
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			hmd->DisconnectFromRemoteHoloLens();
		}
#endif
	}
#if WITH_WINDOWS_MIXED_REALITY
	bool FWindowsMixedRealityStatics::GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition, float& OutRadius)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetHandJointOrientationAndPosition(hand, joint, OutOrientation, OutPosition, OutRadius);
		}

		return false;
	}
#endif
}
