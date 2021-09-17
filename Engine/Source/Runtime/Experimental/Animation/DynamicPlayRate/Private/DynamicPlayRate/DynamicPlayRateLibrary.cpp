// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicPlayRate/DynamicPlayRateLibrary.h"

#include "Algo/MinElement.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"

DEFINE_LOG_CATEGORY_STATIC(LogDynamicPlayRateLibrary, Verbose, All);

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarDynamicPlayRateDebug(TEXT("a.DynamicPlayRate.Debug"), 0, TEXT("Turn on debug for dynamic play rate adjustment"));
TAutoConsoleVariable<int32> CVarDynamicPlayRateEnable(TEXT("a.DynamicPlayRate.Enable"), 1, TEXT("Toggle dynamic play rate adjustment"));
#endif

float FDynamicPlayRateSettings::ComputePlayRate(float PlayRate, float DeltaTime) const
{
	return RemappingCurve ? RemappingCurve->GetFloatValue(PlayRate) : ScaleBiasClamp.ApplyTo(PlayRate, DeltaTime);
}

float DynamicPlayRateAdjustment(const FAnimationUpdateContext& Context
	, FTrajectorySampleRange Trajectory
	, const FDynamicPlayRateSettings& Settings
	, const UAnimSequenceBase* Sequence
	, float AccumulatedTime
	, float PlayRate
	, bool bLooping)
{
	if (!Settings.bEnabled)
	{
		return PlayRate;
	}

	const float DeltaTime = Context.GetDeltaTime();

#if ENABLE_ANIM_DEBUG
	// Debug enable/disable toggle for play rate scaling
	if (!CVarDynamicPlayRateEnable.GetValueOnAnyThread())
	{
		return PlayRate;
	}
#if WITH_EDITORONLY_DATA
	bool bDebugDraw = Settings.bDebugDraw || CVarDynamicPlayRateDebug.GetValueOnAnyThread();
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

	// Trajectory isn't being updated
	if (!Trajectory.HasSamples())
	{
		return PlayRate;
	}

	// Currently dynamic play rate adjustmenmt only takes into consideration present and future motion samples
	Trajectory.RemoveHistory();

	// Trajectory contains only zeroed samples
	if (Trajectory.HasOnlyZeroSamples())
	{
		return PlayRate;
	}

	// No sequence available to play rate scale
	if (!Sequence)
	{
		return PlayRate;
	}

	// Sequence Base pointer is not an actual sequence asset
	const UAnimSequence* InternalSequence = Cast<const UAnimSequence>(Sequence);
	if (!ensure(InternalSequence))
	{
		return PlayRate;
	}

	// Find the minima trajectory trajectory velocity
	// Approximate zeroed values may indicate the synchronization point for a stop or pivot
	const FTrajectorySample* MinimaSample = Algo::MinElementBy(Trajectory.Samples, [](const FTrajectorySample& Sample)
		{
			return Sample.LocalLinearVelocity.SizeSquared();
		});

	// Given a high-resolution time step, walk the current animation sequence to find a corresponding minima root motion delta
	// Changes in direction are considered extreme minima events, meaning no other subsequent minima is significant enough to take precedence
	float MinimaSampleTime = FLT_MAX;
	bool bPivotDetected = false;

	check(MinimaSample);
	check(Settings.RootMotionSampleRate > 0.f);

	for (auto [Idx, RootMotionSampleStep, PreviousDirection, MinimaDisplacement, CosOfPivotAngleThreshold] = std::tuple{ 0, 1.f / Settings.RootMotionSampleRate, FVector::ZeroVector, FLT_MAX, FMath::Cos(Settings.ZeroRootMotionAngleThreshold) };; ++Idx)
	{
		const float SampleTime = AccumulatedTime + static_cast<float>(Idx) * RootMotionSampleStep;

		if (SampleTime > InternalSequence->GetPlayLength())
		{
			break;
		}

		const FVector RootMotion = InternalSequence->ExtractRootMotion(SampleTime, RootMotionSampleStep, bLooping).GetTranslation();

		FVector RootMotionDirection;
		float RootMotionDisplacement;
		RootMotion.ToDirectionAndLength(RootMotionDirection, RootMotionDisplacement);

		// Found a smaller displacement in the root motion track
		if (RootMotionDisplacement <= MinimaDisplacement)
		{
			MinimaDisplacement = RootMotionDisplacement;
			MinimaSampleTime = SampleTime;
		}

		const float CosOfPotentialPivotAngle = RootMotionDirection.Dot(PreviousDirection);
		PreviousDirection = RootMotionDirection;

		// Significant changes in direction will be defined as a pivot
		// Hack: Unfortunately in practice, we may erroneously identify animations as a pivot when in fact they have malformed root motion tracks
		if (CosOfPotentialPivotAngle < CosOfPivotAngleThreshold && RootMotionDisplacement > Settings.ZeroRootMotionDisplacementError)
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
	const FVector MinimaRootMotionDelta = InternalSequence->ExtractRootMotion(MinimaSampleTime, DeltaTime, bLooping).GetTranslation();

	// Play rate scaling is root motion driven when a near zero root motion delta or pivot has been detected
	// Otherwise we are locomotion driven, which is reflected in the numerator of the divisor
	bool bRootMotionDrivenPlayRate = MinimaRootMotionDelta.IsNearlyZero(KINDA_SMALL_NUMBER) || bPivotDetected;
#if WITH_EDITORONLY_DATA
	FColor SynchronizationColor = FColor::Red;
#endif
	FVector RootMotionDelta = FVector::ZeroVector;

	// This loop allows for play rate scaling to apply correction in the event that we discover the animation and trajectory trajectory minima mismatch
	// Example: 
	//	If both the trajectory trajectory and chosen animation are decelerating to zero, minima driven play rate scaling can be correctly applied
	//  However if the chosen animation is not decelerating to zero -- such as Jog_Right vs Jog_Right_Stop -- then a mismatch has been detected
	//	and locomotion/instantaneous driven play rate scaling is attempted instead
	for (int32 MaxIterations = 1; MaxIterations >= 0; MaxIterations--)
	{
		// Minima driven play rate scaling synchronizes using the remaining displacement to near zero with: animation / locomotion
		// Locomotion driven play rate scaling synchronizes using the per-frame instantaneous displacement with: locomotion / animation
		const float SequenceDelta = bRootMotionDrivenPlayRate ? MinimaSampleTime - AccumulatedTime : DeltaTime;
		const float TrajectoryDisplacement = bRootMotionDrivenPlayRate ? MinimaSample->AccumulatedDistance : Trajectory.Samples[0].LocalLinearVelocity.Size() * DeltaTime;

		RootMotionDelta = InternalSequence->ExtractRootMotion(AccumulatedTime, SequenceDelta, bLooping).GetTranslation();
		const float RootMotionDeltaDisplacement = RootMotionDelta.Size();

		// Zero displacement is left in the animation, which may result in sliding if the trajectory minima has non-zero displacement
		const bool bZeroRootMotion = FMath::IsNearlyZero(RootMotionDeltaDisplacement, KINDA_SMALL_NUMBER);

		// Zero displacement is left in the trajectory minima, which may result in a pop or a pose break if the animation has non-zero displacement
		const bool bZeroTrajectory = FMath::IsNearlyZero(TrajectoryDisplacement, KINDA_SMALL_NUMBER);

		// Play rate scaling isn't require since no trajectory trajectory motion or root motion is present
		if (bZeroRootMotion && bZeroTrajectory)
		{
			PlayRate = FMath::Clamp(PlayRate, 0.f, 1.f);
			break;
		}
		// The computed minima in the root motion and trajectory mismatch, so flip the synchronization behavior as an attempt to correctly play rate scale
		// If this fails, the algorithm will automatically fall back to a default play rate of 1.f, which may introduce sliding.
		else if (bZeroTrajectory || bZeroRootMotion)
		{
			// There are cases where root motion may be available in the absence of predicted motion. We need to guarantee that the play rate 
			// will not superfluously reach extreme values due to bRootMotionDrivenPlayRate being enabled.
			if (bRootMotionDrivenPlayRate && FMath::IsNearlyZero(MinimaSample->AccumulatedDistance, KINDA_SMALL_NUMBER))
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
				? RootMotionDeltaDisplacement / TrajectoryDisplacement
				: TrajectoryDisplacement / RootMotionDeltaDisplacement;

			break;
		}
	}

#if WITH_EDITORONLY_DATA
	// Render the starting and ending trajectory trajectory positions for the distance matching play rate synchronization
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
	return Settings.ComputePlayRate(PlayRate, DeltaTime);
}

float UDynamicPlayRateLibrary::DynamicPlayRateAdjustment(const FAnimUpdateContext& UpdateContext
	, FTrajectorySampleRange Trajectory
	, const FDynamicPlayRateSettings& Settings
	, const UAnimSequenceBase* Sequence
	, float AccumulatedTime
	, float PlayRate
	, bool bLooping)
{
	return ::DynamicPlayRateAdjustment(*(UpdateContext.GetContext())
		, Trajectory
		, Settings
		, Sequence
		, AccumulatedTime
		, PlayRate
		, bLooping
	);
}