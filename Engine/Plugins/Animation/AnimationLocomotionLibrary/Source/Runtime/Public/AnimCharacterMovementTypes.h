// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimCharacterMovementTypes.generated.h"

class UAnimSequence;

UENUM(BlueprintType)
enum class EAnimCardinalDirection : uint8
{
	North,
	East,
	South,
	West
};

/**
 * Animations for a locomotion set authored with only four cardinal directions.
 * This will often be accompanied by Orientation Warping to account for diagonals.
 */
USTRUCT(BlueprintType)
struct FCardinalDirectionAnimSet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> NorthAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> EastAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> SouthAnim = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
	TObjectPtr<UAnimSequence> WestAnim = nullptr;
};

/**
 * Snapshot of movement properties used to predict where the character will move in the future.
 * These properties are found on the UCharacterMovementComponent. They're copied (usually via Property Access) on the game thread
 * so they can be used in thread safe functions during animation update.
 */
USTRUCT(BlueprintType)
struct FAnimCharacterMovementPredictionSnapshot
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = MovementPrediction)
	float GroundFriction = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = MovementPrediction)
	float BrakingFriction = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = MovementPrediction)
	float BrakingFrictionFactor = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = MovementPrediction)
	float BrakingDecelerationWalking = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = MovementPrediction)
	bool bUseSeparateBrakingFriction = false;
};

/**
 * Snapshot of movement data commonly used to drive locomotion animations.
 * See UAnimationCharacterMovementLibrary::UpdateCharacterMovementSnapshot() for an example of how to populate this data.
 */
USTRUCT(BlueprintType)
struct FAnimCharacterMovementSnapshot
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	FVector WorldVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	FVector LocalVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	FVector WorldAcceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	FVector LocalAcceleration = FVector::ZeroVector;

	/** Angle (in degrees) between velocity and the character's forward vector. */
	UPROPERTY(BlueprintReadWrite, Category = Movement)
	float VelocityYawAngle = 0.0f;

	/** Angle (in degrees) between acceleration and the character's forward vector. */
	UPROPERTY(BlueprintReadWrite, Category = Movement)
	float AccelerationYawAngle = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	float Distance2DTraveledSinceLastUpdate = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	float Speed2D = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	float AccelerationSize2D = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = Movement)
	bool bIsOnGround = false;
};