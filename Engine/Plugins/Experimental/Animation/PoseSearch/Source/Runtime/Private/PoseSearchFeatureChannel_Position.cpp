// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Position.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "DrawDebugHelpers.h"
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
}

void UPoseSearchFeatureChannel_Position::FillWeights(TArray<float>& Weights) const
{
	for (int32 i = 0; i < ChannelCardinality; ++i)
	{
		Weights[ChannelDataOffset + i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Position::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, TArrayView<float> FeatureVectorTable) const
{
	using namespace UE::PoseSearch;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();

	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;

		const float OriginSampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
		const float SubsampleTime = OriginSampleTime + SampleTimeOffset;

		bool ClampedPresent;
		const FTransform BoneTransformsPresent = Indexer.GetComponentSpaceTransform(SubsampleTime, OriginSampleTime, ClampedPresent, SchemaBoneIdx);
		FFeatureVectorHelper::EncodeVector(IndexingContext.GetPoseVector(VectorIdx, FeatureVectorTable), ChannelDataOffset, BoneTransformsPresent.GetTranslation(), ComponentStripping);
	}
}

void UPoseSearchFeatureChannel_Position::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	using namespace UE::PoseSearch;

	check(InOutQuery.GetSchema());
	const bool bIsCurrentResultValid = SearchContext.CurrentResult.IsValid();
	const bool bSkip = InputQueryPose != EInputQueryPose::UseCharacterPose && bIsCurrentResultValid && SearchContext.CurrentResult.Database->Schema == InOutQuery.GetSchema();
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
		// calculating the Transform in component space for the bone indexed by SchemaBoneIdx
		const FTransform Transform = SearchContext.GetComponentSpaceTransform(SampleTimeOffset, 0.f, InOutQuery.GetSchema(), SchemaBoneIdx, bBoneValid);

		FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, Transform.GetTranslation(), ComponentStripping);
	}
}

void UPoseSearchFeatureChannel_Position::PreDebugDraw(UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
	using namespace UE::PoseSearch;
	const FVector BonePos = DrawParams.RootTransform.TransformPosition(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));
	DrawParams.AddCachedPosition(SampleTimeOffset, SchemaBoneIdx, BonePos);
#endif // ENABLE_DRAW_DEBUG
}

void UPoseSearchFeatureChannel_Position::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);
	const FColor Color = DrawParams.GetColor(ColorPresetIndex);

	const FVector BonePos = DrawParams.RootTransform.TransformPosition(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));
	// validating DrawParams.AddCachedPosition 
	check(DrawParams.GetCachedPosition(SampleTimeOffset, SchemaBoneIdx) == BonePos);

	if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast | EDebugDrawFlags::DrawSearchIndex))
	{
		DrawDebugPoint(DrawParams.World, BonePos, DrawParams.PointSize, Color, bPersistent, LifeTime, DepthPriority);
	}
	else
	{
		DrawDebugSphere(DrawParams.World, BonePos, 2.f, 8, Color, bPersistent, LifeTime, DepthPriority);
	}

	if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawBoneNames))
	{
		const UPoseSearchSchema* Schema = DrawParams.GetSchema();
		check(Schema && Schema->IsValid());
		DrawDebugString(DrawParams.World, BonePos + FVector(0.0, 0.0, 10.0), Schema->BoneReferences[SchemaBoneIdx].BoneName.ToString(), nullptr, Color, LifeTime, false, 1.0f);
	}
#endif // ENABLE_DRAW_DEBUG
}

#if WITH_EDITOR
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

	Label.Appendf(TEXT(" %.1f"), SampleTimeOffset);
	return Label.ToString();
}
#endif