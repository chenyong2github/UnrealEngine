// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Trajectory.h"
#include "Animation/Skeleton.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_Heading.h"
#include "PoseSearchFeatureChannel_Phase.h"
#include "PoseSearchFeatureChannel_Position.h"
#include "PoseSearchFeatureChannel_Velocity.h"

#define LOCTEXT_NAMESPACE "PoseSearchFeatureChannels"

UPoseSearchFeatureChannel_Trajectory::UPoseSearchFeatureChannel_Trajectory()
{
#if WITH_EDITOR
	// defaulting UPoseSearchFeatureChannel_Trajectory to a meaningful locomotion setup
	Weight = 7.f;
	Samples.Add(FPoseSearchTrajectorySample({ -0.4f, int32(EPoseSearchTrajectoryFlags::PositionXY), 0.4f, 0 }));
	Samples.Add(FPoseSearchTrajectorySample({ 0.f, int32(EPoseSearchTrajectoryFlags::VelocityXY | EPoseSearchTrajectoryFlags::FacingDirectionXY), 2.f }));
	Samples.Add(FPoseSearchTrajectorySample({ 0.35f, int32(EPoseSearchTrajectoryFlags::PositionXY | EPoseSearchTrajectoryFlags::FacingDirectionXY), 0.7f }));
	Samples.Add(FPoseSearchTrajectorySample({ 0.7f, int32(EPoseSearchTrajectoryFlags::VelocityXY | EPoseSearchTrajectoryFlags::PositionXY | EPoseSearchTrajectoryFlags::FacingDirectionXY), 0.5f }));
#endif
}

void UPoseSearchFeatureChannel_Trajectory::Finalize(UPoseSearchSchema* Schema)
{
	SubChannels.Reset();

	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position | EPoseSearchTrajectoryFlags::PositionXY))
		{
			UPoseSearchFeatureChannel_Position* Position = NewObject<UPoseSearchFeatureChannel_Position>(this, NAME_None, RF_Transient);
			Position->Weight = Sample.Weight * Weight;
			Position->SampleTimeOffset = Sample.Offset;
			Position->ColorPresetIndex = Sample.ColorPresetIndex;
			Position->InputQueryPose = EInputQueryPose::UseCharacterPose;
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::PositionXY))
			{
				Position->ComponentStripping = EComponentStrippingVector::StripZ;
			}
			SubChannels.Add(Position);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity | EPoseSearchTrajectoryFlags::VelocityXY))
		{
			UPoseSearchFeatureChannel_Velocity* Velocity = NewObject<UPoseSearchFeatureChannel_Velocity>(this, NAME_None, RF_Transient);
			Velocity->Weight = Sample.Weight * Weight;
			Velocity->SampleTimeOffset = Sample.Offset;
			Velocity->ColorPresetIndex = Sample.ColorPresetIndex;
			Velocity->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Velocity->bUseCharacterSpaceVelocities = false;
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityXY))
			{
				Velocity->ComponentStripping = EComponentStrippingVector::StripZ;
			}
			SubChannels.Add(Velocity);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection | EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			UPoseSearchFeatureChannel_Velocity* Velocity = NewObject<UPoseSearchFeatureChannel_Velocity>(this, NAME_None, RF_Transient);
			Velocity->Weight = Sample.Weight * Weight;
			Velocity->SampleTimeOffset = Sample.Offset;
			Velocity->ColorPresetIndex = Sample.ColorPresetIndex;
			Velocity->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Velocity->bUseCharacterSpaceVelocities = false;
			Velocity->bNormalize = true;
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirectionXY))
			{
				Velocity->ComponentStripping = EComponentStrippingVector::StripZ;
			}
			SubChannels.Add(Velocity);
		}

		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection | EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			UPoseSearchFeatureChannel_Heading* Heading = NewObject<UPoseSearchFeatureChannel_Heading>(this, NAME_None, RF_Transient);
			Heading->Weight = Sample.Weight * Weight;
			Heading->SampleTimeOffset = Sample.Offset;
			Heading->ColorPresetIndex = Sample.ColorPresetIndex;
			Heading->InputQueryPose = EInputQueryPose::UseCharacterPose;
			if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirectionXY))
			{
				Heading->ComponentStripping = EComponentStrippingVector::StripZ;
			}
			SubChannels.Add(Heading);
		}
	}

	Super::Finalize(Schema);
}

