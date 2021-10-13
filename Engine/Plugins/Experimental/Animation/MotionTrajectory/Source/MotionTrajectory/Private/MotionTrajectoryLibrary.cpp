// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrajectoryLibrary.h"
#include "GameFramework/Actor.h"
#include "Algo/Find.h"
#include "GameFramework/Actor.h"

static void FlattenTrajectoryPosition(FTrajectorySample& Sample, float& AccumulatedDeltaDistance, bool PreserveSpeed)
{
	if (!Sample.Position.IsZero())
	{
		if (PreserveSpeed)
		{
			// Take the full displacement, effectively meaning that the Z axis never existed
			Sample.Position = Sample.Position.Size() * Sample.Position.GetSafeNormal2D();
		}
		else
		{
			// Accumulate the delta displacement difference as a result of Z axis being removed
			const float DeltaDistance = Sample.Position.Size() - Sample.Position.Size2D();
			AccumulatedDeltaDistance += DeltaDistance;

			// Remove the accumulated delta displacement from each sample on the timeline, and recompute the new projected position
			Sample.AccumulatedDistance -= AccumulatedDeltaDistance;
			Sample.Position = FMath::Abs(Sample.AccumulatedDistance) * Sample.Position.GetSafeNormal2D();
		}
	}
}

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::FlattenTrajectory2D(FTrajectorySampleRange Trajectory, bool PreserveSpeed)
{
	if (!Trajectory.HasSamples())
	{
		return Trajectory;
	}

	if (Trajectory.HasOnlyZeroSamples())
	{
		return Trajectory;
	}

	// Each iteration will preserve the linear magnitudes of velocity and acceleration while removing the direction Z-axis component
	for (auto& Sample : Trajectory.Samples)
	{
		// Note: As as a consequence of magnitude preservation, AccumulatedDistance alongside AccumulatedTime should not need modification

		// Linear velocity Z-axis component removal
		if (!Sample.LocalLinearVelocity.IsZero())
		{
			const float VelMagnitude = PreserveSpeed ? Sample.LocalLinearVelocity.Size() : Sample.LocalLinearVelocity.Size2D();
			Sample.LocalLinearVelocity = VelMagnitude * Sample.LocalLinearVelocity.GetSafeNormal2D();
		}

		// Align linear acceleration via projection onto velocity
		if (!Sample.LocalLinearVelocity.IsZero() && !Sample.LocalLinearAcceleration.IsZero())
		{
			const float AccelMagnitude = PreserveSpeed ? Sample.LocalLinearAcceleration.Size() : Sample.LocalLinearAcceleration.Size2D();
			Sample.LocalLinearAcceleration = AccelMagnitude * Sample.LocalLinearAcceleration.ProjectOnTo(Sample.LocalLinearVelocity).GetSafeNormal();
		}
	}

	// The present position sample is used as the basis for recomputing the future and history Accumulated Distance
	const int32 PresentSampleIdx = Trajectory.Samples.IndexOfByPredicate([](const FTrajectorySample& Sample){
		return Sample.AccumulatedSeconds == 0.f;
	});

	check(PresentSampleIdx != INDEX_NONE);

	// Walk all samples into the future, conditionally removing contribution of Z axis motion
	float AccumulatedDeltaDistance = 0.f;
	for (int32 Idx = PresentSampleIdx, Num = Trajectory.Samples.Num(); Idx < Num; ++Idx)
	{
		FlattenTrajectoryPosition(Trajectory.Samples[Idx], AccumulatedDeltaDistance, PreserveSpeed);
	}

	// There is a possibility history has not been computed yet
	if (PresentSampleIdx == 0)
	{
		return Trajectory;
	}

	// Walk all samples in the past, conditionally removing the contribution of Z axis motion
	AccumulatedDeltaDistance = 0.f;
	for (int32 Idx = PresentSampleIdx - 1, Begin = 0; Idx >= Begin; --Idx)
	{
		FlattenTrajectoryPosition(Trajectory.Samples[Idx], AccumulatedDeltaDistance, PreserveSpeed);
	}

	return Trajectory;
}

