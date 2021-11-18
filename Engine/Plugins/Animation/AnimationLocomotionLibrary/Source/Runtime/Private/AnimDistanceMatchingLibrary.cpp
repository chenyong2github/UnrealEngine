// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDistanceMatchingLibrary.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Curves/CurveFloat.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnimDistanceMatchingLibrary, Verbose, All);

/**
 * Advance from the current time to a new time in the animation that will result in the desired distance traveled by the authored root motion.
 */
float GetTimeAfterDistanceTraveled(const UAnimSequence* AnimSequence, float CurrentTime, float DistanceTraveled, const FDistanceCurve& CachedDistanceCurve, const bool bAllowLooping)
{
	float NewTime = CurrentTime;
	if (AnimSequence != nullptr)
	{
		// Avoid infinite loops if the animation doesn't cover any distance.
		if (!FMath::IsNearlyZero(CachedDistanceCurve.GetDistanceRange(AnimSequence)))
		{
			float AccumulatedDistance = 0.f;

			const float SequenceLength = AnimSequence->GetPlayLength();
			const float StepTime = 1.f / 30.f;
			
			// Distance Matching expects the distance curve on the animation to increase monotonically. If the curve fails to increase in value
			// after a certain number of iterations, we abandon the algorithm to avoid an infinite loop.
			const int32 StuckLoopThreshold = 5;
			int32 StuckLoopCounter = 0;

			// Traverse the distance curve, accumulating animated distance until the desired distance is reached.
			while ((AccumulatedDistance < DistanceTraveled) && (bAllowLooping || (NewTime + StepTime < SequenceLength)))
			{
				const float CurrentDistance = CachedDistanceCurve.GetValueAtPosition(AnimSequence, NewTime);
				const float DistanceAfterStep = CachedDistanceCurve.GetValueAtPosition(AnimSequence, NewTime + StepTime);
				const float AnimationDistanceThisStep = DistanceAfterStep - CurrentDistance;

				if (!FMath::IsNearlyZero(AnimationDistanceThisStep))
				{
					// Keep advancing if the desired distance hasn't been reached.
					if (AccumulatedDistance + AnimationDistanceThisStep < DistanceTraveled)
					{
						FAnimationRuntime::AdvanceTime(bAllowLooping, StepTime, NewTime, SequenceLength);
						AccumulatedDistance += AnimationDistanceThisStep;
					}
					// Once the desired distance is passed, find the approximate time between samples where the distance will be reached.
					else
					{
						const float DistanceAlpha = (DistanceTraveled - AccumulatedDistance) / AnimationDistanceThisStep;
						FAnimationRuntime::AdvanceTime(bAllowLooping, DistanceAlpha * StepTime, NewTime, SequenceLength);
						AccumulatedDistance = DistanceTraveled;
						break;
					}

					StuckLoopCounter = 0;
				}
				else
				{
					++StuckLoopCounter;
					if (StuckLoopCounter >= StuckLoopThreshold)
					{
						UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Failed to advance any distance after %d loops on anim sequence (%s). Aborting."), StuckLoopThreshold, *GetNameSafe(AnimSequence));
						break;
					}
				}
			}
		}
		else
		{
			UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Anim sequence (%s) is missing a distance curve or doesn't cover enough distance for GetTimeAfterDistanceTraveled."), *GetNameSafe(AnimSequence));
		}
	}
	else
	{
		UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Invalid AnimSequence passed to GetTimeAfterDistanceTraveled"));
	}

	return NewTime;
}

FSequenceEvaluatorReference UAnimDistanceMatchingLibrary::AdvanceTimeByDistanceMatching(const FAnimUpdateContext& UpdateContext, const FSequenceEvaluatorReference& SequenceEvaluator,
	float DistanceTraveled, const FDistanceCurve& CachedDistanceCurve, FVector2D PlayRateClamp)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluator>(
		TEXT("AdvanceTimeByDistanceMatching"),
		[&UpdateContext, DistanceTraveled, &CachedDistanceCurve, PlayRateClamp](FAnimNode_SequenceEvaluator& InSequenceEvaluator)
		{
			if (const FAnimationUpdateContext* AnimationUpdateContext = UpdateContext.GetContext())
			{
				const float DeltaTime = AnimationUpdateContext->GetDeltaTime(); 

				if (DeltaTime > 0 && DistanceTraveled > 0)
				{
					if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(InSequenceEvaluator.GetSequence()))
					{
						const float CurrentTime = InSequenceEvaluator.GetExplicitTime();
						const float CurrentAssetLength = InSequenceEvaluator.GetCurrentAssetLength();
						const bool bAllowLooping = InSequenceEvaluator.GetShouldLoop();

						float TimeAfterDistanceTraveled = GetTimeAfterDistanceTraveled(AnimSequence, CurrentTime, DistanceTraveled, CachedDistanceCurve, bAllowLooping);

						// Calculate the effective playrate that would result from advancing the animation by the distance traveled.
						// Account for the animation looping.
						if (TimeAfterDistanceTraveled < CurrentTime)
						{
							TimeAfterDistanceTraveled += CurrentAssetLength;
						}
						float EffectivePlayRate = (TimeAfterDistanceTraveled - CurrentTime) / DeltaTime;

						// Clamp the effective play rate.
						if (PlayRateClamp.X >= 0.0f && PlayRateClamp.X < PlayRateClamp.Y)
						{
							EffectivePlayRate = FMath::Clamp(EffectivePlayRate, PlayRateClamp.X, PlayRateClamp.Y);
						}

						// Advance animation time by the effective play rate.
						float NewTime = CurrentTime;
						FAnimationRuntime::AdvanceTime(bAllowLooping, EffectivePlayRate * DeltaTime, NewTime, CurrentAssetLength);

						if (!InSequenceEvaluator.SetExplicitTime(NewTime))
						{
							UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Could not set explicit time on sequence evaluator, value is not dynamic. Set it as Always Dynamic."));
						}
					}
					else
					{
						UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Sequence evaluator does not have an anim sequence to play."));
					}
				}
			}
			else
			{
				UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("AdvanceTimeByDistanceMatching called with invalid context"));
			}
		});

	return SequenceEvaluator;
}

