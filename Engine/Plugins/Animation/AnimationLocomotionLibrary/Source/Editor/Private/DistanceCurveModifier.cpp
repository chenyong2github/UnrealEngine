// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistanceCurveModifier.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"

// TODO: This logic works decently for simple clips but it should be reworked to be more robust:
//  * It could detect pivot points by change in direction.
//  * It should also account for clips that have multiple stop/pivot points.
//  * It should handle distance traveled for the ends of looping animations.
void UDistanceCurveModifier::OnApply_Implementation(UAnimSequence* Animation)
{
	if (Animation == nullptr)
	{
		UE_LOG(LogAnimation, Error, TEXT("DistanceCurveModifier failed. Reason: Invalid Animation"));
		return;
	}

	if (!Animation->HasRootMotion())
	{
		UE_LOG(LogAnimation, Error, TEXT("DistanceCurveModifier failed. Reason: Root motion is disabled on the animation (%s)"), *GetNameSafe(Animation));
		return;
	}

	const bool bMetaDataCurve = false;
	UAnimationBlueprintLibrary::AddCurve(Animation, CurveName, ERawCurveTrackTypes::RCT_Float, bMetaDataCurve);

	const float AnimLength = Animation->GetPlayLength();

	// Perform a high resolution search to find the sample point with minimum speed.
	
	float TimeOfMinSpeed = 0.f;
	float MinSpeedSq = FMath::Square(StopSpeedThreshold);

	float SampleInterval = 1.f / 120.f;
	int32 NumSteps = AnimLength / SampleInterval;
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float Time = Step * SampleInterval;

		const bool bAllowLooping = false;
		const FVector RootMotionTranslation = Animation->ExtractRootMotion(Time, SampleInterval, bAllowLooping).GetTranslation();
		const float RootMotionSpeedSq = RootMotionTranslation.SizeSquared2D() / SampleInterval;

		if (RootMotionSpeedSq < MinSpeedSq)
		{
			MinSpeedSq = RootMotionSpeedSq;
			TimeOfMinSpeed = Time;
		}
	}

	SampleInterval = 1.f / SampleRate;
	NumSteps = FMath::CeilToInt(AnimLength / SampleInterval);
	float Time = 0.0f;
	for (int32 Step = 0; Step <= NumSteps && Time < AnimLength; ++Step)
	{
		Time = FMath::Min(Step * SampleInterval, AnimLength);

		// Assume that during any time before the stop/pivot point, the animation is approaching that point.
		// TODO: This works for clips that are broken into starts/stops/pivots, but needs to be rethought for more complex clips.
		const float ValueSign = (Time < TimeOfMinSpeed) ? -1.0f : 1.0f;

		const FVector RootMotionTranslation = Animation->ExtractRootMotionFromRange(TimeOfMinSpeed, Time).GetTranslation();
		UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, CurveName, Time, ValueSign * RootMotionTranslation.Size2D());
	}
}

void UDistanceCurveModifier::OnRevert_Implementation(UAnimSequence* Animation)
{
	const bool bRemoveNameFromSkeleton = false;
	UAnimationBlueprintLibrary::RemoveCurve(Animation, CurveName, bRemoveNameFromSkeleton);
}