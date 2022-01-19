// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsMixedRealityHandTrackingFunctionLibrary.h"
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "WindowsMixedRealityHandTracking.h"

bool UDEPRECATED_WindowsMixedRealityHandTrackingFunctionLibrary::GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform, float& OutRadius)
{
	TSharedPtr<FWindowsMixedRealityHandTracking> HandTracking = StaticCastSharedPtr<FWindowsMixedRealityHandTracking>(IWindowsMixedRealityHandTrackingModule::Get().GetInputDevice());

	//UE_LOG(LogWindowsMixedRealityHandTracking, Display, TEXT("CNNTEMP GetGestureKeypointTransform 0"));
	if (HandTracking.IsValid() && HandTracking->IsHandTrackingStateValid())
	{
		FTransform KeyPointTransform;
		const bool bSuccess = HandTracking->GetKeypointTransform(Hand, Keypoint, KeyPointTransform);
		//UE_LOG(LogWindowsMixedRealityHandTracking, Display, TEXT("CNNTEMP GetGestureKeypointTransform hand %d joint %d (%d)"), (uint32)Hand, (uint32)Keypoint, bSuccess);

		if (bSuccess)
		{
			OutTransform = KeyPointTransform;
			HandTracking->GetKeypointRadius(Hand, Keypoint, OutRadius);
			return true;
		}
	}

	return false;
}

bool UDEPRECATED_WindowsMixedRealityHandTrackingFunctionLibrary::SupportsHandTracking()
{
	return WindowsMixedReality::FWindowsMixedRealityStatics::SupportsHandTracking();
}
