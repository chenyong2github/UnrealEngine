// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PoseSearchDatabaseIndexingContext.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/MirrorDataTable.h"
#include "DerivedDataRequestOwner.h"
#include "InstancedStruct.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// FAssetSamplingContext
void FAssetSamplingContext::Init(const UMirrorDataTable* InMirrorDataTable, const FBoneContainer& BoneContainer)
{
	MirrorDataTable = InMirrorDataTable;

	if (InMirrorDataTable)
	{
		InMirrorDataTable->FillCompactPoseAndComponentRefRotations(BoneContainer, CompactPoseMirrorBones, ComponentSpaceRefRotations);
	}
	else
	{
		CompactPoseMirrorBones.Reset();
		ComponentSpaceRefRotations.Reset();
	}
}

FTransform FAssetSamplingContext::MirrorTransform(const FTransform& InTransform) const
{
	return UE::PoseSearch::MirrorTransform(InTransform, MirrorDataTable->MirrorAxis, ComponentSpaceRefRotations[FCompactPoseBoneIndex(RootBoneIndexType)]);
}

//////////////////////////////////////////////////////////////////////////
// FDatabaseIndexingContext
bool FDatabaseIndexingContext::IndexDatabase(FPoseSearchIndexBase& SearchIndexBase, const UPoseSearchDatabase& Database, UE::DerivedData::FRequestOwner& Owner)
{
	const UPoseSearchSchema* Schema = Database.Schema;
	check(Schema);

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Schema->BoneIndicesWithParents, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *Schema->Skeleton);

	SamplingContext.Init(Schema->MirrorDataTable, BoneContainer);

	if (Owner.IsCanceled())
	{
		return false;
	}

	// Prepare samplers for all animation assets.
	TMap<const UAnimationAsset*, int32> SamplerMap;
	TMap<TPair<const UBlendSpace*, FVector>, int32> BlendSpaceSamplerMap;
	for (const FInstancedStruct& DatabaseAssetStruct : Database.AnimationAssets)
	{
		auto AddSequenceBaseSampler = [&](const UAnimSequenceBase* Sequence)
		{
			if (Sequence && !SamplerMap.Contains(Sequence))
			{
				FSequenceBaseSampler::FInput Input;
				Input.SequenceBase = Sequence;

				SamplerMap.Add(Sequence, Samplers.Num());

				TSharedPtr<FSequenceBaseSampler> Sampler = MakeShared<FSequenceBaseSampler>();
				Sampler->Init(Input);
				Samplers.Add(Sampler);
			}
		};

		if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseSequence>())
		{
			AddSequenceBaseSampler(DatabaseSequence->Sequence);
		}
		else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			AddSequenceBaseSampler(DatabaseAnimComposite->AnimComposite);
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			if (DatabaseBlendSpace->BlendSpace)
			{
				int32 HorizontalBlendNum, VerticalBlendNum;
				DatabaseBlendSpace->GetBlendSpaceParameterSampleRanges(HorizontalBlendNum, VerticalBlendNum);

				for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalBlendNum; HorizontalIndex++)
				{
					for (int32 VerticalIndex = 0; VerticalIndex < VerticalBlendNum; VerticalIndex++)
					{
						const FVector BlendParameters = DatabaseBlendSpace->BlendParameterForSampleRanges(HorizontalIndex, VerticalIndex);

						if (!BlendSpaceSamplerMap.Contains({ DatabaseBlendSpace->BlendSpace, BlendParameters }))
						{
							FBlendSpaceSampler::FInput Input;
							Input.BoneContainer = BoneContainer;
							Input.BlendSpace = DatabaseBlendSpace->BlendSpace;
							Input.BlendParameters = BlendParameters;

							BlendSpaceSamplerMap.Add({ DatabaseBlendSpace->BlendSpace, BlendParameters }, Samplers.Num());

							TSharedPtr<FBlendSpaceSampler> Sampler = MakeShared<FBlendSpaceSampler>();
							Sampler->Init(Input);
							Samplers.Add(Sampler);
						}
					}
				}
			}
		}
		else if (const FPoseSearchDatabaseAnimMontage* DatabaseAnimMontage = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseAnimMontage>())
		{
			if (DatabaseAnimMontage->AnimMontage && !SamplerMap.Contains(DatabaseAnimMontage->AnimMontage))
			{
				FAnimMontageSampler::FInput Input;
				Input.AnimMontage = DatabaseAnimMontage->AnimMontage;

				SamplerMap.Add(DatabaseAnimMontage->AnimMontage, Samplers.Num());

				TSharedPtr<FAnimMontageSampler> Sampler = MakeShared<FAnimMontageSampler>();
				Sampler->Init(Input);
				Samplers.Add(Sampler);
			}
		}
		else
		{
			checkNoEntry();
		}
	}

	ParallelFor(Samplers.Num(), [this](int32 SamplerIdx) { Samplers[SamplerIdx]->Process(); }, ParallelForFlags);

	if (Owner.IsCanceled())
	{
		return false;
	}

	// prepare indexers
	Indexers.Reserve(SearchIndexBase.Assets.Num());

	int32 TotalPoses = 0;
	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase.Assets.Num(); ++AssetIdx)
	{
		FPoseSearchIndexAsset& SearchIndexAsset = SearchIndexBase.Assets[AssetIdx];
		SearchIndexAsset.FirstPoseIdx = TotalPoses;

		FAssetIndexingContext IndexerContext;
		IndexerContext.SamplingContext = &SamplingContext;
		IndexerContext.Schema = Schema;
		IndexerContext.RequestedSamplingRange = SearchIndexAsset.SamplingInterval;
		IndexerContext.bMirrored = SearchIndexAsset.bMirrored;

		const FInstancedStruct& DatabaseAsset = Database.GetAnimationAssetStruct(SearchIndexAsset.SourceAssetIdx);
		if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAsset.GetPtr<FPoseSearchDatabaseSequence>())
		{
			if (DatabaseSequence->Sequence)
			{
				const float SequenceLength = DatabaseSequence->Sequence->GetPlayLength();
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseSequence->Sequence]];
			}
		}
		else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			if (DatabaseAnimComposite->AnimComposite)
			{
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseAnimComposite->AnimComposite]];
			}
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			if (DatabaseBlendSpace->BlendSpace)
			{
				IndexerContext.AssetSampler = Samplers[BlendSpaceSamplerMap[{DatabaseBlendSpace->BlendSpace, SearchIndexAsset.BlendParameters}]];
			}
		}
		else if (const FPoseSearchDatabaseAnimMontage* DatabaseAnimMontage = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimMontage>())
		{
			if (DatabaseAnimMontage->AnimMontage)
			{
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseAnimMontage->AnimMontage]];
			}
		}
		else
		{
			checkNoEntry();
		}

		const int32 NewIndexerIdx = Indexers.Emplace(IndexerContext, BoneContainer, SearchIndexAsset);
		SearchIndexAsset.NumPoses = Indexers[NewIndexerIdx].GetNumIndexedPoses();
		TotalPoses += SearchIndexAsset.NumPoses;
	}

	// allocating Values and PoseMetadata
	SearchIndexBase.Values.Reset();
	SearchIndexBase.PoseMetadata.Reset();

	SearchIndexBase.Values.SetNumZeroed(Schema->SchemaCardinality * TotalPoses);
	SearchIndexBase.PoseMetadata.SetNumZeroed(TotalPoses);

	// assigning local data to each Indexer
	TotalPoses = 0;
	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase.Assets.Num(); ++AssetIdx)
	{
		const int32 NumIndexedPoses = Indexers[AssetIdx].GetNumIndexedPoses();
		Indexers[AssetIdx].AssignWorkingData(
			MakeArrayView(SearchIndexBase.Values.GetData() + Schema->SchemaCardinality * TotalPoses, Schema->SchemaCardinality * NumIndexedPoses),
			MakeArrayView(SearchIndexBase.PoseMetadata.GetData() + TotalPoses, NumIndexedPoses));
		TotalPoses += NumIndexedPoses;
	}

	if (Owner.IsCanceled())
	{
		return false;
	}

	// Index asset data
	ParallelFor(Indexers.Num(), [this](int32 AssetIdx) { Indexers[AssetIdx].Process(AssetIdx); }, ParallelForFlags);

	if (Owner.IsCanceled())
	{
		return false;
	}

	// Joining Metadata.Flags into OverallFlags
	SearchIndexBase.bAnyBlockTransition = false;
	for (const FPoseSearchPoseMetadata& Metadata : SearchIndexBase.PoseMetadata)
	{
		if (Metadata.IsBlockTransition())
		{
			SearchIndexBase.bAnyBlockTransition = true;
			break;
		}
	}

	// Joining Stats
	int32 NumAccumulatedSamples = 0;
	SearchIndexBase.Stats = FPoseSearchStats();
	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase.Assets.Num(); ++AssetIdx)
	{
		const FAssetIndexer::FStats& Stats = Indexers[AssetIdx].GetStats();
		SearchIndexBase.Stats.AverageSpeed += Stats.AccumulatedSpeed;
		SearchIndexBase.Stats.MaxSpeed = FMath::Max(SearchIndexBase.Stats.MaxSpeed, Stats.MaxSpeed);
		SearchIndexBase.Stats.AverageAcceleration += Stats.AccumulatedAcceleration;
		SearchIndexBase.Stats.MaxAcceleration = FMath::Max(SearchIndexBase.Stats.MaxAcceleration, Stats.MaxAcceleration);

		NumAccumulatedSamples += Stats.NumAccumulatedSamples;
	}

	if (NumAccumulatedSamples > 0)
	{
		const float Denom = 1.f / float(NumAccumulatedSamples);
		SearchIndexBase.Stats.AverageSpeed *= Denom;
		SearchIndexBase.Stats.AverageAcceleration *= Denom;
	}

	// Calculate Min Cost Addend
	SearchIndexBase.MinCostAddend = 0.f;
	if (!SearchIndexBase.PoseMetadata.IsEmpty())
	{
		SearchIndexBase.MinCostAddend = MAX_FLT;
		for (const FPoseSearchPoseMetadata& PoseMetadata : SearchIndexBase.PoseMetadata)
		{
			if (PoseMetadata.GetCostAddend() < SearchIndexBase.MinCostAddend)
			{
				SearchIndexBase.MinCostAddend = PoseMetadata.GetCostAddend();
			}
		}
	}

	if (Owner.IsCanceled())
	{
		return false;
	}

	return true;
}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
