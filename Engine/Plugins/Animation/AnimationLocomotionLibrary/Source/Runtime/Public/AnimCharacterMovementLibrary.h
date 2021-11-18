// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimCharacterMovementTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AnimCharacterMovementLibrary.generated.h"

class UAnimSequence;

/**
  * Library of common techniques for driving character locomotion animations. 
  */
UCLASS()
class ANIMATIONLOCOMOTIONLIBRARYRUNTIME_API UAnimCharacterMovementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Calculate the closest cardinal direction to the direction the character is currently moving.
	 * @param PreviousCardinalDirection - The cardinal direction from the previous frame. Typically the animation blueprint holds a EAnimCardinalDirection variable.
	 * @param DirectionAngleInDegrees - The direction that the character is currently moving.
	 * @param DirectionDeadZoneAngle - Deadzone to prevent flickering between directions at angle boundaries.
	 * @return The resulting cardinal direction.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static EAnimCardinalDirection GetCardinalDirectionFromAngle(EAnimCardinalDirection PreviousCardinalDirection, float DirectionAngleInDegrees, float DeadZoneAngle);

	/**
	 * Select an animation to play based on the cardinal direction calculated by GetCardinalDirectionFromAngle(). For example, this can pick a start animation
	 * to play based on the character's movement direction.	    
	 * @param CardinalDirection - The closest cardinal direction to the character's movement direction.
	 * @param AnimSet - The set of animations to choose from.
	 * @return The animation to play.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static const UAnimSequence* SelectAnimForCardinalDirection(EAnimCardinalDirection CardinalDirection, const FCardinalDirectionAnimSet& AnimSet);

	/**
	 * Predict where the character will stop based on its current movement properties and parameters from the movement component.
	 * This uses prediction logic that is heavily tied to the UCharacterMovementComponent.
	 * Each parameter corresponds to a value from the UCharacterMovementComponent with the same name.
	 * Because this is a thread safe function, it's	recommended to populate these fields via the Property Access system.
	 * @return The predicted stop position in local space to the character. The size of this vector will be the distance to the stop location.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static FVector PredictGroundMovementStopLocation(const FVector& Velocity,
		bool bUseSeparateBrakingFriction, float BrakingFriction, float GroundFriction, float BrakingFrictionFactor, float BrakingDecelerationWalking);

	/**
	 * Predict where the character will change direction during a pivot based on its current movement properties and parameters from the movement component.
	 * This uses prediction logic that is heavily tied to the UCharacterMovementComponent.
	 * Each parameter corresponds to a value from the UCharacterMovementComponent with the same name.
	 * Because this is a thread safe function, it's	recommended to populate these fields via the Property Access system.
	 * @return The predicted pivot position in local space to the character. The size of this vector will be the distance to the pivot.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static FVector PredictGroundMovementPivotLocation(const FVector& Acceleration, const FVector& Velocity, float GroundFriction);
};