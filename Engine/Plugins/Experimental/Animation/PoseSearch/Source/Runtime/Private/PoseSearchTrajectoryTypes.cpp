// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchTrajectoryTypes.h"

#include "Animation/AnimTypes.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchDefines.h"

FPoseSearchQueryTrajectorySample FPoseSearchQueryTrajectorySample::Lerp(const FPoseSearchQueryTrajectorySample& Other, float Alpha) const
{
	check(Facing.IsNormalized());
	check(Other.Facing.IsNormalized());

	FPoseSearchQueryTrajectorySample Result;
	
	Result.Facing = FQuat::FastLerp(Facing, Other.Facing, Alpha).GetNormalized();
	Result.Position = FMath::Lerp(Position, Other.Position, Alpha);
	Result.AccumulatedSeconds = FMath::Lerp(AccumulatedSeconds, Other.AccumulatedSeconds, Alpha);

	return Result;
}

void FPoseSearchQueryTrajectorySample::SetTransform(const FTransform& Transform)
{
	Position = Transform.GetTranslation();
	Facing = Transform.GetRotation();
}

FPoseSearchQueryTrajectorySample FPoseSearchQueryTrajectory::GetSampleAtTime(float Time, bool bExtrapolate) const
{
	const int32 Num = Samples.Num();
	if (Num > 1)
	{
		const int32 LowerBoundIdx = Algo::LowerBound(Samples, Time, [](const FPoseSearchQueryTrajectorySample& TrajectorySample, float Value)
			{
				return Value > TrajectorySample.AccumulatedSeconds;
			});

		const int32 NextIdx = FMath::Clamp(LowerBoundIdx, 1, Samples.Num() - 1);
		const int32 PrevIdx = NextIdx - 1;

		const float Denominator = Samples[NextIdx].AccumulatedSeconds - Samples[PrevIdx].AccumulatedSeconds;
		if (!FMath::IsNearlyZero(Denominator))
		{
			const float Numerator = Time - Samples[PrevIdx].AccumulatedSeconds;
			const float LerpValue = bExtrapolate ? Numerator / Denominator : FMath::Clamp(Numerator / Denominator, 0.f, 1.f);
			return Samples[PrevIdx].Lerp(Samples[NextIdx], LerpValue);
		}

		return Samples[PrevIdx];
	}

	if (Num > 0)
	{
		return Samples[0];
	}

	return FPoseSearchQueryTrajectorySample();
}

void FPoseSearchQueryTrajectory::TransformReferenceFrame(const FTransform& DeltaTransform)
{
	const FTransform InverseDeltaTransform = DeltaTransform.Inverse();
	for (FPoseSearchQueryTrajectorySample& Sample : Samples)
	{
		const FTransform Transform = InverseDeltaTransform * Sample.GetTransform() * DeltaTransform;
		Sample.SetTransform(Transform);
	}
}

#if ENABLE_ANIM_DEBUG
void FPoseSearchQueryTrajectory::DebugDrawTrajectory(const UWorld* World, const FTransform& TransformWS) const
{
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FVector CurrentSamplePositionWS = TransformWS.TransformPosition(Samples[Index].Position);

		DrawDebugSphere(
			World,
			CurrentSamplePositionWS,
			2.f /*Radius*/, 4 /*Segments*/,
			FColor::Black, false /*bPersistentLines*/, -1.f /*LifeTime*/, 0 /*DepthPriority*/, 1.f /*Thickness*/);

		if (Samples.IsValidIndex(Index + 1))
		{
			const FVector NextSamplePositionWS = TransformWS.TransformPosition(Samples[Index + 1].Position);

			DrawDebugLine(
				World,
				CurrentSamplePositionWS,
				NextSamplePositionWS,
				FColor::Black, false /*bPersistentLines*/, -1.f /*LifeTime*/, 0 /*DepthPriority*/, 1.f /*Thickness*/);
		}

		const FQuat CurrentSampleFacingWS = TransformWS.TransformRotation(Samples[Index].Facing);
		DrawDebugDirectionalArrow(
			World,
			CurrentSamplePositionWS,
			CurrentSamplePositionWS + CurrentSampleFacingWS.RotateVector(FVector::ForwardVector) * 25.f,
			20.f, FColor::Orange, false /*bPersistentLines*/, -1.f /*LifeTime*/, 0 /*DepthPriority*/, 1.f /*Thickness*/);
	}
}
#endif // ENABLE_ANIM_DEBUG