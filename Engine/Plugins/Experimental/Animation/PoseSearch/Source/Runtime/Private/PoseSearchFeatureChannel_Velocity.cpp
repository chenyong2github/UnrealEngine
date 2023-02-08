// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchFeatureChannel_Velocity.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchAssetIndexer.h"
#include "PoseSearch/PoseSearchAssetSampler.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchSchema.h"

void UPoseSearchFeatureChannel_Velocity::Finalize(UPoseSearchSchema* Schema)
{
	ChannelDataOffset = Schema->SchemaCardinality;
	ChannelCardinality = UE::PoseSearch::FFeatureVectorHelper::GetVectorCardinality(ComponentStripping);
	Schema->SchemaCardinality += ChannelCardinality;

	SchemaBoneIdx = Schema->AddBoneReference(Bone);
}

void UPoseSearchFeatureChannel_Velocity::FillWeights(TArray<float>& Weights) const
{
	for (int32 i = 0; i < ChannelCardinality; ++i)
	{
		Weights[ChannelDataOffset + i] = Weight;
	}
}

void UPoseSearchFeatureChannel_Velocity::IndexAsset(UE::PoseSearch::IAssetIndexer& Indexer, TArrayView<float> FeatureVectorTable) const
{
	using namespace UE::PoseSearch;

	const FAssetIndexingContext& IndexingContext = Indexer.GetIndexingContext();
	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;
	check(SamplingContext->FiniteDelta > UE_KINDA_SMALL_NUMBER);

	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 VectorIdx = SampleIdx - IndexingContext.BeginSampleIdx;

		const float OriginSampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), IndexingContext.AssetSampler->GetPlayLength());
		const float SubsampleTime = OriginSampleTime + SampleTimeOffset;

		bool ClampedPast, ClampedPresent, ClampedFuture;
		const FTransform BoneTransformsPast = Indexer.GetComponentSpaceTransform(SubsampleTime - SamplingContext->FiniteDelta, bUseCharacterSpaceVelocities ? OriginSampleTime - SamplingContext->FiniteDelta : OriginSampleTime, ClampedPast, SchemaBoneIdx);
		const FTransform BoneTransformsPresent = Indexer.GetComponentSpaceTransform(SubsampleTime, OriginSampleTime, ClampedPresent, SchemaBoneIdx);
		const FTransform BoneTransformsFuture = Indexer.GetComponentSpaceTransform(SubsampleTime + SamplingContext->FiniteDelta, bUseCharacterSpaceVelocities ? OriginSampleTime + SamplingContext->FiniteDelta : OriginSampleTime, ClampedFuture, SchemaBoneIdx);

		// We can get a better finite difference if we ignore samples that have
		// been clamped at either side of the clip. However, if the central sample 
		// itself is clamped, or there are no samples that are clamped, we can just 
		// use the central difference as normal.
		FVector LinearVelocity;
		if (ClampedPast && !ClampedPresent && !ClampedFuture)
		{
			LinearVelocity = (BoneTransformsFuture.GetTranslation() - BoneTransformsPresent.GetTranslation()) / SamplingContext->FiniteDelta;
		}
		else if (ClampedFuture && !ClampedPresent && !ClampedPast)
		{
			LinearVelocity = (BoneTransformsPresent.GetTranslation() - BoneTransformsPast.GetTranslation()) / SamplingContext->FiniteDelta;
		}
		else
		{
			LinearVelocity = (BoneTransformsFuture.GetTranslation() - BoneTransformsPast.GetTranslation()) / (SamplingContext->FiniteDelta * 2.f);
		}

		if (bNormalize)
		{
			LinearVelocity = LinearVelocity.GetClampedToMaxSize(1.f);
		}

		FFeatureVectorHelper::EncodeVector(IndexingContext.GetPoseVector(VectorIdx, FeatureVectorTable), ChannelDataOffset, LinearVelocity, ComponentStripping);
	}
}

void UPoseSearchFeatureChannel_Velocity::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
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
			// @todo: we should normalize if EPoseSearchVelocityFlags::Normalize && LerpValue != 0
			FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, SearchContext.GetCurrentResultPrevPoseVector(), SearchContext.GetCurrentResultPoseVector(), SearchContext.GetCurrentResultNextPoseVector(), LerpValue, false, ComponentStripping);
		}
		// else leave the InOutQuery set to zero since the SearchContext.History is invalid and it'll fail if we continue
	}
	else
	{
		const float HistorySampleInterval = SearchContext.History ? SearchContext.History->GetSampleTimeInterval() : 1 / 60.0f;
		check(HistorySampleInterval > UE_KINDA_SMALL_NUMBER);

		// calculating the Transforms in component space for the bone indexed by SchemaBoneIdx
		const FTransform TransformCurrent = SearchContext.GetComponentSpaceTransform(SampleTimeOffset, 0.f, InOutQuery.GetSchema(), SchemaBoneIdx, bBoneValid);
		const FTransform TransformPrevious = SearchContext.GetComponentSpaceTransform(SampleTimeOffset - HistorySampleInterval, bUseCharacterSpaceVelocities ? -HistorySampleInterval : 0.f, InOutQuery.GetSchema(), SchemaBoneIdx, bBoneValid);

		FVector LinearVelocity = (TransformCurrent.GetTranslation() - TransformPrevious.GetTranslation()) / HistorySampleInterval;
		if (bNormalize)
		{
			LinearVelocity = LinearVelocity.GetClampedToMaxSize(1.f);
		}

		FFeatureVectorHelper::EncodeVector(InOutQuery.EditValues(), ChannelDataOffset, LinearVelocity, ComponentStripping);
	}
}

void UPoseSearchFeatureChannel_Velocity::DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector) const
{
#if ENABLE_DRAW_DEBUG
	using namespace UE::PoseSearch;

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;
	const bool bPersistent = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::Persistent);
	const FColor Color = DrawParams.GetColor(ColorPresetIndex);
	const float LinearVelocityScale = bNormalize ? 15.f : 0.08f;

	const FVector LinearVelocity = DrawParams.RootTransform.TransformVector(FFeatureVectorHelper::DecodeVector(PoseVector, ChannelDataOffset, ComponentStripping));
	const FVector BoneVelDirection = LinearVelocity.GetSafeNormal();
	const FVector BonePos = DrawParams.GetCachedPosition(SampleTimeOffset, SchemaBoneIdx);

	if (EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
	{
		DrawDebugLine(DrawParams.World, BonePos, BonePos + LinearVelocity * LinearVelocityScale, Color, bPersistent, LifeTime, DepthPriority);
	}
	else
	{
		const float AdjustedThickness = EnumHasAnyFlags(DrawParams.Flags, EDebugDrawFlags::DrawFast) ? 0.f : 1.f;
		DrawDebugLine(DrawParams.World, BonePos + BoneVelDirection * 2.f, BonePos + LinearVelocity * LinearVelocityScale, Color, bPersistent, LifeTime, DepthPriority, AdjustedThickness);
	}
#endif // ENABLE_DRAW_DEBUG
}

#if WITH_EDITOR
FString UPoseSearchFeatureChannel_Velocity::GetLabel() const
{
	TStringBuilder<256> Label;
	if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
	{
		Label.Append(OuterChannel->GetLabel());
		Label.Append(TEXT("_"));
	}

	Label.Append(TEXT("Vel"));
	if (bNormalize)
	{
		Label.Append(TEXT("Dir"));
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