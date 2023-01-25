// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Pose.h"
#include "Animation/Skeleton.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearchFeatureChannel_Heading.h"
#include "PoseSearchFeatureChannel_Phase.h"
#include "PoseSearchFeatureChannel_Position.h"
#include "PoseSearchFeatureChannel_Velocity.h"

#define LOCTEXT_NAMESPACE "PoseSearchFeatureChannels"

void UPoseSearchFeatureChannel_Pose::Finalize(UPoseSearchSchema* Schema)
{
	SubChannels.Reset();

	for (int32 ChannelBoneIdx = 0; ChannelBoneIdx != SampledBones.Num(); ++ChannelBoneIdx)
	{
		const FPoseSearchBone& SampledBone = SampledBones[ChannelBoneIdx];
		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Position))
		{
			UPoseSearchFeatureChannel_Position* Position = NewObject<UPoseSearchFeatureChannel_Position>();
			Position->Bone = SampledBone.Reference;
			Position->Weight = SampledBone.Weight * Weight;
			Position->SampleTimeOffset = 0.f;
			Position->ColorPresetIndex = SampledBone.ColorPresetIndex;
			Position->InputQueryPose = InputQueryPose;
			Position->SetFlags(RF_Transient);
			SubChannels.Add(Position);
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Rotation))
		{
			UPoseSearchFeatureChannel_Heading* HeadingX = NewObject<UPoseSearchFeatureChannel_Heading>();
			HeadingX->Bone = SampledBone.Reference;
			HeadingX->Weight = SampledBone.Weight * Weight;
			HeadingX->SampleTimeOffset = 0.f;
			HeadingX->HeadingAxis = EHeadingAxis::X;
			HeadingX->ColorPresetIndex = SampledBone.ColorPresetIndex;
			HeadingX->InputQueryPose = InputQueryPose;
			HeadingX->SetFlags(RF_Transient);
			SubChannels.Add(HeadingX);

			UPoseSearchFeatureChannel_Heading* HeadingY = NewObject<UPoseSearchFeatureChannel_Heading>();
			HeadingY->Bone = SampledBone.Reference;
			HeadingY->Weight = SampledBone.Weight * Weight;
			HeadingY->SampleTimeOffset = 0.f;
			HeadingY->HeadingAxis = EHeadingAxis::Y;
			HeadingY->ColorPresetIndex = SampledBone.ColorPresetIndex;
			HeadingY->InputQueryPose = InputQueryPose;
			HeadingY->SetFlags(RF_Transient);
			SubChannels.Add(HeadingY);
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Velocity))
		{
			UPoseSearchFeatureChannel_Velocity* Velocity = NewObject<UPoseSearchFeatureChannel_Velocity>();
			Velocity->Bone = SampledBone.Reference;
			Velocity->Weight = SampledBone.Weight * Weight;
			Velocity->SampleTimeOffset = 0.f;
			Velocity->ColorPresetIndex = SampledBone.ColorPresetIndex;
			Velocity->InputQueryPose = InputQueryPose;
			Velocity->bUseCharacterSpaceVelocities = bUseCharacterSpaceVelocities;
			Velocity->SetFlags(RF_Transient);
			SubChannels.Add(Velocity);
		}

		if (EnumHasAnyFlags(SampledBone.Flags, EPoseSearchBoneFlags::Phase))
		{
			UPoseSearchFeatureChannel_Phase* Phase = NewObject<UPoseSearchFeatureChannel_Phase>();
			Phase->Bone = SampledBone.Reference;
			Phase->Weight = SampledBone.Weight * Weight;
			Phase->ColorPresetIndex = SampledBone.ColorPresetIndex;
			Phase->InputQueryPose = InputQueryPose;
			Phase->SetFlags(RF_Transient);
			SubChannels.Add(Phase);
		}
	}

	Super::Finalize(Schema);
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Pose::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
	{
		FString BoneName;
		EPoseSearchBoneFlags BoneFlag;
		const char* Label;
		const UPoseSearchFeatureChannel* Channel = SubChannelPtr.Get();
		if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(Channel))
		{
			BoneName = Position->Bone.BoneName.ToString();
			BoneFlag = EPoseSearchBoneFlags::Position;
			Label = "Pos";
		}
		else if (const UPoseSearchFeatureChannel_Heading* Heading = Cast<UPoseSearchFeatureChannel_Heading>(Channel))
		{
			BoneName = Heading->Bone.BoneName.ToString();
			BoneFlag = EPoseSearchBoneFlags::Rotation;

			if (Heading->HeadingAxis == EHeadingAxis::X)
			{
				Label = "HdX";
			}
			else
			{
				check(Heading->HeadingAxis == EHeadingAxis::Y);
				Label = "HdY";
			}
		}
		else if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(Channel))
		{
			BoneName = Velocity->Bone.BoneName.ToString();
			BoneFlag = EPoseSearchBoneFlags::Velocity;
			Label = "Vel";
		}
		else if (const UPoseSearchFeatureChannel_Phase* Phase = Cast<UPoseSearchFeatureChannel_Phase>(Channel))
		{
			BoneName = Phase->Bone.BoneName.ToString();
			BoneFlag = EPoseSearchBoneFlags::Phase;
			Label = "Pha";
		}
		else
		{
			checkNoEntry();
			continue;
		}
		
		FString SkeletonName = FeatureChannelLayoutSet.CurrentSchema->Skeleton->GetName();
		UE::PoseSearch::FKeyBuilder KeyBuilder;
		KeyBuilder << SkeletonName << BoneName << BoneFlag;
		FeatureChannelLayoutSet.Add(FString::Format(TEXT("{0} {1}"), { BoneName, Label }), KeyBuilder.Finalize(), Channel->GetChannelDataOffset(), Channel->GetChannelCardinality());
	}
}

