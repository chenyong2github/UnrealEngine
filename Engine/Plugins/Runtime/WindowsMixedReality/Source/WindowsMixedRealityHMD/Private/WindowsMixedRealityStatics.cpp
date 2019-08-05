// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityStatics.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "WindowsMixedRealityHMD.h"

namespace WindowsMixedReality
{
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
	void FWindowsMixedRealityStatics::ConnectToRemoteHoloLens(FString remoteIP, unsigned int bitrate, bool isHoloLens1)
	{
#if !PLATFORM_HOLOLENS
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			hmd->ConnectToRemoteHoloLens(*remoteIP, bitrate, isHoloLens1);
		}
#endif
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
	bool FWindowsMixedRealityStatics::GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition)
	{
		FWindowsMixedRealityHMD* hmd = GetWindowsMixedRealityHMD();

		if (hmd != nullptr)
		{
			return hmd->GetHandJointOrientationAndPosition(hand, joint, OutOrientation, OutPosition);
		}

		return false;
	}
#endif
}
