// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Heading.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void UPoseSearchFeatureChannel_Heading::Finalize(UPoseSearchSchema* Schema)
{
	ChannelDataOffset = Schema->SchemaCardinality;
	ChannelCardinality = UE::PoseSearch::FFeatureVectorHelper::GetVectorCardinality(ComponentStripping);
	Schema->SchemaCardinality += ChannelCardinality;
	SchemaBoneIdx = Schema->AddBoneReference(Bone);
}

FVector UPoseSearchFeatureChannel_Heading::GetAxis(const FQuat& Rotation) const
{
	switch (HeadingAxis)
	{
	case EHeadingAxis::X:
		return Rotation.GetAxisX();
	case EHeadingAxis::Y:
		return Rotation.GetAxisY();
	case EHeadingAxis::Z:
		return Rotation.GetAxisZ();
	}

	checkNoEntry();
	return FVector(1.f, 0.f, 0.f);
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Heading::FillWeights(TArray<float>& Weights) const
{
	for (int32 i = 0; i < ChannelCardinality; ++i)
	{
		Weights[ChannelDataOffset + i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Heading::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer, TArrayView<float> FeatureVectorTable) const
{
	using namespace UE::PoseSearch;

	for (int32 SampleIdx = Indexer.GetBeginSampleIdx(); SampleIdx != Indexer.GetEndSampleIdx(); ++SampleIdx)
	{
		const FVector Heading = GetAxis(Indexer.GetSampleRotation(SampleTimeOffset, SampleIdx, SchemaBoneIdx));
		FFeatureVectorHelper::EncodeVector(Indexer.GetPoseVector(SampleIdx, FeatureVectorTable), ChannelDataOffset, Heading, ComponentStripping);
	}
}
#endif // WITH_EDITOR

void UPoseSearchFeatureChannel_Heading::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	const bool bIsCurrentResultValid = SearchContext.CurrentResult.IsValid();
	const bool bSkip = InputQueryPose != EInputQueryPose::UseCharacterPose && bIsCurrentResultValid && SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();
	const bool bBoneValid = InOutQuery.GetSchema()->BoneReferences[SchemaBoneIdx].HasValidSetup();
	if (bSkip || (!SearchContext.History && bBoneValid))
	{
		if (bIsCurrentResultValid)
		{
			const float LerpValue = InputQueryPose == EInputQueryPose::UseInterpolatedContinuingPose ? SearchContext.CurrentResult.LerpValue : 0.f;
			// @todo: we should normalize if LerpValue != 0
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue, true, ComponentStripping);
		}
		// else leave the InOutQuery set to zero since the SearchContext.History is invalid and it'll fail if we continue
	}
	else
	{
		// calculating the BoneRotation in component space for the bone indexed by SchemaBoneIdx
		const FQuat BoneRotation = SearchContext.GetSampleRotation(SampleTimeOffset, InOutQuery.GetSchema(), SchemaBoneIdx, RootSchemaBoneIdx, bBoneValid);
		FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, GetAxis(BoneRotation), ComponentStripping);
	}
}

#if ENABLE_DRAW_DEBUG
void UPoseSearchFeatureChannel_Heading::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);
	const FColor Color = DrawParams.GetColor(ColorPresetIndex);
	const FVector BoneHeading = DrawParams.RootTransform.GetRotation().RotateVector(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));
	const FVector BonePos = DrawParams.GetCachedPosition(SampleTimeOffset, SchemaBoneIdx);

	if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
	{
		DrawDebugLine(DrawParams.World, BonePos, BonePos + BoneHeading * 15.f, Color, bPersistent, LifeTime, DepthPriority);
	}
	else
	{
		const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.0f : 1.f;
		DrawDebugLine(DrawParams.World, BonePos, BonePos + BoneHeading * 15.f, Color, bPersistent, LifeTime, DepthPriority, AdjustedThickness);
	}
}
#endif // ENABLE_DRAW_DEBUG

#if WITH_EDITOR
FString UPoseSearchFeatureChannel_Heading::GetLabel() const
{
	TStringBuilder<256> Label;
	if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
	{
		Label.Append(OuterChannel->GetLabel());
		Label.Append(TEXT("_"));
	}

	Label.Append(TEXT("Head"));
	switch (HeadingAxis)
	{
	case EHeadingAxis::X:
		Label.Append(TEXT("X"));
		break;
	case EHeadingAxis::Y:
		Label.Append(TEXT("Y"));
		break;
	case EHeadingAxis::Z:
		Label.Append(TEXT("Z"));
		break;
	}

	if (ComponentStripping == EComponentStrippingVector::StripXY)
	{
		Label.Append(TEXT("_z"));
	}
	else if (ComponentStripping == EComponentStrippingVector::StripZ)
	{
		Label.Append(TEXT("_xy"));
	}

	const FBoneReference& BoneReference = GetSchema()->BoneReferences[SchemaBoneIdx];
	if (BoneReference.HasValidSetup())
	{
		Label.Append(TEXT("_"));
		Label.Append(BoneReference.BoneName.ToString());
	}

	Label.Appendf(TEXT(" %.1f"), SampleTimeOffset);
	return Label.ToString();
}
#endif