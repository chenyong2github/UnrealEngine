// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimCharacterMovementTypes.h"
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
	 * Populate a snapshot struct with movement data that's commonly used by animation graph logic.
	 * To avoid performance costs from calling this in the Event Graph on the game thread, it's recommended to call it in BlueprintThreadSafeUpdateAnimation and
 	 * use the Property Access system to access the input parameters (Property Access will handle copying the inputs at the right time in the frame).
	 * @param WorldTransform - The transform of the character in world space.
	 * @param WorldVelocity - The velocity of the character in world space.
	 * @param WorldAcceleration - The acceleration of the character in world space.
	 * @param bIsOnGround - If the character is on the ground.
	 * @param RootYawOffset - Offset being applied to the root bone in the animation graph (e.g. for countering capsule rotation). Set to zero if not needed.
	 * @param Snapshot - The snapshot to write to. This is typically a member variable of the animation blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static void UpdateCharacterMovementSnapshot(const FTransform& WorldTransform, const FVector& WorldVelocity, const FVector& WorldAcceleration, bool bIsOnGround,
		float RootYawOffset, UPARAM(ref) FAnimCharacterMovementSnapshot& Snapshot);

	/**
	 * Calculate the closest cardinal direction to the direction the character is currently moving.
	 * @param PreviousCardinalDirection - The cardinal direction from the previous frame. Typically the animation blueprint holds a EAnimCardinalDirection variable.
	 * @param DirectionAngleInDegrees - The direction that the character is currently moving. FAnimCharacterMovementSnapshot.VelocityYawAngle is a commonly used input for this.
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
	 * @param MovementSnapshot - Snapshot of current movement properties.
	 * @param PredictionSnapshot - Snapshot of parameters needed to predict how the movement component will move. Because this is a thread safe function, it's
	 *	recommended to populate these fields via the Property Access system.
	 * @return The predicted stop position in local space to the character. The size of this vector will be the distance to stop.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static FVector PredictGroundMovementStopLocation(const FAnimCharacterMovementSnapshot& MovementSnapshot, const FAnimCharacterMovementPredictionSnapshot& PredictionSnapshot);

	/**
	 * Predict where the character will change direction during a pivot based on its current movement properties and parameters from the movement component.
	 * This uses prediction logic that is heavily tied to the UCharacterMovementComponent.
	 * @param MovementSnapshot - Snapshot of current movement properties.
	 * @param GroundFriction - Value from the movement component. Because this is a thread safe function, it's recommended to populate this field via the Property Access system.
	 * @return The predicted pivot position in local space to the character. The size of this vector will be the distance to the pivot.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Character Movement", meta = (BlueprintThreadSafe))
	static FVector PredictGroundMovementPivotLocation(const FAnimCharacterMovementSnapshot& MovementSnapshot, float GroundFriction);
};