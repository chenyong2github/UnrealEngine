// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapMovementTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapMovementType : uint8
{
	Controller3DOF,
	Controller6DOF
};

/** Settings for a movement session. */
USTRUCT(BlueprintType)
struct MAGICLEAPMOVEMENT_API FMagicLeapMovementSettings
{
	GENERATED_BODY()

	/** The movement type to use when updating the transform of the set controller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	EMagicLeapMovementType MovementType = EMagicLeapMovementType::Controller6DOF;

	/** Number of frames of sway history to track.  Increase to improve smoothing.  Minimum value of 3. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "3", ClampMax = "100", UIMin = "3", UIMax = "100"))
	int32 SwayHistorySize = 30;

	/**
		Maximum angle, in degrees, between the oldest and newest headpose to object vector.  Increasing this will increase the
		maximum speed of movement.  Must be greater than zero.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "1.0", ClampMax = "360.0", UIMin = "1.0", UIMax = "360.0"))
	float MaxDeltaAngle = 28.647888f;

	/**
		A unitless number that governs the smoothing of Control input.  Larger values will make the movement more twitchy,
		smaller values will make it smoother by increasing latency between Control input and object movement response by averaging
		multiple frames of input values.  Must be greater than zero.  Typical values would be between 0.5 and 10.0.  This is
		defaulted to 7.0.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "0.5", UIMin = "0.5"))
	float ControlDampeningFactor = 3.5f;

	/**
		The maximum angle, in degrees, that the object will be tilted left/right and front/back.  Cannot be a negative value,
		but may be zero.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float MaxSwayAngle = 30.0f;

	/**
		The speed of rotation that will stop implicit depth translation from happening.  The speed of rotation about the
		headpose Y-axis, in degrees per second, that if exceeded, stops implicit depth translation from happening.  Must be greater
		than zero.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "1.0", ClampMax = "360.0", UIMin = "1.0", UIMax = "360.0"))
	float MaximumHeadposeRotationSpeed = 300.0f;

	/**
		The maximum speed that headpose can move, in cm per second, that will stop implicit depth translation.  If the
		headpose is moving faster than this speed (meters per second) implicit depth translation doesn't happen.  Must be greater
		than zero.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "75.0", UIMin = "75.0"))
	float MaximumHeadposeMovementSpeed = 75.0f;

	/** Distance object must move in depth since the last frame to cause maximum push/pull sway.  Must be greater than zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "10.0", UIMin = "10.0"))
	float MaximumDepthDeltaForSway = 10.0f;

	/**
		The minimum distance in cm the object can be moved in depth relative to the headpose.  This must be greater than
		zero and less than MaximumDistance.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "50.0", UIMin = "50.0"))
	float MinimumDistance = 50.0f;

	/**
		The maximum distance in cm the object can be moved in depth relative to the headpose.  This must be greater than
		zero and MinimumDistance.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "60.0", UIMin = "60.0"))
	float MaximumDistance =  1500.0f;

	/**
		Maximum length of time, in seconds, lateral sway should take to decay.  Maximum length of time (in seconds) lateral sway
		should take to decay back to an upright orientation once lateral movement stops.  Defaults to 0.15, must be greater
		than zero.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "0.15", UIMin = "0.15"))
	float MaximumSwayTimeS = 0.4f;

	/**
		Maximum length of time, in seconds, to allow DetatchObject() to resolve before forcefully aborting.  This serves as a
		fail-safe for instances where the object is in a bad position and can't resolve to a safe position.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EndResolveTimeoutS = 10.0f;

	/**
		The percentage (0 to 1) of the moved object's radius that can penetrate a colliding object.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxPenetrationPercentage = 0.3f;
};

/** 3DoF specific movement settings that must be provided when starting a 3DoF movement session. */
USTRUCT(BlueprintType)
struct MAGICLEAPMOVEMENT_API FMagicLeapMovement3DofSettings
{
	GENERATED_BODY()

	/** If the object should automatically center on the control direction when beginning movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	bool bAutoCenter;
};

/** 6DoF specific movement settings that must be provided when starting a 3DoF movement session. */
USTRUCT(BlueprintType)
struct MAGICLEAPMOVEMENT_API FMagicLeapMovement6DofSettings
{
	GENERATED_BODY()

	/** If the object should automatically center on the control direction when beginning movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	bool bAutoCenter;
};
