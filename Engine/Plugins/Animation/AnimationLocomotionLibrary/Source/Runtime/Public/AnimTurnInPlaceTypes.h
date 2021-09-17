// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/CachedAnimData.h"
#include "Engine/SpringInterpolator.h"
#include "AnimTurnInPlaceTypes.generated.h"

class UAnimSequence;

/**
 * Settings for an animation of a character turning on the spot.
 */
USTRUCT(BlueprintType)
struct FAnimTurnTransition
{
	GENERATED_BODY()

public:

	/** The animation to play. */
	UPROPERTY(EditAnywhere, Category = "TurnTransition")
	TObjectPtr<UAnimSequence> Anim = nullptr;

	/** Once the offset between the character and mesh is large enough to trigger this transition, wait this time before triggering the animation. */
	UPROPERTY(EditAnywhere, Category = "TurnTransition")
	float DelayBeforeTrigger = 0.f;
};

/**
 * Set of turn on the spot animations.
 */
USTRUCT(BlueprintType)
struct FAnimTurnInPlaceAnimSet
{
	GENERATED_BODY()

public:
	/**
	 * Look for the best transition that will result in a smaller offset.
	 * E.g. If YawOffset is 60 degrees, a transition that rotates 90 degrees will result in a 30 degree offset.
	 */
	int32 FindBestTurnTransitionIndex(float YawOffset) const;

	/** Potential animations to play. */
	UPROPERTY(EditAnywhere, Category = "Turns")
	TArray<FAnimTurnTransition> TurnTransitions;

	/** The offset between the capsule and mesh has to be bigger than the animation turn angle plus this deadzone to trigger an animation. */
	UPROPERTY(EditAnywhere, Category = "Turns")
	float TurnDeadZoneAngle = 0.f;
};

/**
 * State that needs to be tracked for triggering turn in place animations and for maintaining an offset between the capsule and mesh. 
 */
USTRUCT(BlueprintType)
struct FAnimTurnInPlaceState
{
	GENERATED_BODY()

public:

	FAnimTurnInPlaceState()
	{
		YawOffsetInterpolator.SetDefaultSpringConstants(30.f);
	}

	/**
	 * See UAnimTurnInPlaceLibrary::UpdateTurnInPlace() for parameter documentation.
	 */ 
	void Update(float DeltaTime, bool bAllowTurnInPlace, bool bHoldYawOffset, bool bIsTurnTransitionStateRelevant,
		const FRotator& MeshWorldRotation, const FAnimTurnInPlaceAnimSet& AnimSet, UPARAM(ref) FAnimTurnInPlaceState& TurnInPlaceState);

	/** Animation that is actively rotating the mesh. */
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	TObjectPtr<UAnimSequence> ActiveTurnAnim = nullptr;

	/** When there's no longer any rotation left in the ActiveTurnAnim, it switches to being the recovery animation to finish playing.
	    The recovery animation can be interrupted by a new turn animation. They are tracked separately so they can be cross-fade blended together. */
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	TObjectPtr<UAnimSequence> TurnRecoveryAnim = nullptr;

	/** The desired offset between the capsule and the mesh. */
	UPROPERTY(BlueprintReadWrite, Category = "Root Offset")
	float RootYawOffset = 0.f;

	/** The inverse of the root yaw offset. This is useful for having an aim offset to keep the upper body looking in the same direction
		as the character while the lower body stays planted. */
	UPROPERTY(BlueprintReadWrite, Category = "Root Offset")
	float RootYawOffsetInverse = 0.f;

	/** The current time of the active turn animation. */
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	float ActiveTurnAnimTime = 0.f;

	/** The time that the recovery animation should start playing at. This will be set to the active turn animation's time when it switches
		to being the recovery animation. */
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	float TurnRecoveryAnimStartTime = 0.f;

	/** Flag that the animation blueprint can use to trigger a turn in place transition state.*/
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	bool bTurnTransitionRequested = false;

	/** Flag that the animation blueprint can use to trigger a turn in place recovery state. */
	UPROPERTY(BlueprintReadWrite, Category = "Turns")
	bool bTurnRecoveryRequested = false;

private:
	/**
	 * Advance the turn transition animation and back out any rotation that the animation plays from the root yaw offset.
	 */ 
	void UpdateActiveTurnTransition(float DeltaTime);
	
	/**
	 * Check to see if a turn transition should trigger.
	 */ 
	void UpdateTurnTransitionTrigger(float DeltaTime, const FAnimTurnInPlaceAnimSet& AnimSet);

	/** If the character starts moving while there's a root yaw offset, the offset will be blended out with this interpolator. */
	UPROPERTY(EditAnywhere, Category = "Root Offset", meta = (AllowPrivateAccess = "true"))
	FFloatRK4SpringInterpolator YawOffsetInterpolator;

	/** The turn transition animation that is desired to play, but is still waiting for its trigger delay to finish. */
	UPROPERTY(transient)
	TObjectPtr<UAnimSequence> PendingTurnAnim = nullptr;

	/** How long the system has been waiting to trigger PendingTurnAnim. */
	float PendingTurnDelayCounter = 0.f;

	/** World yaw of the mesh component. This is tracked to detect deltas in rotation between updates.
		The mesh component's yaw is used, rather than the capsule, to account for mesh smoothing on simulated proxies. */
	float MeshWorldYaw = FLT_MAX;
};