FSequenceEvaluatorReference UAnimDistanceMatchingLibrary::DistanceMatchToTarget(const FSequenceEvaluatorReference& SequenceEvaluator,
	float DistanceToTarget, const FDistanceCurve& CachedDistanceCurve)
{
	SequenceEvaluator.CallAnimNodeFunction<FAnimNode_SequenceEvaluator>(
		TEXT("DistanceMatchToTarget"),
		[DistanceToTarget, &CachedDistanceCurve](FAnimNode_SequenceEvaluator& InSequenceEvaluator)
		{
			if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(InSequenceEvaluator.GetSequence()))
			{
				if (CachedDistanceCurve.IsValid(AnimSequence))
				{
					// By convention, distance curves store the distance to a target as a negative value.
					const float NewTime = CachedDistanceCurve.GetAnimPositionFromDistance(AnimSequence, -DistanceToTarget);
					if (!InSequenceEvaluator.SetExplicitTime(NewTime))
					{
						UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Could not set explicit time on sequence evaluator, value is not dynamic. Set it as Always Dynamic."));
					}
				}
				else
				{
					UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("DistanceMatchToTarget called with invalid CachedDistanceCurve or animation (%s) is missing a distance curve."), *GetNameSafe(AnimSequence));
				}
			}
			else
			{
				UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Sequence evaluator does not have an anim sequence to play."));
			}
			
		});

	return SequenceEvaluator;
}

FSequencePlayerReference UAnimDistanceMatchingLibrary::SetPlayrateToMatchSpeed(const FSequencePlayerReference& SequencePlayer, float SpeedToMatch, FVector2D PlayRateClamp)
{
	SequencePlayer.CallAnimNodeFunction<FAnimNode_SequencePlayer>(
		TEXT("SetPlayrateToMatchSpeed"),
		[SpeedToMatch, PlayRateClamp](FAnimNode_SequencePlayer& InSequencePlayer)
		{
			if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(InSequencePlayer.GetSequence()))
			{
				const float AnimLength = AnimSequence->GetPlayLength();
				if (!FMath::IsNearlyZero(AnimLength))
				{
					// Calculate the speed as: (distance traveled by the animation) / (length of the animation)
					const FVector RootMotionTranslation = AnimSequence->ExtractRootMotionFromRange(0.0f, AnimLength).GetTranslation();
					const float RootMotionDistance = RootMotionTranslation.Size2D();
					if (!FMath::IsNearlyZero(RootMotionDistance))
					{
						const float AnimationSpeed = RootMotionDistance / AnimLength;
						float DesiredPlayRate = SpeedToMatch / AnimationSpeed;
						if (PlayRateClamp.X >= 0.0f && PlayRateClamp.X < PlayRateClamp.Y)
						{
							DesiredPlayRate = FMath::Clamp(DesiredPlayRate, PlayRateClamp.X, PlayRateClamp.Y);
						}

						if (!InSequencePlayer.SetPlayRate(DesiredPlayRate))
						{
							UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Could not set play rate on sequence player, value is not dynamic. Set it as Always Dynamic."));
						}
					}
					else
					{
						UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Unable to adjust playrate for animation with no root motion delta (%s)."), *GetNameSafe(AnimSequence));
					}
				}
				else
				{
					UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Unable to adjust playrate for zero length animation (%s)."), *GetNameSafe(AnimSequence));
				}
			}
			else
			{
				UE_LOG(LogAnimDistanceMatchingLibrary, Warning, TEXT("Sequence player does not have an anim sequence to play."));
			}
		});

	return SequencePlayer;
}