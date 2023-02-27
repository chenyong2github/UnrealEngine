// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchContext.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AnimationRuntime.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{
	
#if ENABLE_DRAW_DEBUG
//////////////////////////////////////////////////////////////////////////
// FDebugDrawParams
bool FDebugDrawParams::CanDraw() const
{
	return World && Database && Database->Schema && Database->Schema->IsValid();
}

FColor FDebugDrawParams::GetColor(int32 ColorPreset) const
{
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
}

const FPoseSearchIndex* FDebugDrawParams::GetSearchIndex() const
{
	return Database ? &Database->GetSearchIndex() : nullptr;
}

const UPoseSearchSchema* FDebugDrawParams::GetSchema() const
{
	return Database ? Database->Schema : nullptr;
}

void FDebugDrawParams::ClearCachedPositions()
{
	CachedPositions.Reset();
}

void FDebugDrawParams::AddCachedPosition(float TimeOffset, int8 SchemaBoneIdx, const FVector& Position)
{
	if (const UPoseSearchSchema* Schema = GetSchema())
	{
		CachedPositions.Add(TimeOffset, Schema->GetBoneIndexType(SchemaBoneIdx), Position);
	}
}

FVector FDebugDrawParams::GetCachedPosition(float TimeOffset, int8 SchemaBoneIdx) const
{
	if (const UPoseSearchSchema* Schema = GetSchema())
	{
		if (auto CachedPosition = CachedPositions.Find(TimeOffset, Schema->GetBoneIndexType(SchemaBoneIdx)))
		{
			return CachedPosition->Transform;
		}

		if (Mesh.IsValid() && SchemaBoneIdx >= 0)
		{
			return Mesh->GetSocketTransform(Schema->BoneReferences[SchemaBoneIdx].BoneName).GetTranslation();
		}
	}
	return RootTransform.GetTranslation();
}

void DrawFeatureVector(FDebugDrawParams& DrawParams, TConstArrayView<float> PoseVector)
{
	DrawParams.ClearCachedPositions();

	if (DrawParams.CanDraw())
	{
		const UPoseSearchSchema* Schema = DrawParams.GetSchema();
		check(Schema);

		if (PoseVector.Num() == Schema->SchemaCardinality)
		{
			for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : Schema->Channels)
			{
				if (ChannelPtr)
				{
					ChannelPtr->PreDebugDraw(DrawParams, PoseVector);
				}
			}

			for (const TObjectPtr<UPoseSearchFeatureChannel>& ChannelPtr : Schema->Channels)
			{
				if (ChannelPtr)
				{
					ChannelPtr->DebugDraw(DrawParams, PoseVector);
				}
			}
		}
	}
}

void DrawFeatureVector(FDebugDrawParams& DrawParams, int32 PoseIdx)
{
	// if we're editing the schema while in PIE with Rewind Debugger active, PoseIdx could be out of bound / stale
	if (DrawParams.CanDraw() && PoseIdx >= 0 && PoseIdx < DrawParams.GetSearchIndex()->GetNumPoses())
	{
		DrawFeatureVector(DrawParams, DrawParams.GetSearchIndex()->GetPoseValues(PoseIdx));
	}
}
#endif // ENABLE_DRAW_DEBUG

//////////////////////////////////////////////////////////////////////////
// FSearchContext
FQuat FSearchContext::GetSampleRotation(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot)
{
	// @todo: add support for SchemaSampleBoneIdx
	if (SchemaOriginBoneIdx != RootSchemaBoneIdx)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FSearchContext::GetSampleRotation: support for non root origin bones not implemented (bone: '%s', schema: '%s'"),
			*Schema->BoneReferences[SchemaOriginBoneIdx].BoneName.ToString(), *GetNameSafe(Schema));
	}

	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;

	// @todo: add support for OriginTime != 0 (like in GetSamplePosition and GetSampleVelocity)
	if (OriginTime != 0.f)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FSearchContext::GetSampleRotation: support for OriginTime != 0 not implemented (bone: '%s', schema: '%s'"),
			SchemaOriginBoneIdx >= 0 ? *Schema->BoneReferences[SchemaOriginBoneIdx].BoneName.ToString() : TEXT("RootBone"), *GetNameSafe(Schema));
	}

	return GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetRotation();
}

FVector FSearchContext::GetSamplePosition(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot)
{
	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;
	return GetSamplePositionInternal(SampleTime, OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);
}

FVector FSearchContext::GetSampleVelocity(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseCharacterSpaceVelocities, bool bUseHistoryRoot)
{
	const float SampleTime = SampleTimeOffset + PermutationSampleTimeOffset;
	const float OriginTime = PermutationOriginTimeOffset;
	const float FiniteDelta = History ? History->GetSampleTimeInterval() : 1 / 60.0f;
	check(FiniteDelta > UE_KINDA_SMALL_NUMBER);

	// calculating the Position in component space for the bone indexed by SchemaSampleBoneIdx
	const FVector PreviousTranslation = GetSamplePositionInternal(SampleTime - FiniteDelta, bUseCharacterSpaceVelocities ? OriginTime - FiniteDelta : OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);
	const FVector CurrentTranslation = GetSamplePositionInternal(SampleTime, OriginTime, Schema, SchemaSampleBoneIdx, SchemaOriginBoneIdx, bUseHistoryRoot);

	const FVector LinearVelocity = (CurrentTranslation - PreviousTranslation) / FiniteDelta;
	return LinearVelocity;
}

