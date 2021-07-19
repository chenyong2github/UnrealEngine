// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchPredictionTypes.h"
#include "Animation/AnimSequence.h"
#include "Algo/AllOf.h"


float FPredictionPlayRateAdjustment::ComputePlayRate(float PlayRate, float DeltaTime) const
{
	PlayRate = RemappingCurve ? RemappingCurve->GetFloatValue(PlayRate) : PlayRate;
	PlayRate = ScaleBiasClamp.ApplyTo(PlayRate, DeltaTime);
	return PlayRate;
}

bool FPredictionTrajectoryState::IsZeroSample() const
{
	return LocalLinearVelocity == FVector::ZeroVector && LocalLinearAcceleration == FVector::ZeroVector && Position == FVector::ZeroVector && AccumulatedDistance == 0.f;
}

FPredictionTrajectoryState FPredictionTrajectoryState::Lerp(const FPredictionTrajectoryState& A, const FPredictionTrajectoryState& B, float Alpha)
{
	FPredictionTrajectoryState State;
	State.AccumulatedDistance = FMath::Lerp(A.AccumulatedDistance, B.AccumulatedDistance, Alpha);
	State.LocalLinearAcceleration = FMath::Lerp(A.LocalLinearAcceleration, B.LocalLinearAcceleration, Alpha);
	State.LocalLinearVelocity = FMath::Lerp(A.LocalLinearVelocity, B.LocalLinearVelocity, Alpha);
	State.Position = FMath::Lerp(A.Position, B.Position, Alpha);
	return State;
}

bool FPredictionTrajectoryRange::HasSamples() const
{
	return !Samples.IsEmpty();
}

bool FPredictionTrajectoryRange::HasOnlyZeroSamples() const
{
	return Algo::AllOf(Samples, [](const FPredictionTrajectoryState& Sample) 
	{
		return Sample.IsZeroSample();
	});
}

bool FPredictionSequenceState::HasSequence() const
{
	return !SequenceBase.IsNull() && SequenceBase->IsA<UAnimSequence>();
}