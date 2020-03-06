// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Lumin/CAPIShims/LuminAPIImageTracking.h"
#include "MagicLeapImageTrackerTypes.generated.h"

/**
	Delegate used to notify the instigating blueprint that the target image's location has become unrealiable.
	@param LastTrackedLocation The last reliable location of the target image.
	@param LastTrackedRotation The last reliable rotation of the target image.
	@param NewUnreliableLocation The new location of the target image (which may or may not be accurate).
	@param NewUnreliableRotation The new rotation of the target image (which may or may not be accurate).
*/
DECLARE_DYNAMIC_DELEGATE_FourParams(FMagicLeapImageTargetUnreliableTracking, const FVector&, LastTrackedLocation, const FRotator&, LastTrackedRotation, const FVector&, NewUnreliableLocation, const FRotator&, NewUnreliableRotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMagicLeapImageTargetUnreliableTrackingMulti, const FVector&, LastTrackedLocation, const FRotator&, LastTrackedRotation, const FVector&, NewUnreliableLocation, const FRotator&, NewUnreliableRotation);

/**
	Delegate used to notify the instigating blueprint that the target image's location/rotation has changed.
	@param NewLocation The new location of the target image (which may or may not be accurate).
	@param NewRotation The new rotation of the target image (which may or may not be accurate).
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapImageTargetReliableTracking, const FVector&, NewLocation, const FRotator&, NewRotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapImageTargetReliableTrackingMulti, const FVector&, NewLocation, const FRotator&, NewRotation);

/** Delegate used to notify LuminARImageTracker that the target image was successfully set. */
DECLARE_DELEGATE_OneParam(FMagicLeapSetImageTargetSucceededStaticDelegate, FMagicLeapImageTrackerTarget& /*Target*/);

/** Delegate used to notify the instigating blueprint that the target image was successfully set. */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapSetImageTargetSucceeded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapSetImageTargetSucceededMulti);

/** Delegate used to notify the instigating blueprint that the target image failed to be set. */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapSetImageTargetFailed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapSetImageTargetFailedMulti);

/** Delegate used to notify the instigating blueprint that the target image is currently visible to the camera */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapImageTargetFound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapImageTargetFoundMulti);

/** Delegate used to notify the instigating blueprint that the target image just became invisible to the camera */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapImageTargetLost);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapImageTargetLostMulti);

USTRUCT()
struct FMagicLeapImageTrackerTarget
{
	GENERATED_BODY()

	FString Name;
#if WITH_MLSDK
	MLHandle Handle;
	MLImageTrackerTargetStaticData Data;
	MLImageTrackerTargetSettings Settings;
	MLImageTrackerTargetResult OldTrackingStatus;
#endif // WITH_MLSDK
	UTexture2D* Texture;
	FVector Location;
	FRotator Rotation;
	FVector UnreliableLocation;
	FRotator UnreliableRotation;
	bool bUseUnreliablePose;
	FMagicLeapSetImageTargetSucceededMulti OnSetImageTargetSucceeded;
	FMagicLeapSetImageTargetFailedMulti OnSetImageTargetFailed;
	FMagicLeapImageTargetFoundMulti OnImageTargetFound;
	FMagicLeapImageTargetLostMulti OnImageTargetLost;
	FMagicLeapImageTargetUnreliableTrackingMulti OnImageTargetUnreliableTracking;
	FMagicLeapSetImageTargetSucceededStaticDelegate SetImageTargetSucceededDelegate;

	FMagicLeapImageTrackerTarget()
	: Name(TEXT("Undefined"))
#if WITH_MLSDK
	, Handle(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
	, Texture(nullptr)
	{
#if WITH_MLSDK
		OldTrackingStatus.status = MLImageTrackerTargetStatus_Ensure32Bits;
#endif // WITH_MLSDK
	}
};
