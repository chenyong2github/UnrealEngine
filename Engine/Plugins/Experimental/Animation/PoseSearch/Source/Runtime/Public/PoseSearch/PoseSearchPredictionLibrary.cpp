// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchPredictionLibrary.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Algo/MinElement.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarAnimNodePredictionPlayRateDebug(TEXT("a.AnimNode.PredictionPlayRate.Debug"), 0, TEXT("Turn on debug for trajectory prediction play rate scaling"));
TAutoConsoleVariable<int32> CVarAnimNodePredictionPlayRateEnable(TEXT("a.AnimNode.PredictionPlayRate.Enable"), 1, TEXT("Toggle trajectory prediction play rate scaling"));
#endif

float UPoseSearchPredictionDistanceMatching::ComputePlayRate(const FAnimationUpdateContext& Context
	, const FPredictionTrajectoryRange& TrajectoryRange
	, const FPredictionTrajectorySettings& Settings
	, const FPredictionSequenceState& SequenceState)
{
	const float DeltaTime = Context.GetDeltaTime();
	float PlayRate = 1.f;

#if ENABLE_ANIM_DEBUG
	// Debug enable/disable toggle for play rate scaling
	if (!CVarAnimNodePredictionPlayRateEnable.GetValueOnAnyThread())
	{
		return PlayRate;
	}
#if WITH_EDITORONLY_DATA
	bool bDebugDraw = Settings.bDebugDraw || CVarAnimNodePredictionPlayRateDebug.GetValueOnAnyThread();
#endif
#else
#if WITH_EDITORONLY_DATA
	bool bDebugDraw = Settings.bDebugDraw;
#endif
#endif
#if WITH_EDITORONLY_DATA
	if (bDebugDraw)
	{
		Context.AnimInstanceProxy->AnimDrawDebugSphere(Context.AnimInstanceProxy->GetComponentTransform().GetLocation(), 8.f, 16, FColor::Green);
	}
#endif
	// Delta time is not progressing
	if (FMath::IsNearlyZero(DeltaTime, SMALL_NUMBER))
	{
		return PlayRate;
	}

	// Prediction range isn't being updated
	if (!TrajectoryRange.HasSamples())
	{
		return PlayRate;
	}

	// Prediction range contains only zeroed samples
	if (TrajectoryRange.HasOnlyZeroSamples())
	{
		return PlayRate;
	}

	// No sequence available to play rate scale
	if (!SequenceState.HasSequence())
	{
		return PlayRate;
	}

	PlayRate = SequenceState.PlayRate;

	// Find the minima prediction trajectory velocity
	// Approximate zeroed values may indicate the synchronization point for a stop or pivot
	const FPredictionTrajectoryState* MinimaSample = Algo::MinElementBy(TrajectoryRange.Samples, [](const FPredictionTrajectoryState& Sample)
	{
		return Sample.LocalLinearVelocity.SizeSquared();
	});

	check(MinimaSample);
	const UAnimSequence* Sequence = Cast<const UAnimSequence>(SequenceState.SequenceBase);
	check(Sequence);

	// Given a high-resolution time step, walk the current animation sequence to find a corresponding minima root motion delta
	// Changes in direction are considered extreme minima events, meaning no other subsequent minima is significant enough to take precedence
	float MinimaSampleTime = FLT_MAX;
	bool bPivotDetected = false;

	for (auto [SampleTime, RootMotionSampleStep, PreviousDirection, MinimaDisplacement, CosPivotAngleThreshold] = std::tuple{ (double)SequenceState.AccumulatedTime, 1.f / Settings.RootMotionSampleStepPerSecond, FVector::ZeroVector, FLT_MAX, FMath::Cos(Settings.ZeroRootMotionAngleThreshold) };
		SampleTime <= Sequence->GetPlayLength(); 
		SampleTime += RootMotionSampleStep)
	{
		FVector RootMotionDirection;
		float RootMotionDisplacement;

		const FVector RootMotion = Sequence->ExtractRootMotion(SampleTime, RootMotionSampleStep, SequenceState.bLooping).GetTranslation();
		RootMotion.ToDirectionAndLength(RootMotionDirection, RootMotionDisplacement);

		// Found a smaller displacement in the root motion track
		if (RootMotionDisplacement <= MinimaDisplacement)
		{
			MinimaDisplacement = RootMotionDisplacement;
			MinimaSampleTime = SampleTime;
		}

		const float PotentialPivotAngle = RootMotionDirection.Dot(PreviousDirection);
		PreviousDirection = RootMotionDirection;

		// Significant changes in direction will be defined as a pivot
		// Hack: Unfortunately in practice, we may erroneously identify animations as a pivot when in fact they have malformed root motion tracks
		if (PotentialPivotAngle < CosPivotAngleThreshold && RootMotionDisplacement > Settings.ZeroRootMotionDisplacementError)
		{
			// Bias the minima sample time for pivots to favor the pre-pivot phase (moment prior to direction change)
			MinimaSampleTime = SampleTime - RootMotionSampleStep;
			bPivotDetected = true;
			break;
		}
	}

	// We should always be able to find a minima, however a few situations could lead to this firing
	// 1) If the root motion sampling time step is not high enough resolution, we may not be able to sample the
	//    track near the end of a non-looping sequence
	// 2) We may be sampling at time 'sequence length' of a non-looping sequence
	if (MinimaSampleTime == FLT_MAX)
	{
		return PlayRate;
	}

	// Extrapolate the minima forward in time to detect a potential complete loss in velocity
	const FVector MinimaRootMotionDelta = Sequence->ExtractRootMotion(MinimaSampleTime, DeltaTime, SequenceState.bLooping).GetTranslation();

	// Play rate scaling is root motion driven when a near zero root motion delta or pivot has been detected
	// Otherwise we are locomotion driven, which is reflected in the numerator of the divisor
	bool bRootMotionDrivenPlayRate = MinimaRootMotionDelta.IsNearlyZero(KINDA_SMALL_NUMBER) || bPivotDetected;
#if WITH_EDITORONLY_DATA
	FColor SynchronizationColor = FColor::Red;
#endif
	FVector RootMotionDelta = FVector::ZeroVector;

	// This loop allows for play rate scaling to apply correction in the event that we discover the animation and trajectory prediction minima mismatch
	// Example: 
	//	If both the trajectory prediction and chosen animation are decelerating to zero, minima driven play rate scaling can be correctly applied
	//  However if the chosen animation is not decelerating to zero -- such as Jog_Right vs Jog_Right_Stop -- then a mismatch has been detected
	//	and locomotion/instantaneous driven play rate scaling is attempted instead
	for (int MaxIterations = 1; MaxIterations >= 0; MaxIterations--)
	{
		// Minima driven play rate scaling synchronizes using the remaining displacement to near zero with: animation / locomotion
		// Locomotion driven play rate scaling synchronizes using the per-frame instantaneous displacement with: locomotion / animation
		const float SequenceDelta = bRootMotionDrivenPlayRate ? MinimaSampleTime - SequenceState.AccumulatedTime : DeltaTime;
		const float PredictionDisplacement = bRootMotionDrivenPlayRate ? MinimaSample->AccumulatedDistance : TrajectoryRange.Samples[0].LocalLinearVelocity.Size2D() * DeltaTime;

		RootMotionDelta = Sequence->ExtractRootMotion(SequenceState.AccumulatedTime, SequenceDelta, SequenceState.bLooping).GetTranslation();
		const float RootMotionDeltaDisplacement = RootMotionDelta.Size2D();

		// Zero displacement is left in the animation, which may result in sliding if the prediction minima has non-zero displacement
		const bool bZeroRootMotion = FMath::IsNearlyZero(RootMotionDeltaDisplacement, KINDA_SMALL_NUMBER);

		// Zero displacement is left in the prediction minima, which may result in a pop or a pose break if the animation has non-zero displacement
		const bool bZeroPrediction = FMath::IsNearlyZero(PredictionDisplacement, KINDA_SMALL_NUMBER);

		// Play rate scaling isn't require since no trajectory prediction motion or root motion is present
		if (bZeroRootMotion && bZeroPrediction)
		{
			PlayRate = FMath::Clamp(PlayRate, 0.f, 1.f);
			break;
		}
		// The computed minima in the root motion and prediction mismatch, so flip the synchronization behavior as an attempt to correctly play rate scale
		// If this fails, the algorithm will automatically fall back to a default play rate of 1.f, which may introduce sliding.
		else if (bZeroPrediction || bZeroRootMotion)
		{
			// There are cases where root motion may be available in the absence of predicted motion. We need to guarantee that the play rate 
			// will not superfluously reach extreme values due to bRootMotionDrivenPlayRate being enabled.
			if (bRootMotionDrivenPlayRate && MinimaSample->IsZeroSample())
			{
				PlayRate = FMath::Clamp(PlayRate, 0.f, 1.f);
				break;
			}

			bRootMotionDrivenPlayRate = !bRootMotionDrivenPlayRate;
			continue;
		}
		// Play rate scaling succeeded
		else
		{
#if WITH_EDITORONLY_DATA
			SynchronizationColor = bRootMotionDrivenPlayRate ? FColor::Purple : FColor::Blue;
#endif
			PlayRate = bRootMotionDrivenPlayRate
				? RootMotionDeltaDisplacement / PredictionDisplacement 
				: PredictionDisplacement / RootMotionDeltaDisplacement;

			break;
		}
	}

#if WITH_EDITORONLY_DATA
	// Render the starting and ending trajectory prediction positions for the distance matching play rate synchronization
	if (bDebugDraw)
	{
		Context.AnimInstanceProxy->AnimDrawDebugSphere(Context.AnimInstanceProxy->GetComponentTransform().TransformPosition(RootMotionDelta), 8.f, 16, SynchronizationColor);

		if (bRootMotionDrivenPlayRate)
		{
			Context.AnimInstanceProxy->AnimDrawDebugSphere(Context.AnimInstanceProxy->GetComponentTransform().TransformPosition(MinimaSample->Position), 8.f, 16, FColor::Yellow);
		}
	}
#endif

	// Optionally remap the computed play rate against a curve
	return Settings.PlayRateAdjustment.ComputePlayRate(PlayRate, DeltaTime);
}