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
DECLARE_DYNAMIC_DELEGATE_FourParams(FImageTargetUnreliableTracking, const FVector&, LastTrackedLocation, const FRotator&, LastTrackedRotation, const FVector&, NewUnreliableLocation, const FRotator&, NewUnreliableRotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FImageTargetUnreliableTrackingMulti, const FVector&, LastTrackedLocation, const FRotator&, LastTrackedRotation, const FVector&, NewUnreliableLocation, const FRotator&, NewUnreliableRotation);

/**
	Delegate used to notify the instigating blueprint that the target image's location/rotation has changed.
	@param NewLocation The new location of the target image (which may or may not be accurate).
	@param NewRotation The new rotation of the target image (which may or may not be accurate).
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FImageTargetReliableTracking, const FVector&, NewLocation, const FRotator&, NewRotation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FImageTargetReliableTrackingMulti, const FVector&, NewLocation, const FRotator&, NewRotation);

/** Delegate used to notify the instigating blueprint that the target image was successfully set. */
DECLARE_DYNAMIC_DELEGATE(FSetImageTargetSucceeded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSetImageTargetSucceededMulti);

/** Delegate used to notify the instigating blueprint that the target image failed to be set. */
DECLARE_DYNAMIC_DELEGATE(FSetImageTargetFailed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSetImageTargetFailedMulti);

/** Delegate used to notify the instigating blueprint that the target image is currently visible to the camera */
DECLARE_DYNAMIC_DELEGATE(FImageTargetFound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FImageTargetFoundMulti);

/** Delegate used to notify the instigating blueprint that the target image just became invisible to the camera */
DECLARE_DYNAMIC_DELEGATE(FImageTargetLost);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FImageTargetLostMulti);

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
	FSetImageTargetSucceededMulti OnSetImageTargetSucceeded;
	FSetImageTargetFailedMulti OnSetImageTargetFailed;
	FImageTargetFoundMulti OnImageTargetFound;
	FImageTargetLostMulti OnImageTargetLost;
	FImageTargetUnreliableTrackingMulti OnImageTargetUnreliableTracking;

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