void UPoseSearchFeatureChannel_Pose::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	CostBreakDownData.AddEntireBreakDownSection(LOCTEXT("ColumnLabelPoseChannelTotal", "Pose Total"), Schema, ChannelDataOffset, ChannelCardinality);

	if (CostBreakDownData.IsVerbose())
	{
		for (const TObjectPtr<UPoseSearchFeatureChannel>& SubChannelPtr : GetSubChannels())
		{
			const UPoseSearchFeatureChannel* Channel = SubChannelPtr.Get();
			if (const UPoseSearchFeatureChannel_Position* Position = Cast<UPoseSearchFeatureChannel_Position>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelPosition", "{0} Pos"), FText::FromName(Position->Bone.BoneName)), Schema, Position->GetChannelDataOffset(), Position->GetChannelCardinality());
			}
			else if (const UPoseSearchFeatureChannel_Heading* Heading = Cast<UPoseSearchFeatureChannel_Heading>(Channel))
			{
				if (Heading->HeadingAxis == EHeadingAxis::X)
				{
					CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelHeading", "{0} HdX"), FText::FromName(Heading->Bone.BoneName)), Schema, Heading->GetChannelDataOffset(), Heading->GetChannelCardinality());
				}
				else
				{
					check(Heading->HeadingAxis == EHeadingAxis::Y);
					CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelHeading", "{0} HdY"), FText::FromName(Heading->Bone.BoneName)), Schema, Heading->GetChannelDataOffset(), Heading->GetChannelCardinality());
				}
			}
			else if (const UPoseSearchFeatureChannel_Velocity* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelVelocity", "{0} Vel"), FText::FromName(Velocity->Bone.BoneName)), Schema, Velocity->GetChannelDataOffset(), Velocity->GetChannelCardinality());
			}
			else if (const UPoseSearchFeatureChannel_Phase* Phase = Cast<UPoseSearchFeatureChannel_Phase>(Channel))
			{
				CostBreakDownData.AddEntireBreakDownSection(FText::Format(LOCTEXT("ColumnLabelPoseChannelPhase", "{0} Pha"), FText::FromName(Phase->Bone.BoneName)), Schema, Phase->GetChannelDataOffset(), Phase->GetChannelCardinality());
			}
			else
			{
				checkNoEntry();
			}
		}
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE