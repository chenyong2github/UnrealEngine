// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
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
DECLARE_DELEGATE_OneParam(FMagicLeapSetImageTargetCompletedStaticDelegate, const FString& /* TargetName */);

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

UENUM(BlueprintType)
enum class EMagicLeapImageTargetStatus : uint8
{
	Tracked,
	Unreliable,
	NotTracked
};

USTRUCT(BlueprintType)
struct MAGICLEAPIMAGETRACKER_API FMagicLeapImageTargetSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	UTexture2D* ImageTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	float LongerDimension;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	bool bIsStationary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	bool bIsEnabled;

public:
	FMagicLeapImageTargetSettings();
};

USTRUCT(BlueprintType)
struct MAGICLEAPIMAGETRACKER_API FMagicLeapImageTargetState
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ImageTracking|MagicLeap")
	EMagicLeapImageTargetStatus TrackingStatus = EMagicLeapImageTargetStatus::Tracked;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ImageTracking|MagicLeap")
	FVector Location = FVector(0.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ImageTracking|MagicLeap")
	FRotator Rotation = FRotator(0.0f);
};

UENUM(BlueprintType)
enum class EMagicLeapImageTargetOrientation : uint8
{
	/**
		Unreal's Forward (X) axis will be normal to image target plane,
		Right (Y) axis will point to the edge towards the left hand of the user facing the image (this edge is "right" for the image itself),
		Up (Z) axis will point to the top edge of the image.
	*/
	ForwardAxisAsNormal,

	/**
		Unreal's Up (Z) axis will be normal to image target plane,
		Right (Y) axis will point to the edge towards the right hand of the user facing the image,
		Forward (X) axis will point to the top edge of the image.
		Matches image target orientation reported by ARKit, ARCore.
	*/
	UpAxisAsNormal
};
