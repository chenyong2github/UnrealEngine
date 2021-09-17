// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTurnInPlaceTypes.h"
#include "Animation/AnimSequence.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimTurnInPlaceLibrary, Verbose, All);

int32 FAnimTurnInPlaceAnimSet::FindBestTurnTransitionIndex(float YawOffset) const
{
	int32 BestTurnTransitionIndex = INDEX_NONE;
	float BestResultingOffsetSize = MAX_FLT;

	for (int32 Index = 0; Index < TurnTransitions.Num(); Index++)
	{
		if (const UAnimSequence* TurnTransitionAnim = TurnTransitions[Index].Anim)
		{
			// TODO: We could potentially cache the rotation info in the struct or a curve in the asset.
			const FTransform TransitionAnimRootMotion = TurnTransitionAnim->ExtractRootMotionFromRange(0.f, TurnTransitionAnim->GetPlayLength());
			const float TransitionAnimYaw = FMath::RadiansToDegrees(TransitionAnimRootMotion.GetRotation().GetTwistAngle(FVector::UpVector));

			// Calculate the yaw offset that would result from playing the animation.
			const float OffsetAfterTransition = FRotator::NormalizeAxis(YawOffset + TransitionAnimYaw);
			const float OffsetSizeAfterTransition = FMath::Abs(OffsetAfterTransition);

			// Check if the animation will result in a smaller yaw offset.
			if ((OffsetSizeAfterTransition + TurnDeadZoneAngle) < FMath::Abs(YawOffset))
			{
				const bool bTransitionResultsInSmallerAngle = (OffsetSizeAfterTransition < BestResultingOffsetSize);

				// Prefer animations that rotate toward the offset.
				// E.g. If the offset is -160 degrees, we prefer to rotate 180 degrees toward the offset, rather than 180 away, even though they will result in the same new offset.
				const bool bSameResultButBetterDirection = FMath::IsNearlyEqual(OffsetSizeAfterTransition, BestResultingOffsetSize) && (TransitionAnimYaw * YawOffset < 0.f);

				if (bTransitionResultsInSmallerAngle || bSameResultButBetterDirection)
				{
					BestTurnTransitionIndex = Index;
					BestResultingOffsetSize = OffsetSizeAfterTransition;
				}
			}
		}
	}

	return BestTurnTransitionIndex;
}

