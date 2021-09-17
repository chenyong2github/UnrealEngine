// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimTurnInPlaceTypes.h"
#include "AnimTurnInPlaceLibrary.generated.h"

class UAnimSequence;

/**
 * Turn In Place functionality maintains an offset between the capsule and the mesh to prevent the animated pose from spinning when the character rotates on the spot.
 * Once the offset gets large enough, an animation can be played to rotate the mesh closer to the character's facing direction.
 */
UCLASS()
class ANIMATIONLOCOMOTIONLIBRARYRUNTIME_API UAnimTurnInPlaceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Update a Turn in Place state structure based on the rotation of a character's mesh.
	 * A typical setup is to have a FAnimTurnInPlaceState variable on the animation blueprint and call this function to update it in BlueprintThreadSafeUpdateAnimation.
	 * The animation blueprint will use a Rotate Root Bone node to apply the RootYawOffset from the state. The locomotion state machine will have a state to play the 
	 * active turn transition if requested and a state to play the turn recovery if requested. 
	 * @param DeltaTime - Time since last update.
	 * @param bAllowTurnInPlace - This should be set to true when character rotation should no longer rotate the mesh (typically during stops and idles).
	 * @param bHoldYawOffset - This should be true when the root yaw offset should be maintained but no longer updated (e.g. during start animations).
	 * @param bIsTurnTransitionStateRelevant - True when the animation blueprint is in the turn transition state. UCachedAnimDataLibrary::StateMachine_IsStateRelevant() can be used for this test.
	 * @param MeshWorldRotation - The current world rotation of the character's mesh component.
	 * @param AnimSet - Set of turn on spot animations used to turn the mesh to align with the character's facing.
	 * @param TurnInPlaceState - The turn in place state information to update. This typically is a variable on the animation blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation Turn In Place", meta = (BlueprintThreadSafe))
	static void UpdateTurnInPlace(float DeltaTime, bool bAllowTurnInPlace, bool bHoldYawOffset, bool bIsTurnTransitionStateRelevant,
		const FRotator& MeshWorldRotation, const FAnimTurnInPlaceAnimSet& AnimSet, UPARAM(ref) FAnimTurnInPlaceState& TurnInPlaceState);
};