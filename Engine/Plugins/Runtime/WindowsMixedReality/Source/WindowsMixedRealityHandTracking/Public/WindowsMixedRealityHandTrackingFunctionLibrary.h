// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityHandTrackingTypes.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "WindowsMixedRealityHandTrackingFunctionLibrary.generated.h"

UCLASS(ClassGroup = WindowsMixedReality)
class WINDOWSMIXEDREALITYHANDTRACKING_API UWindowsMixedRealityHandTrackingFunctionLibrary :
	public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	Returns true if hand tracking available.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandTracking|WindowsMixedReality", meta = (DeprecatedFunction, DeprecationMessage = "Use GetXRSystemFlags and check for SupportsHandTracking"))
	static bool SupportsHandTracking();

	/**
	Get Transform for a point on the hand.

	@param Hand
	@param Keypoint the specific joint or wrist point to fetch.
	@param Transform The joint's transform.
	@param Radius The distance from the joint position to the surface of the hand.
	@return true if the output param was populated with a valid value, false means that the tracking is lost and output is undefined.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandTracking|WindowsMixedReality", meta=(DeprecatedFunction, DeprecationMessage="Use GetMotionControllerData HandKeyPositions and HandKeyRadii"))
	static bool GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& Transform, float& Radius);
};