FTransform FSearchContext::GetTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx, bool bUseHistoryRoot)
{
	// collecting the RootTransform from the IPoseHistory
	FTransform RootTransform = FTransform::Identity;
	if (bUseHistoryRoot)
	{
		check(History);
		History->GetRootTransformAtTime(SampleTime, RootTransform);
	}
	else
	{
		check(Trajectory);
		const FTrajectorySample TrajectorySample = Trajectory->GetSampleAtTime(SampleTime);
		RootTransform = TrajectorySample.Transform;
	}

	const FBoneIndexType BoneIndexType = Schema->GetBoneIndexType(SchemaBoneIdx);
	if (BoneIndexType != RootBoneIndexType)
	{
		const FTransform BoneTransform = GetComponentSpaceTransform(SampleTime, Schema, SchemaBoneIdx);
		return BoneTransform * RootTransform;
	}

	return RootTransform;
}

FTransform FSearchContext::GetComponentSpaceTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx)
{
	check(Schema);

	const FBoneIndexType BoneIndexType = Schema->GetBoneIndexType(SchemaBoneIdx);
	if (BoneIndexType != RootBoneIndexType)
	{
		if (const FCachedTransform<FTransform>* CachedTransform = CachedTransforms.Find(SampleTime, BoneIndexType))
		{
			return CachedTransform->Transform;
		}
	
		// collecting the local bone transforms from the IPoseHistory
		check(History);
		FTransform BoneComponentSpaceTransform;
		if (!History->GetComponentSpaceTransformAtTime(SampleTime, BoneIndexType, BoneComponentSpaceTransform))
		{
			FName BoneName;
			if (const USkeleton* Skeleton = Schema->Skeleton)
			{
				BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneIndexType);
			}

			UE_LOG(LogPoseSearch, Warning, TEXT("FSearchContext::GetComponentSpaceTransform - Couldn't find BoneIndexType %d (%s) requested by %s"), BoneIndexType, *BoneName.ToString(), *Schema->GetName());
		}

		CachedTransforms.Add(SampleTime, BoneIndexType, BoneComponentSpaceTransform);
		return BoneComponentSpaceTransform;
	}

	return FTransform::Identity;
}

FVector FSearchContext::GetSamplePositionInternal(float SampleTime, float OriginTime, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx, int8 SchemaOriginBoneIdx, bool bUseHistoryRoot)
{
	if (SampleTime == OriginTime)
	{
		if (Schema->IsRootBone(SchemaOriginBoneIdx))
		{
			return GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetTranslation();
		}

		const FVector SampleBonePosition = GetComponentSpaceTransform(SampleTime, Schema, SchemaSampleBoneIdx).GetTranslation();
		const FVector OriginBonePosition = GetComponentSpaceTransform(OriginTime, Schema, SchemaOriginBoneIdx).GetTranslation();
		return SampleBonePosition - OriginBonePosition;
	}

	const FTransform RootBoneTransform = GetTransform(OriginTime, Schema, RootSchemaBoneIdx, bUseHistoryRoot);
	const FTransform SampleBoneTransform = GetTransform(SampleTime, Schema, SchemaSampleBoneIdx, bUseHistoryRoot);
	if (Schema->IsRootBone(SchemaOriginBoneIdx))
	{
		return RootBoneTransform.InverseTransformPosition(SampleBoneTransform.GetTranslation());
	}

	const FTransform OriginBoneTransform = GetTransform(OriginTime, Schema, SchemaOriginBoneIdx, bUseHistoryRoot);
	const FVector DeltaBoneTranslation = SampleBoneTransform.GetTranslation() - OriginBoneTransform.GetTranslation();
	return RootBoneTransform.InverseTransformVector(DeltaBoneTranslation);
}

void FSearchContext::SetPermutationTimeOffsets(float InPermutationSampleTimeOffset, float InPermutationOriginTimeOffset)
{
	// right now we disallow having nested channel controlling time offsets
	check(PermutationSampleTimeOffset == 0.f && PermutationOriginTimeOffset == 0.f);
	PermutationSampleTimeOffset = InPermutationSampleTimeOffset;
	PermutationOriginTimeOffset = InPermutationOriginTimeOffset;
}

void FSearchContext::ResetPermutationTimeOffsets()
{
	PermutationSampleTimeOffset = 0.f;
	PermutationOriginTimeOffset = 0.f;
}

void FSearchContext::ClearCachedEntries()
{
	CachedTransforms.Reset();
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

const FPoseSearchFeatureVectorBuilder* FSearchContext::GetCachedQuery(const UPoseSearchSchema* Schema) const
{
	const FPoseSearchFeatureVectorBuilder* CachedQuery = CachedQueries.FindByPredicate([Schema](const FPoseSearchFeatureVectorBuilder& CachedQuery)
	{
		return CachedQuery.GetSchema() == Schema;
	});

	if (CachedQuery)
	{
		return CachedQuery;
	}
	return nullptr;
}

void FSearchContext::GetOrBuildQuery(const UPoseSearchSchema* Schema, FPoseSearchFeatureVectorBuilder& FeatureVectorBuilder)
{
	check(Schema && Schema->IsValid());
	const FPoseSearchFeatureVectorBuilder* CachedFeatureVectorBuilder = GetCachedQuery(Schema);
	if (CachedFeatureVectorBuilder)
	{
		FeatureVectorBuilder = *CachedFeatureVectorBuilder;
	}
	else
	{
		FPoseSearchFeatureVectorBuilder& NewCachedQuery = CachedQueries[CachedQueries.AddDefaulted()];
		Schema->BuildQuery(*this, NewCachedQuery);
		FeatureVectorBuilder = NewCachedQuery;
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