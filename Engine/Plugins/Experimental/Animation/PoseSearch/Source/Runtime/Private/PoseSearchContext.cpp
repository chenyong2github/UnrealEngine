// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchContext.h"
#include "AnimationRuntime.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{
	
//////////////////////////////////////////////////////////////////////////
// FDebugDrawParams
bool FDebugDrawParams::CanDraw() const
{
#if ENABLE_DRAW_DEBUG
	return World && Database && Database->Schema && Database->Schema->IsValid();
#else // ENABLE_DRAW_DEBUG
	return false;
#endif // ENABLE_DRAW_DEBUG
}

FColor FDebugDrawParams::GetColor(int32 ColorPreset) const
{
#if ENABLE_DRAW_DEBUG
	FLinearColor Color = FLinearColor::Red;

	const UPoseSearchSchema* Schema = GetSchema();
	if (!Schema || !Schema->IsValid())
	{
		Color = FLinearColor::Red;
	}
	else if (ColorPreset < 0 || ColorPreset >= Schema->ColorPresets.Num())
	{
		if (EnumHasAnyFlags(Flags, EDebugDrawFlags::DrawQuery))
		{
			Color = FLinearColor::Blue;
		}
		else
		{
			Color = FLinearColor::Green;
		}
	}
	else
	{
		if (EnumHasAnyFlags(Flags, EDebugDrawFlags::DrawQuery))
		{
			Color = Schema->ColorPresets[ColorPreset].Query;
		}
		else
		{
			Color = Schema->ColorPresets[ColorPreset].Result;
		}
	}

	return Color.ToFColor(true);
#else // ENABLE_DRAW_DEBUG
	return FColor::Black;
#endif // ENABLE_DRAW_DEBUG
}

const FPoseSearchIndex* FDebugDrawParams::GetSearchIndex() const
{
	return Database ? &Database->GetSearchIndex() : nullptr;
}

const UPoseSearchSchema* FDebugDrawParams::GetSchema() const
{
	return Database ? Database->Schema : nullptr;
}

void DrawFeatureVector(const FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector)
{
#if ENABLE_DRAW_DEBUG
	if (DrawParams.CanDraw())
	{
		const UPoseSearchSchema* Schema = DrawParams.GetSchema();
		check(Schema);

		if (PoseVector.Num() == Schema->SchemaCardinality)
		{
			for (int32 ChannelIdx = 0; ChannelIdx != Schema->Channels.Num(); ++ChannelIdx)
			{
				if (DrawParams.ChannelMask & (1 << ChannelIdx))
				{
					Schema->Channels[ChannelIdx]->DebugDraw(DrawParams, PoseVector);
				}
			}
		}
	}
#endif // ENABLE_DRAW_DEBUG
}

void DrawFeatureVector(const FDebugDrawParams& DrawParams, int32 PoseIdx)
{
#if ENABLE_DRAW_DEBUG
	// if we're editing the schema while in PIE with Rewind Debugger active, PoseIdx could be out of bound / stale
	if (DrawParams.CanDraw() && PoseIdx >= 0 && PoseIdx < DrawParams.GetSearchIndex()->NumPoses)
	{
		DrawFeatureVector(DrawParams, DrawParams.GetSearchIndex()->GetPoseValues(PoseIdx));
	}
#endif // ENABLE_DRAW_DEBUG
}

