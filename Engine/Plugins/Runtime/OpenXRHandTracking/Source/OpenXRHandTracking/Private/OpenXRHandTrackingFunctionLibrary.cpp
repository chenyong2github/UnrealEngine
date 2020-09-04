// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRHandTrackingFunctionLibrary.h"
#include "IOpenXRHandTrackingModule.h"
#include "OpenXRHandTracking.h"
#include "IOpenXRHMDPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Engine/World.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"

bool UOpenXRHandTrackingFunctionLibrary::GetHandJointTransform(EControllerHand Hand, EOpenXRHandKeypoint Keypoint, FTransform& OutTransform, float& OutRadius)
{
	TSharedPtr<FOpenXRHandTracking> HandTracking = StaticCastSharedPtr<FOpenXRHandTracking>(IOpenXRHandTrackingModule::Get().GetInputDevice());

	if (HandTracking.IsValid() && HandTracking->IsHandTrackingStateValid())
	{
		FTransform KeyPointTransform;
		const bool bSuccess = HandTracking->GetKeypointTransform(Hand, Keypoint, KeyPointTransform);
		if (bSuccess)
		{
			OutTransform = KeyPointTransform;
			HandTracking->GetKeypointRadius(Hand, Keypoint, OutRadius);
			return true;
		}
	}

	return false;
}

bool UOpenXRHandTrackingFunctionLibrary::SupportsHandTracking()
{
	TSharedPtr<FOpenXRHandTracking> HandTracking = StaticCastSharedPtr<FOpenXRHandTracking>(IOpenXRHandTrackingModule::Get().GetInputDevice());
	return HandTracking.IsValid() && HandTracking->IsHandTrackingSupportedByDevice();
}