void FAnimTurnInPlaceState::Update(float DeltaTime, bool bAllowTurnInPlace, bool bHoldYawOffset, bool bIsTurnTransitionStateRelevant,
	const FRotator& MeshWorldRotation, const FAnimTurnInPlaceAnimSet& AnimSet, UPARAM(ref) FAnimTurnInPlaceState& TurnInPlaceState)
{
	bTurnTransitionRequested = false;
	bTurnRecoveryRequested = false;

	// Avoid large offset from default value on first update.
	if (MeshWorldYaw == FLT_MAX)
	{
		MeshWorldYaw = MeshWorldRotation.Yaw;
	}

	// Calculate how much the mesh has rotated since the last update.
	const float YawDeltaSinceLastUpdate = MeshWorldRotation.Yaw - MeshWorldYaw;
	MeshWorldYaw = MeshWorldRotation.Yaw;

	if (bAllowTurnInPlace)
	{
		// Apply any recent mesh rotation to the root offset.
		RootYawOffset = FRotator::NormalizeAxis(RootYawOffset - YawDeltaSinceLastUpdate);

		// Update the current turn transition.
		if (bIsTurnTransitionStateRelevant)
		{
			UpdateActiveTurnTransition(DeltaTime);
		}
		// Trigger a turn transition animation if necessary.
		else
		{
			UpdateTurnTransitionTrigger(DeltaTime, AnimSet);
		}
	}
	// Interpolate out the offset if it's no longer requested.
	else if (!bHoldYawOffset)
	{
		RootYawOffset = YawOffsetInterpolator.Update(RootYawOffset, 0.f, DeltaTime);
	}

	RootYawOffsetInverse = -RootYawOffset;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FString LogMessage = FString::Printf(TEXT("RootYawOffset(%.2f) PendingTurnAnim(%s) PendingTurnDelayCounter(%.2f) ActiveTurnAnim(%s) ActiveTurnAnimTime(%.2f) bTurnTransitionRequested(%d) bHoldYawOffset(%d) bIsTurnTransitionStateRelevant(%d)"), 
		RootYawOffset, *GetNameSafe(PendingTurnAnim), PendingTurnDelayCounter, *GetNameSafe(ActiveTurnAnim), ActiveTurnAnimTime, bTurnTransitionRequested, bHoldYawOffset, bIsTurnTransitionStateRelevant);
	UE_LOG(LogAnimTurnInPlaceLibrary, VeryVerbose, TEXT("%s"), *LogMessage);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

void FAnimTurnInPlaceState::UpdateActiveTurnTransition(float DeltaTime)
{
	check(ActiveTurnAnim);

	// Manually advance the turn transition animation so we can subtract the animated change in rotation from the current yaw offset.
	// TODO: This could be a lot simpler if we could leverage the root motion animation attribute somehow to consume the yaw offset.
	const float EndTime = ActiveTurnAnim->GetPlayLength();
	const float NewTime = FMath::Min(ActiveTurnAnimTime + DeltaTime, EndTime);

	if (NewTime > ActiveTurnAnimTime)
	{
		const FTransform AnimRootMotion = ActiveTurnAnim->ExtractRootMotionFromRange(ActiveTurnAnimTime, NewTime);
		const float AnimYaw = FMath::RadiansToDegrees(AnimRootMotion.GetRotation().GetTwistAngle(FVector::UpVector));

		ActiveTurnAnimTime = NewTime;
		RootYawOffset += AnimYaw;

		// Check if there's any rotation left in the animation.
		const FTransform AnimRemainingRootMotion = ActiveTurnAnim->ExtractRootMotionFromRange(NewTime, EndTime);
		const float AnimRemainingYaw = FMath::RadiansToDegrees(AnimRemainingRootMotion.GetRotation().GetTwistAngle(FVector::UpVector));
		if (FMath::IsNearlyZero(AnimRemainingYaw))
		{
			TurnRecoveryAnim = ActiveTurnAnim;
			TurnRecoveryAnimStartTime = ActiveTurnAnimTime;
			bTurnRecoveryRequested = true;
		}
	}
	// Turn transition animations should have some recovery time at the end. A possible workaround if they don't is to have an automatic transition
	// so the state machine doesn't get stuck in the turn transition state.
	else
	{
		ensureMsgf(false, TEXT("Reached end of turn transition without reaching the end of rotation. If the animation (%s) rotates until the end, an automatic transition back to idle is recommended."), *GetNameSafe(ActiveTurnAnim));
	}
}

void FAnimTurnInPlaceState::UpdateTurnTransitionTrigger(float DeltaTime, const FAnimTurnInPlaceAnimSet& AnimSet)
{
	const UAnimSequence* PrevPendingTurnAnim = PendingTurnAnim;

	const int32 BestTurnTransitionIndex = AnimSet.FindBestTurnTransitionIndex(RootYawOffset);
	if (BestTurnTransitionIndex != INDEX_NONE)
	{
		// If a turn transition is desired, handle the required delay before playing it. If a new turn transition is requested during
		// the delay (e.g. because the character keeps rotating), restart the delay.
		const FAnimTurnTransition& PendingTurnTransition = AnimSet.TurnTransitions[BestTurnTransitionIndex];
		PendingTurnAnim = PendingTurnTransition.Anim;
		PendingTurnDelayCounter = (PendingTurnAnim == PrevPendingTurnAnim) ? (PendingTurnDelayCounter + DeltaTime) : 0.f;

		if (PendingTurnDelayCounter >= PendingTurnTransition.DelayBeforeTrigger)
		{
			ActiveTurnAnim = PendingTurnAnim;
			ActiveTurnAnimTime = 0.f;
			bTurnTransitionRequested = true;
			PendingTurnAnim = nullptr;
		}
	}
	else
	{
		PendingTurnAnim = nullptr;
	}
}