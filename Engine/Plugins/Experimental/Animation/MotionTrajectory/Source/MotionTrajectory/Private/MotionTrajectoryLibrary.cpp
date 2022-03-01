// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrajectoryLibrary.h"
#include "MotionTrajectory.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Algo/Find.h"
#include "GameFramework/Actor.h"

static void FlattenTrajectoryPosition(
	FTrajectorySample& Sample, 
	const FTrajectorySample& PrevSample, 
	const FTrajectorySample& FlattenedPrevSample, 
	bool PreserveSpeed)
{
	if (!Sample.Transform.GetTranslation().IsZero())
	{
		const FVector Translation = Sample.Transform.GetLocation() - PrevSample.Transform.GetLocation();
		const FVector FlattenedTranslation = FVector(Translation.X, Translation.Y, 0.0f);

		if (PreserveSpeed)
		{
			const float TargetDistance = 
				FMath::Abs(Sample.AccumulatedDistance - PrevSample.AccumulatedDistance);
			const FVector FlattenedTranslationDir = Translation.GetSafeNormal2D();

			// Take the full displacement, effectively meaning that the Z axis never existed
			Sample.Transform.SetLocation(
				FlattenedPrevSample.Transform.GetLocation() + (FlattenedTranslationDir * TargetDistance));
		}
		else
		{
			// Accumulate the delta displacement difference as a result of Z axis being removed
			const float DeltaSeconds = Sample.AccumulatedSeconds - PrevSample.AccumulatedSeconds;
			const float DeltaDistance = DeltaSeconds >= 0.0f ?
				FlattenedTranslation.Size() :
				-FlattenedTranslation.Size();

			Sample.AccumulatedDistance = FlattenedPrevSample.AccumulatedDistance + DeltaDistance;
			Sample.Transform.SetTranslation(FlattenedPrevSample.Transform.GetLocation() + FlattenedTranslation);
		}
	}
}

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::FlattenTrajectory2D(FTrajectorySampleRange Trajectory, bool bPreserveSpeed)
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
		if (!Sample.LinearVelocity.IsZero())
		{
			const float VelMagnitude = bPreserveSpeed ? Sample.LinearVelocity.Size() : Sample.LinearVelocity.Size2D();
			Sample.LinearVelocity = VelMagnitude * Sample.LinearVelocity.GetSafeNormal2D();
		}
	}

	// The present position sample is used as the basis for recomputing the future and history Accumulated Distance
	const int32 PresentSampleIdx = Trajectory.Samples.IndexOfByPredicate([](const FTrajectorySample& Sample){
		return Sample.AccumulatedSeconds == 0.f;
	});

	check(PresentSampleIdx != INDEX_NONE);

	// the present location should be zero but let's, for sanity, assume it might not be
	FTrajectorySample& PresentSample = Trajectory.Samples[PresentSampleIdx];
	const FVector PresentSampleLocation = PresentSample.Transform.GetLocation();
	PresentSample.Transform.SetLocation(FVector(PresentSampleLocation.X, PresentSampleLocation.Y, 0.0f));

	// Walk all samples into the future, conditionally removing contribution of Z axis motion
	FTrajectorySample PrevSample = PresentSample;
	for (int32 Idx = PresentSampleIdx + 1, Num = Trajectory.Samples.Num(); Idx < Num; ++Idx)
	{
		FTrajectorySample CurrentSample = Trajectory.Samples[Idx];
		FlattenTrajectoryPosition(Trajectory.Samples[Idx], PrevSample, Trajectory.Samples[Idx - 1], bPreserveSpeed);
		PrevSample = CurrentSample;
	}

	// There is a possibility history has not been computed yet
	if (PresentSampleIdx == 0)
	{
		return Trajectory;
	}

	// Walk all samples in the past, conditionally removing the contribution of Z axis motion
	PrevSample = PresentSample;
	for (int32 Idx = PresentSampleIdx - 1, Begin = 0; Idx >= Begin; --Idx)
	{
		FTrajectorySample CurrentSample = Trajectory.Samples[Idx];
		FlattenTrajectoryPosition(Trajectory.Samples[Idx], PrevSample, Trajectory.Samples[Idx + 1], bPreserveSpeed);
		PrevSample = CurrentSample;
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

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::ClampTrajectoryDirection(FTrajectorySampleRange Trajectory, const TArray<FTrajectoryDirectionClamp>& Directions, bool bPreserveRotation)
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

	if (!PresentSample->LinearVelocity.IsZero())
	{
		const FVector VelocityBasis = ClampDirection(PresentSample->LinearVelocity, Directions).GetSafeNormal();

		for (auto& Sample : Trajectory.Samples)
		{
			// Align linear velocity onto the velocity basis to maintain the present intended direction, while retaining per-sample magnitude
			if (!Sample.LinearVelocity.IsZero())
			{
				Sample.LinearVelocity = Sample.LinearVelocity.Size() * Sample.LinearVelocity.ProjectOnTo(VelocityBasis).GetSafeNormal();
			}

			// Align the position path through projection onto the modified velocity
			if (!Sample.LinearVelocity.IsZero() && !Sample.Transform.GetLocation().IsZero())
			{
				Sample.Transform.SetLocation(
					FMath::Abs(Sample.AccumulatedDistance) * 
					Sample.Transform.GetLocation().ProjectOnTo(Sample.LinearVelocity).GetSafeNormal());
			}

			if (bPreserveRotation)
			{
				Sample.Transform.SetRotation(PresentSample->Transform.GetRotation());
			}
		}
	}

	return Trajectory;
}

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::RotateTrajectory(
	FTrajectorySampleRange Trajectory, 
	const FQuat& Rotation)
{
	Trajectory.Rotate(Rotation);
	return Trajectory;
}

