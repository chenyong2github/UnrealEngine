// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"
#include "HeadMountedDisplayTypes.h"
#include "UObject/ObjectMacros.h"
#include "XRGestureConfig.h"


#pragma warning(disable:4668)  
#include <DirectXMath.h>
#pragma warning(default:4668)

#if WITH_WINDOWS_MIXED_REALITY
#include "MixedRealityInterop.h"
#endif
#include "WindowsMixedRealityInteropUtility.h"


namespace WindowsMixedReality
{
	class WINDOWSMIXEDREALITYHMD_API FWindowsMixedRealityStatics
	{
	public:
		static bool SupportsSpatialInput();

#if WITH_WINDOWS_MIXED_REALITY
		static bool SupportsHandTracking();
		static bool SupportsHandedness();

		static HMDTrackingStatus GetControllerTrackingStatus(HMDHand hand);

		static bool GetControllerOrientationAndPosition(HMDHand hand, FRotator& OutOrientation, FVector& OutPosition);

		static bool GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition, float& OutRadius);

		static bool PollInput();
		static bool PollHandTracking();

		static HMDInputPressState GetPressState(
			HMDHand hand,
			HMDInputControllerButtons button);

		static float GetAxisPosition(
			HMDHand hand,
			HMDInputControllerAxes axis);

		static void SubmitHapticValue(
			HMDHand hand,
			float value);
#endif
		// Remoting
		static EXRDeviceConnectionResult::Type ConnectToRemoteHoloLens(FString remoteIP, unsigned int bitrate, bool isHoloLens1);
		static void DisconnectFromRemoteHoloLens();

		DECLARE_MULTICAST_DELEGATE_TwoParams(FOnConfigureGesturesDelegate, const FXRGestureConfig&, bool& /*bSuccess*/);
		static FOnConfigureGesturesDelegate OnConfigureGesturesDelegate;
		static FDelegateHandle ConfigureGesturesHandle;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnGetXRSystemFlagsDelegate, int32& /*Flags*/);
		static FOnGetXRSystemFlagsDelegate OnGetXRSystemFlagsDelegate;
		static FDelegateHandle GetXRSystemFlagsHandle;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnTogglePlayDelegate, bool /*bOnOff*/);
		static FOnTogglePlayDelegate OnTogglePlayDelegate;
		static FDelegateHandle TogglePlayDelegateHandle;

		DECLARE_MULTICAST_DELEGATE_FiveParams(FOnGetHandJointTransformDelegate, EControllerHand /*Hand*/, EHandKeypoint /*Keypoint*/, FTransform& /*Transform*/, float& /*OutRadius*/, bool& /*bSuccess*/ );
		static FOnGetHandJointTransformDelegate OnGetHandJointTransformDelegate;
		static FDelegateHandle GetHandJointTransformDelegateHandle;
	};
}