//////////////////////////////////////////////////////////////////////////
// FSearchContext
FTransform FSearchContext::TryGetTransformAndCacheResults(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx)
{
	check(History && Schema);

	static constexpr FBoneIndexType RootBoneIdx = 0xFFFF;
	const FBoneIndexType BoneIndexType = SchemaBoneIdx >= 0 && Schema->BoneReferences[SchemaBoneIdx].HasValidSetup() ? Schema->BoneReferences[SchemaBoneIdx].BoneIndex : RootBoneIdx;

	// @todo: use an hashmap if we end up having too many entries
	const FCachedEntry* Entry = CachedEntries.FindByPredicate([SampleTime, BoneIndexType](const FSearchContext::FCachedEntry& Entry)
	{
		return Entry.SampleTime == SampleTime && Entry.BoneIndexType == BoneIndexType;
	});

	if (Entry)
	{
		return Entry->Transform;
	}

	if (BoneIndexType != RootBoneIdx)
	{
		TArray<FTransform> SampledLocalPose;
		if (History->TrySampleLocalPose(-SampleTime, &Schema->BoneIndicesWithParents, &SampledLocalPose, nullptr))
		{
			TArray<FTransform> SampledComponentPose;
			FAnimationRuntime::FillUpComponentSpaceTransforms(Schema->Skeleton->GetReferenceSkeleton(), SampledLocalPose, SampledComponentPose);

			// adding bunch of entries, without caring about adding eventual duplicates
			for (const FBoneIndexType NewEntryBoneIndexType : Schema->BoneIndicesWithParents)
			{
				CachedEntries.Emplace(SampleTime, SampledComponentPose[NewEntryBoneIndexType], NewEntryBoneIndexType);
			}

			return SampledComponentPose[BoneIndexType];
		}

		return FTransform::Identity;
	}
	
	FTransform SampledRootTransform;
	if (History->TrySampleLocalPose(-SampleTime, nullptr, nullptr, &SampledRootTransform))
	{
		FCachedEntry& NewEntry = CachedEntries[CachedEntries.AddDefaulted()];
		NewEntry.SampleTime = SampleTime;
		NewEntry.BoneIndexType = BoneIndexType;
		NewEntry.Transform = SampledRootTransform;

		return SampledRootTransform;
	}
	
	return FTransform::Identity;
}

void FSearchContext::ClearCachedEntries()
{
	CachedEntries.Reset();
}

void FSearchContext::ResetCurrentBestCost()
{
	CurrentBestTotalCost = MAX_flt;
}

void FSearchContext::UpdateCurrentBestCost(const FPoseSearchCost& PoseSearchCost)
{
	check(PoseSearchCost.IsValid());

	if (PoseSearchCost.GetTotalCost() < CurrentBestTotalCost)
	{
		CurrentBestTotalCost = PoseSearchCost.GetTotalCost();
	};
}

const FPoseSearchFeatureVectorBuilder* FSearchContext::GetCachedQuery(const UPoseSearchDatabase* Database) const
{
	const FSearchContext::FCachedQuery* CachedQuery = CachedQueries.FindByPredicate([Database](const FSearchContext::FCachedQuery& CachedQuery)
	{
		return CachedQuery.Database == Database;
	});

	if (CachedQuery)
	{
		return &CachedQuery->FeatureVectorBuilder;
	}
	return nullptr;
}

void FSearchContext::GetOrBuildQuery(const UPoseSearchDatabase* Database, FPoseSearchFeatureVectorBuilder& FeatureVectorBuilder)
{
	const FPoseSearchFeatureVectorBuilder* CachedFeatureVectorBuilder = GetCachedQuery(Database);
	if (CachedFeatureVectorBuilder)
	{
		FeatureVectorBuilder = *CachedFeatureVectorBuilder;
	}
	else
	{
		FSearchContext::FCachedQuery& NewCachedQuery = CachedQueries[CachedQueries.AddDefaulted()];
		NewCachedQuery.Database = Database;
		Database->BuildQuery(*this, NewCachedQuery.FeatureVectorBuilder);
		FeatureVectorBuilder = NewCachedQuery.FeatureVectorBuilder;
	}
}

bool FSearchContext::IsCurrentResultFromDatabase(const UPoseSearchDatabase* Database) const
{
	return CurrentResult.IsValid() && CurrentResult.Database == Database;
}

TConstArrayView<float> FSearchContext::GetCurrentResultPrevPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.PrevPoseIdx);
}

TConstArrayView<float> FSearchContext::GetCurrentResultPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.PoseIdx);
}

TConstArrayView<float> FSearchContext::GetCurrentResultNextPoseVector() const
{
	check(CurrentResult.IsValid());
	const FPoseSearchIndex& SearchIndex = CurrentResult.Database->GetSearchIndex();
	return SearchIndex.GetPoseValues(CurrentResult.NextPoseIdx);
}

} // namespace UE::PoseSearch