// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsMixedRealityHandTrackingFunctionLibrary.h"
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "WindowsMixedRealityHandTracking.h"
#include "IWindowsMixedRealityHMDPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/World.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"

bool UWindowsMixedRealityHandTrackingFunctionLibrary::GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform)
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
			OutTransform = KeyPointTransform * UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(GWorld);
			return true;
		}
	}

	return false;
}
