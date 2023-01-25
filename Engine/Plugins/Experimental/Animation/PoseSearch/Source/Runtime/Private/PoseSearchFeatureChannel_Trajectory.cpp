// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Trajectory.h"
#include "Animation/Skeleton.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_Heading.h"
#include "PoseSearchFeatureChannel_Phase.h"
#include "PoseSearchFeatureChannel_Position.h"
#include "PoseSearchFeatureChannel_Velocity.h"

#define LOCTEXT_NAMESPACE "PoseSearchFeatureChannels"

void UPoseSearchFeatureChannel_Trajectory::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Samples.Sort([](const FPoseSearchTrajectorySample& a, const FPoseSearchTrajectorySample& b)
		{
			return a.Offset < b.Offset;
		});

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchFeatureChannel_Trajectory::Finalize(UPoseSearchSchema* Schema)
{
	SubChannels.Reset();

	for (const FPoseSearchTrajectorySample& Sample : Samples)
	{
		// @todo: implement PositionXY properly as 2 dimension channel
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Position | EPoseSearchTrajectoryFlags::PositionXY))
		{
			UPoseSearchFeatureChannel_Position* Position = NewObject<UPoseSearchFeatureChannel_Position>();
			Position->Weight = Sample.Weight * Weight;
			Position->SampleTimeOffset = Sample.Offset;
			Position->ColorPresetIndex = Sample.ColorPresetIndex;
			Position->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Position->SetFlags(RF_Transient);
			SubChannels.Add(Position);
		}

		// @todo: implement VelocityXY properly as 2 dimension channel
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::Velocity | EPoseSearchTrajectoryFlags::VelocityXY))
		{
			UPoseSearchFeatureChannel_Velocity* Velocity = NewObject<UPoseSearchFeatureChannel_Velocity>();
			Velocity->Weight = Sample.Weight * Weight;
			Velocity->SampleTimeOffset = Sample.Offset;
			Velocity->ColorPresetIndex = Sample.ColorPresetIndex;
			Velocity->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Velocity->bUseCharacterSpaceVelocities = false;
			Velocity->SetFlags(RF_Transient);
			SubChannels.Add(Velocity);
		}

		// @todo: implement VelocityDirectionXY properly as 2 dimension channel
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::VelocityDirection | EPoseSearchTrajectoryFlags::VelocityDirectionXY))
		{
			UPoseSearchFeatureChannel_Velocity* Velocity = NewObject<UPoseSearchFeatureChannel_Velocity>();
			Velocity->Weight = Sample.Weight * Weight;
			Velocity->SampleTimeOffset = Sample.Offset;
			Velocity->ColorPresetIndex = Sample.ColorPresetIndex;
			Velocity->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Velocity->bUseCharacterSpaceVelocities = false;
			Velocity->bNormalize = true;
			Velocity->SetFlags(RF_Transient);
			SubChannels.Add(Velocity);
		}

		// @todo: implement FacingDirectionXY properly as 2 dimension channel
		if (EnumHasAnyFlags(Sample.Flags, EPoseSearchTrajectoryFlags::FacingDirection | EPoseSearchTrajectoryFlags::FacingDirectionXY))
		{
			UPoseSearchFeatureChannel_Heading* Heading = NewObject<UPoseSearchFeatureChannel_Heading>();
			Heading->Weight = Sample.Weight * Weight;
			Heading->SampleTimeOffset = Sample.Offset;
			Heading->ColorPresetIndex = Sample.ColorPresetIndex;
			Heading->InputQueryPose = EInputQueryPose::UseCharacterPose;
			Heading->SetFlags(RF_Transient);
			SubChannels.Add(Heading);
		}
	}

	Super::Finalize(Schema);
}

