// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Position.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void UPoseSearchFeatureChannel_Position::Finalize(UPoseSearchSchema* Schema)
{
	ChannelDataOffset = Schema->SchemaCardinality;
	ChannelCardinality = UE::PoseSearch::FFeatureVectorHelper::GetVectorCardinality(ComponentStripping);
	Schema->SchemaCardinality += ChannelCardinality;

	SchemaBoneIdx = Schema->AddBoneReference(Bone);
	SchemaOriginBoneIdx = Schema->AddBoneReference(OriginBone);
}

void UPoseSearchFeatureChannel_Position::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	check(InOutQuery.GetSchema());
	const bool bIsCurrentResultValid = SearchContext.CurrentResult.IsValid() && SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();
	const bool bSkip = InputQueryPose != EInputQueryPose::UseCharacterPose && bIsCurrentResultValid;
	const bool bBoneValid = InOutQuery.GetSchema()->BoneReferences[SchemaBoneIdx].HasValidSetup();
	if (bSkip || (!SearchContext.History && bBoneValid))
	{
		if (bIsCurrentResultValid)
		{
			const float LerpValue = InputQueryPose == EInputQueryPose::UseInterpolatedContinuingPose ? SearchContext.CurrentResult.LerpValue : 0.f;
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue, false, ComponentStripping);
		}
		// else leave the InOutQuery set to zero since the SearchContext.History is invalid and it'll fail if we continue
	}
	else
	{
		// calculating the BonePosition in component space for the bone indexed by SchemaBoneIdx
		const FVector BonePosition = SearchContext.GetSamplePosition(SampleTimeOffset, InOutQuery.GetSchema(), SchemaBoneIdx, SchemaOriginBoneIdx, bBoneValid);
		FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, BonePosition, ComponentStripping);
	}
}

#if ENABLE_DRAW_DEBUG
void UPoseSearchFeatureChannel_Position::PreDebugDraw(UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
	using namespace UE::PoseSearch;
	
	// if the SchemaOriginBoneIdx is not the root bone, the feature vector position doesn't represent a component space delta position for the SchemaBoneIdx, so we shouldn't collect it
	if (!DrawParams.GetSchema()->BoneReferences[SchemaOriginBoneIdx].HasValidSetup())
	{
		const FVector BonePos = DrawParams.GetRootTransform().TransformPosition(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));
		DrawParams.AddCachedPosition(SampleTimeOffset, SchemaBoneIdx, BonePos);
	}
}

void UPoseSearchFeatureChannel_Position::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
	using namespace UE::PoseSearch;

	const FColor Color = DrawParams.GetColor(ColorPresetIndex);

	if (!DrawParams.GetSchema()->BoneReferences[SchemaOriginBoneIdx].HasValidSetup())
	{
		const FVector BonePos = DrawParams.GetRootTransform().TransformPosition(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));

		DrawParams.DrawPoint(BonePos, Color);
	}
	else
	{
		const FVector OriginBonePos = DrawParams.GetCachedPosition(SampleTimeOffset, SchemaOriginBoneIdx);
		const FVector DeltaPos = DrawParams.GetRootTransform().TransformVector(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));

		DrawParams.DrawLine(OriginBonePos, OriginBonePos + DeltaPos, Color);
	}	
}
#endif // ENABLE_DRAW_DEBUG

#if WITH_EDITOR
void UPoseSearchFeatureChannel_Position::FillWeights(TArray<float>& Weights) const
{
	for (int32 i = 0; i < ChannelCardinality; ++i)
	{
		Weights[ChannelDataOffset + i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Position::IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const
{
	using namespace UE::PoseSearch;

	for (int32 SampleIdx = Indexer.GetBeginSampleIdx(); SampleIdx != Indexer.GetEndSampleIdx(); ++SampleIdx)
	{
		const FVector BonePosition = Indexer.GetSamplePosition(SampleTimeOffset, SampleIdx, SchemaBoneIdx, SchemaOriginBoneIdx);
		FFeatureVectorHelper::EncodeVector(Indexer.GetPoseVector(SampleIdx), ChannelDataOffset, BonePosition, ComponentStripping);
	}
}

FString UPoseSearchFeatureChannel_Position::GetLabel() const
{
	TStringBuilder<256> Label;
	if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
	{
		Label.Append(OuterChannel->GetLabel());
		Label.Append(TEXT("_"));
	}

	Label.Append(TEXT("Pos"));

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

	const FBoneReference& OriginBoneReference = GetSchema()->BoneReferences[SchemaOriginBoneIdx];
	if (OriginBoneReference.HasValidSetup())
	{
		Label.Append(TEXT("_"));
		Label.Append(OriginBoneReference.BoneName.ToString());
	}

	Label.Appendf(TEXT(" %.1f"), SampleTimeOffset);
	return Label.ToString();
}
#endif