static FVector ClampDirection(const FVector InputVector, const TArray<FTrajectoryDirectionClamp>& Directions)
{
	if (Directions.IsEmpty())
	{
		return InputVector;
	}

	FVector InputDirection;
	float InputLength;
	InputVector.ToDirectionAndLength(InputDirection, InputLength);
	if (InputLength < SMALL_NUMBER)
	{
		return InputVector;
	}

	// Assume first direction is best then check if the input direction is within the remaining sectors
	FVector NearestDirection = Directions[0].Direction;
	for (int32 DirIdx = 1; DirIdx != Directions.Num(); ++DirIdx)
	{
		const auto& Clamp = Directions[DirIdx];
		if (FMath::Acos(FVector::DotProduct(InputDirection, Clamp.Direction)) < FMath::DegreesToRadians(Clamp.AngleTresholdDegrees))
		{
			NearestDirection = Clamp.Direction;
			break;
		}
	}

	const FVector Output = InputLength * NearestDirection;
	return Output;
}

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::ClampTrajectoryDirection(FTrajectorySampleRange Trajectory, const TArray<FTrajectoryDirectionClamp>& Directions)
{
	if (Directions.IsEmpty())
	{
		return Trajectory;
	}

	if (!Trajectory.HasSamples())
	{
		return Trajectory;
	}

	if (Trajectory.HasOnlyZeroSamples())
	{
		return Trajectory;
	}

	// The clamped present (zero domain) sample is used as the basis for projecting samples along its trajectory
	FTrajectorySample* PresentSample = Algo::FindByPredicate(Trajectory.Samples, [](const FTrajectorySample& Sample) {
		return FMath::IsNearlyZero(Sample.AccumulatedSeconds);
	}
	);

	check(PresentSample)

	if (!PresentSample->LocalLinearVelocity.IsZero())
	{
		const FVector VelocityBasis = ClampDirection(PresentSample->LocalLinearVelocity, Directions).GetSafeNormal();

		for (auto& Sample : Trajectory.Samples)
		{
			// Align linear velocity onto the velocity basis to maintain the present intended direction, while retaining per-sample magnitude
			if (!Sample.LocalLinearVelocity.IsZero())
			{
				Sample.LocalLinearVelocity = Sample.LocalLinearVelocity.Size() * Sample.LocalLinearVelocity.ProjectOnTo(VelocityBasis).GetSafeNormal();
			}

			// Align linear acceleration through projection onto the modified velocity
			if (!Sample.LocalLinearVelocity.IsZero() && !Sample.LocalLinearAcceleration.IsZero())
			{
				Sample.LocalLinearAcceleration = Sample.LocalLinearAcceleration.Size() * Sample.LocalLinearAcceleration.ProjectOnTo(Sample.LocalLinearVelocity).GetSafeNormal();
			}

			// Align the position path through projection onto the modified velocity
			if (!Sample.LocalLinearVelocity.IsZero() && !Sample.Position.IsZero())
			{
				Sample.Position = FMath::Abs(Sample.AccumulatedDistance) * Sample.Position.ProjectOnTo(Sample.LocalLinearVelocity).GetSafeNormal();
			}
		}
	}

	return Trajectory;
}

void UMotionTrajectoryBlueprintLibrary::DebugDrawTrajectory(const AActor* Actor
	, const FTransform& WorldTransform
	, const FTrajectorySampleRange& Trajectory
	, const FLinearColor PredictionColor
	, const FLinearColor HistoryColor
	, float ArrowScale
	, float ArrowSize
	, float ArrowThickness
)
{
	if (Actor)
	{
		Trajectory.DebugDrawTrajectory(true
			, Actor->GetWorld()
			, WorldTransform.IsValid() ? WorldTransform : FTransform::Identity
			, PredictionColor
			, HistoryColor
			, ArrowScale
			, ArrowSize
			, ArrowThickness
		);
	}
}