void UPoseSearchFeatureChannel_Trajectory::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
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

		const float LifeTime = DrawParams.DefaultLifeTime;
		const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
		const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);

		DrawCentripetalCatmullRomSpline(DrawParams.World, TrajSplinePos, TrajSplineColor, 0.5f, 8.f, bPersistent, LifeTime, DepthPriority, 0.f);
	}

	Super::DebugDraw(DrawParams, PoseVector);
#endif // ENABLE_DRAW_DEBUG
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Trajectory::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
	{
		float Offset;
		EPoseSearchTrajectoryFlags SampleFlag;
		const char* Label;
		const UPoseSearchFeatureChannel* Channel = SubChannelPtr.Get();
		if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(Channel))
		{
			Offset = Position->SampleTimeOffset;
			SampleFlag = EPoseSearchTrajectoryFlags::Position;
			Label = "Pos";
		}
		else if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(Channel))
		{
			Offset = Velocity->SampleTimeOffset;
			if (Velocity->bNormalize)
			{
				SampleFlag = EPoseSearchTrajectoryFlags::VelocityDirection;
				Label = "VelDir";
			}
			else
			{
				SampleFlag = EPoseSearchTrajectoryFlags::Velocity;
				Label = "Vel";
			}
		}
		else if (const UPoseSearchFeatureChannel_Heading* Heading = Cast<UPoseSearchFeatureChannel_Heading>(Channel))
		{
			Offset = Heading->SampleTimeOffset;
			SampleFlag = EPoseSearchTrajectoryFlags::FacingDirection;
			Label = "Fac";
		}
		else
		{
			checkNoEntry();
			continue;
		}

		FString SkeletonName = FeatureChannelLayoutSet.CurrentSchema->Skeleton->GetName();
		UE::PoseSearch::FKeyBuilder KeyBuilder;
		KeyBuilder << SkeletonName << SampleFlag << Offset;
		FeatureChannelLayoutSet.Add(FString::Format(TEXT("Traj {0} {1}"), { Label, Offset }), KeyBuilder.Finalize(), Channel->GetChannelDataOffset(), Channel->GetChannelCardinality());
	}
}

void UPoseSearchFeatureChannel_Trajectory::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	CostBreakDownData.AddEntireBreakDownSection(LOCTEXT("ColumnLabelTrajChannelTotal", "Traj Total"), Schema, ChannelDataOffset, ChannelCardinality);

	if (CostBreakDownData.IsVerbose())
	{
		for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
		{
			const UPoseSearchFeatureChannel* Channel = SubChannelPtr.Get();
			if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelPosition", "Traj Pos {0}"), Position->SampleTimeOffset), Schema, Position->GetChannelDataOffset(), Position->GetChannelCardinality());
			}
			else if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelVelocity", "Traj Vel {0}"), Velocity->SampleTimeOffset), Schema, Velocity->GetChannelDataOffset(), Velocity->GetChannelCardinality());
			}
			else if (const UPoseSearchFeatureChannel_Heading* Heading = Cast<UPoseSearchFeatureChannel_Heading>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelTrajChannelFacingDirection", "Traj Fac {0}"), Heading->SampleTimeOffset), Schema, Heading->GetChannelDataOffset(), Heading->GetChannelCardinality());
			}
			else
			{
				checkNoEntry();
			}
		}
	}
}

#endif // WITH_EDITOR

float UPoseSearchFeatureChannel_Trajectory::GetEstimatedSpeedRatio(TConstArrayView<float> QueryVector, TConstArrayView<float> PoseVector) const
{
	float EstimatedQuerySpeed = 0.f;
	float EstimatedPoseSpeed = 0.f;

	for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
	{
		if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(SubChannelPtr.Get()))
		{
			if (!Velocity->bNormalize)
			{
				const FVector QueryVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVectorAtOffset(QueryVector, Velocity->GetChannelDataOffset());
				const FVector PoseVelocity = UE::PoseSearch::FFeatureVectorHelper::DecodeVectorAtOffset(PoseVector, Velocity->GetChannelDataOffset());
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