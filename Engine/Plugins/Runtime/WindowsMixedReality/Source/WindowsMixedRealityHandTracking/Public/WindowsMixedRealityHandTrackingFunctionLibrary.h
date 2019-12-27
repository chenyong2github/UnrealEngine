// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityHandTrackingTypes.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "ILiveLinkSource.h"
#include "WindowsMixedRealityHandTrackingFunctionLibrary.generated.h"

UCLASS(ClassGroup = WindowsMixedReality)
class WINDOWSMIXEDREALITYHANDTRACKING_API UWindowsMixedRealityHandTrackingFunctionLibrary :
	public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	Get Transform for a point on the hand.

	@param Hand
	@param Keypoint the specific joint or wrist point to fetch.
	@param Transform Output parameter to write the data to.
	@return true if the output param was populated with a valid value, false means that is is either unchanged or populated with a stale value.
	*/
	UFUNCTION(BlueprintCallable, Category = "HandTracking|WindowsMixedReality")
	static bool GetHandJointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& Transform);
};