#if ENABLE_DRAW_DEBUG
void UPoseSearchFeatureChannel_Trajectory::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
	using namespace UE::PoseSearch;

	TArray<const UPoseSearchFeatureChannel_Position*, TInlineAllocator<32>> Positions;
	for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
	{
		if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(SubChannelPtr.Get()))
		{
			Positions.Add(Position);
		}
	}

	if (Positions.Num() >= 2)
	{
		Positions.Sort([](const UPoseSearchFeatureChannel_Position& a, const UPoseSearchFeatureChannel_Position& b)
			{
				return a.SampleTimeOffset < b.SampleTimeOffset;
			});

		// big enough negative number that prevents PrevTimeOffset * CurrTimeOffset being infinite (there will never be UPoseSearchFeatureChannel_Trajectory trying to match 1000 seconds in the past)
		float PrevTimeOffset = -1000.f;
		TArray<FVector, TInlineAllocator<32>> TrajSplinePos;
		TArray<FColor, TInlineAllocator<32>> TrajSplineColor;
		for (int32 i = 0; i < Positions.Num(); ++i)
		{
			const float CurrTimeOffset = Positions[i]->SampleTimeOffset;
			const int32 CurrColorPresetIndex = Positions[i]->ColorPresetIndex;

			if (PrevTimeOffset * CurrTimeOffset < UE_KINDA_SMALL_NUMBER)
			{
				// we jumped from negative to positive time offset without having a zero time offset. so we add the zero
				TrajSplinePos.Add(DrawParams.GetCachedPosition(0.f));
				TrajSplineColor.Add(DrawParams.GetColor(CurrColorPresetIndex));
			}

			TrajSplinePos.Add(DrawParams.GetCachedPosition(CurrTimeOffset));
			TrajSplineColor.Add(DrawParams.GetColor(CurrColorPresetIndex));

			PrevTimeOffset = CurrTimeOffset;
		}

		DrawParams.DrawCentripetalCatmullRomSpline(TrajSplinePos, TrajSplineColor, 0.5f, 8);
	}

	Super::DebugDraw(DrawParams, PoseVector);
}
#endif // ENABLE_DRAW_DEBUG

#if WITH_EDITOR
FString UPoseSearchFeatureChannel_Trajectory::GetLabel() const
{
	TStringBuilder<256> Label;
	if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
	{
		Label.Append(OuterChannel->GetLabel());
		Label.Append(TEXT("_"));
	}
	Label.Append(TEXT("Traj"));
	return Label.ToString();
}
#endif // WITH_EDITOR

float UPoseSearchFeatureChannel_Trajectory::GetEstimatedSpeedRatio(TConstArrayView<float> QueryVector, TConstArrayView<float> PoseVector) const
{
	using namespace UE::PoseSearch;

	float EstimatedQuerySpeed = 0.f;
	float EstimatedPoseSpeed = 0.f;

	for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
	{
		if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(SubChannelPtr.Get()))
		{
			if (!Velocity->bNormalize)
			{
				const FVector QueryVelocity = FFeatureVectorHelper::DecodeVector(QueryVector, Velocity->GetChannelDataOffset(), Velocity->ComponentStripping);
				const FVector PoseVelocity = FFeatureVectorHelper::DecodeVector(PoseVector, Velocity->GetChannelDataOffset(), Velocity->ComponentStripping);
				EstimatedQuerySpeed += QueryVelocity.Length();
				EstimatedPoseSpeed += PoseVelocity.Length();
			}
		}
	}

	if (EstimatedPoseSpeed > UE_KINDA_SMALL_NUMBER)
	{
		return EstimatedQuerySpeed / EstimatedPoseSpeed;
	}

	return 1.f;
}

#undef LOCTEXT_NAMESPACE