FTrajectorySampleRange UMotionTrajectoryBlueprintLibrary::MakeTrajectoryRelativeToComponent(
	FTrajectorySampleRange ActorTrajectory, 
	const USceneComponent* Component)
{
	if (!IsValid(Component))
	{
		UE_LOG(LogMotionTrajectory, Error, TEXT("Invalid component!"));
		return ActorTrajectory;
	}

	const AActor* Owner = Component->GetOwner();
	const FTransform OwnerTransformWS = Owner->GetActorTransform();
	const FTransform ComponentTransformWS = Component->GetComponentTransform();
	const FTransform ReferenceChangeTransform = OwnerTransformWS.GetRelativeTransform(ComponentTransformWS);

	ActorTrajectory.TransformReferenceFrame(ReferenceChangeTransform);
	return ActorTrajectory;
}

void UMotionTrajectoryBlueprintLibrary::DebugDrawTrajectory(const AActor* Actor
	, const FTransform& WorldTransform
	, const FTrajectorySampleRange& Trajectory
	, const FLinearColor PredictionColor
	, const FLinearColor HistoryColor
	, float TransformScale
	, float TransformThickness
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
			, TransformScale
			, TransformThickness
			, ArrowScale
			, ArrowSize
			, ArrowThickness
		);
	}
}

bool UMotionTrajectoryBlueprintLibrary::IsStoppingTrajectory(
	const FTrajectorySampleRange& Trajectory, 
	float MoveMinSpeed,
	float IdleMaxSpeed)
{
	if (Trajectory.Samples.Num() > 0)
	{
		const FVector LastLinearVelocity = Trajectory.Samples.Last().LinearVelocity;
		const float SquaredLastLinearSpeed = LastLinearVelocity.SquaredLength();
		
		int StartIdx = 0;
		const FTrajectorySample& PresentSample = FTrajectorySampleRange::IterSampleTrajectory(
			Trajectory.Samples, 
			ETrajectorySampleDomain::Time, 
			0.0f, 
			StartIdx);
		const FVector PresenLinearVelocity = PresentSample.LinearVelocity;
		const float SquaredPresentLinearSpeed = PresenLinearVelocity.SquaredLength();

		const bool bIsStopping =
			(SquaredPresentLinearSpeed >= (MoveMinSpeed * MoveMinSpeed)) &&
			(SquaredLastLinearSpeed <= (IdleMaxSpeed * IdleMaxSpeed));

		return bIsStopping;
	}

	return false;
}

bool UMotionTrajectoryBlueprintLibrary::IsStartingTrajectory(
	const FTrajectorySampleRange& Trajectory, 
	float MoveMinSpeed,
	float IdleMaxSpeed)
{
	if (Trajectory.Samples.Num() > 0)
	{
		const FVector FirstLinearVelocity = Trajectory.Samples[0].LinearVelocity;
		const float SquaredFirstLinearSpeed = FirstLinearVelocity.SquaredLength();

		int StartIdx = 0;
		const FTrajectorySample& PresentSample = FTrajectorySampleRange::IterSampleTrajectory(
			Trajectory.Samples,
			ETrajectorySampleDomain::Time,
			0.0f,
			StartIdx);
		const FVector PresentLinearVelocity = PresentSample.LinearVelocity;
		const float SquaredPresentLinearSpeed = PresentLinearVelocity.SquaredLength();

		const bool IsStarting =
			(SquaredPresentLinearSpeed >= (MoveMinSpeed * MoveMinSpeed)) &&
			(SquaredFirstLinearSpeed <= (IdleMaxSpeed * IdleMaxSpeed));

		return IsStarting;
	}

	return false;
}


bool UMotionTrajectoryBlueprintLibrary::IsConstantSpeedTrajectory(
	const FTrajectorySampleRange& Trajectory,
	float Speed,
	float Tolerance)
{
	if (Trajectory.Samples.Num() > 0)
	{
		const float MinSpeed = FMath::Max(Speed - Tolerance, 0.0f);
		const float SquaredMinSpeed = MinSpeed * MinSpeed;
		const float MaxSpeed = FMath::Max(Speed + Tolerance, 0.0f);
		const float SquaredMaxSpeed = MaxSpeed * MaxSpeed;

		auto IsWithinLimit = [=](float SquaredSpeed)
		{
			return SquaredSpeed >= SquaredMinSpeed && SquaredSpeed <= SquaredMaxSpeed;
		};

		const FVector LastLinearVelocity = Trajectory.Samples.Last().LinearVelocity;
		const float SquaredLastLinearSpeed = LastLinearVelocity.SquaredLength();
		const bool LastSpeedWithinLimit = IsWithinLimit(SquaredLastLinearSpeed);

		int StartIdx = 0;
		const FTrajectorySample& PresentSample = FTrajectorySampleRange::IterSampleTrajectory(
			Trajectory.Samples,
			ETrajectorySampleDomain::Time,
			0.0f,
			StartIdx);
		const FVector PresentLinearVelocity = PresentSample.LinearVelocity;
		const float SquaredPresentLinearSpeed = PresentLinearVelocity.SquaredLength();
		const bool PresentSpeedWithinLimit = IsWithinLimit(SquaredLastLinearSpeed);

		const bool bIsAtSpeed = LastSpeedWithinLimit && PresentSpeedWithinLimit;

		return bIsAtSpeed;
	}

	return false;
}
