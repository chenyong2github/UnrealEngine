// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VRNotificationsComponent.h"
#include "MagicLeapHMDFunctionLibrary.h"
#include "MagicLeapHeadTrackingNotificationsComponent.generated.h"

/** 
  Provides head tracking map events to enable apps to cleanly handle it. 
  The most important event to be aware of is when a map changes.
  In the case that a new map session begins, or recovery fails, all formerly cached transform
  and world reconstruction data (raycast, planes, mesh) is invalidated and must be updated.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class MAGICLEAP_API UMagicLeapHeadTrackingNotificationsComponent : public UVRNotificationsComponent
{
	GENERATED_BODY()

public:
	UMagicLeapHeadTrackingNotificationsComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	void BindToOnHeadTrackingLostEvent(const FVRNotificationsDelegate& EventDelegate);
	void BindToOnHeadTrackingRecoveredEvent(const FVRNotificationsDelegate& EventDelegate);
	void BindToOnHeadTrackingRecoveryFailedEvent(const FVRNotificationsDelegate& EventDelegate);
	void BindToOnHeadTrackingNewSessionStartedEvent(const FVRNotificationsDelegate& EventDelegate);

	void UnbindOnHeadTrackingLostEvent();
	void UnbindOnHeadTrackingRecoveredEvent();
	void UnbindOnHeadTrackingRecoveryFailedEvent();
	void UnbindOnHeadTrackingNewSessionStartedEvent();

private:
	/** Map was lost. It could possibly recover. */
	UPROPERTY(BlueprintAssignable)
	FVRNotificationsDelegate OnHeadTrackingLost;

	/** Previous map was recovered. */
	UPROPERTY(BlueprintAssignable, meta = (AllowPrivateAccess = true))
	FVRNotificationsDelegate OnHeadTrackingRecovered;

	/** Failed to recover previous map. */
	UPROPERTY(BlueprintAssignable, meta = (AllowPrivateAccess = true))
	FVRNotificationsDelegate OnHeadTrackingRecoveryFailed;

	/** New map session created. */
	UPROPERTY(BlueprintAssignable, meta = (AllowPrivateAccess = true))
	FVRNotificationsDelegate OnHeadTrackingNewSessionStarted;

	TSet<EMagicLeapHeadTrackingMapEvent> PreviousMapEvents;
};
