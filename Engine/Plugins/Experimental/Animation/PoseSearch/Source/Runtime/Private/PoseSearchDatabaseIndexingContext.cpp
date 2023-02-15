// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseIndexingContext.h"

#if WITH_EDITOR

#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "InstancedStruct.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace UE::PoseSearch
{

void FDatabaseIndexingContext::Prepare(const UPoseSearchDatabase* Database)
{
	const UPoseSearchSchema* Schema = Database->Schema;
	check(Schema);

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Schema->Skeleton);

	TMap<const UAnimationAsset*, int32> SamplerMap;
	TMap<TPair<const UBlendSpace*, FVector>, int32> BlendSpaceSamplerMap;

	SamplingContext.Init(Schema->MirrorDataTable, BoneContainer);

	// Prepare samplers for all animation assets.
	for (const FInstancedStruct& DatabaseAssetStruct : Database->AnimationAssets)
	{
		auto AddSequenceBaseSampler = [&](const UAnimSequenceBase* Sequence)
		{
			if (Sequence && !SamplerMap.Contains(Sequence))
			{
				FSequenceBaseSampler::FInput Input;
				Input.ExtrapolationParameters = Database->ExtrapolationParameters;
				Input.SequenceBase = Sequence;

				SamplerMap.Add(Sequence, Samplers.Num());
				FSequenceBaseSampler* Sampler = Samplers.Add_GetRef(FInstancedStruct::Make(FSequenceBaseSampler())).GetMutablePtr<FSequenceBaseSampler>();
				check(Sampler);
				Sampler->Init(Input);
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
							Input.ExtrapolationParameters = Database->ExtrapolationParameters;
							Input.BlendSpace = DatabaseBlendSpace->BlendSpace;
							Input.BlendParameters = BlendParameters;

							BlendSpaceSamplerMap.Add({ DatabaseBlendSpace->BlendSpace, BlendParameters }, Samplers.Num());
							FBlendSpaceSampler* Sampler = Samplers.Add_GetRef(FInstancedStruct::Make(FBlendSpaceSampler())).GetMutablePtr<FBlendSpaceSampler>();
							check(Sampler);
							Sampler->Init(Input);
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
				Input.ExtrapolationParameters = Database->ExtrapolationParameters;
				Input.AnimMontage = DatabaseAnimMontage->AnimMontage;

				SamplerMap.Add(DatabaseAnimMontage->AnimMontage, Samplers.Num());
				FAnimMontageSampler* Sampler = Samplers.Add_GetRef(FInstancedStruct::Make(FAnimMontageSampler())).GetMutablePtr<FAnimMontageSampler>();
				check(Sampler);
				Sampler->Init(Input);
			}
		}
		else
		{
			checkNoEntry();
		}
	}

	ParallelFor(Samplers.Num(), [this](int32 SamplerIdx)
		{
			FAssetSamplerBase* AssetSamplerBase = Samplers[SamplerIdx].GetMutablePtr<FAssetSamplerBase>();
			check(AssetSamplerBase);
			AssetSamplerBase->Process();
		}, ParallelForFlags);

	// prepare indexers
	Indexers.Reserve(SearchIndexBase->Assets.Num());

	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase->Assets.Num(); ++AssetIdx)
	{
		const FPoseSearchIndexAsset& SearchIndexAsset = SearchIndexBase->Assets[AssetIdx];

		FAssetIndexingContext IndexerContext;
		IndexerContext.SamplingContext = &SamplingContext;
		IndexerContext.Schema = Schema;
		IndexerContext.RequestedSamplingRange = SearchIndexAsset.SamplingInterval;
		IndexerContext.bMirrored = SearchIndexAsset.bMirrored;

		const FInstancedStruct& DatabaseAsset = Database->GetAnimationAssetStruct(SearchIndexAsset.SourceAssetIdx);
		if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAsset.GetPtr<FPoseSearchDatabaseSequence>())
		{
			if (DatabaseSequence->Sequence)
			{
				const float SequenceLength = DatabaseSequence->Sequence->GetPlayLength();
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseSequence->Sequence]].GetPtr<FAssetSamplerBase>();
			}
		}
		else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			if (DatabaseAnimComposite->AnimComposite)
			{
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseAnimComposite->AnimComposite]].GetPtr<FAssetSamplerBase>();
			}
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			if (DatabaseBlendSpace->BlendSpace)
			{
				IndexerContext.AssetSampler = Samplers[BlendSpaceSamplerMap[{DatabaseBlendSpace->BlendSpace, SearchIndexAsset.BlendParameters}]].GetPtr<FAssetSamplerBase>();
			}
		}
		else if (const FPoseSearchDatabaseAnimMontage* DatabaseAnimMontage = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimMontage>())
		{
			if (DatabaseAnimMontage->AnimMontage)
			{
				IndexerContext.AssetSampler = Samplers[SamplerMap[DatabaseAnimMontage->AnimMontage]].GetPtr<FAssetSamplerBase>();
			}
		}
		else
		{
			checkNoEntry();
		}

		FAssetIndexer& Indexer = Indexers.AddDefaulted_GetRef();
		Indexer.Init(IndexerContext, BoneContainer);
	}
}

bool FDatabaseIndexingContext::IndexAssets()
{
	// Index asset data
	ParallelFor(Indexers.Num(), [this](int32 AssetIdx) { Indexers[AssetIdx].Process(); }, ParallelForFlags);
	return true;
}

float FDatabaseIndexingContext::CalculateMinCostAddend() const
{
	float MinCostAddend = 0.f;

	check(SearchIndexBase);
	if (!SearchIndexBase->PoseMetadata.IsEmpty())
	{
		MinCostAddend = MAX_FLT;
		for (const FPoseSearchPoseMetadata& PoseMetadata : SearchIndexBase->PoseMetadata)
		{
			if (PoseMetadata.CostAddend < MinCostAddend)
			{
				MinCostAddend = PoseMetadata.CostAddend;
			}
		}
	}
	return MinCostAddend;
}

void FDatabaseIndexingContext::JoinIndex()
{
	// Write index info to asset and count up total poses and storage required
	int32 TotalPoses = 0;
	int32 TotalFloats = 0;

	check(SearchIndexBase);

	// Join animation data into a single search index
	SearchIndexBase->Values.Reset();
	SearchIndexBase->PoseMetadata.Reset();
	SearchIndexBase->OverallFlags = EPoseSearchPoseFlags::None;
	SearchIndexBase->Stats = FPoseSearchStats();

	int32 NumAccumulatedSamples = 0;
	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase->Assets.Num(); ++AssetIdx)
	{
		const FAssetIndexer::FOutput& Output = Indexers[AssetIdx].GetOutput();

		FPoseSearchIndexAsset& SearchIndexAsset = SearchIndexBase->Assets[AssetIdx];
		SearchIndexAsset.NumPoses = Output.NumIndexedPoses;
		SearchIndexAsset.FirstPoseIdx = TotalPoses;

		const int32 PoseMetadataStartIdx = SearchIndexBase->PoseMetadata.Num();
		const int32 PoseMetadataEndIdx = PoseMetadataStartIdx + Output.PoseMetadata.Num();

		SearchIndexBase->Values.Append(Output.FeatureVectorTable.GetData(), Output.FeatureVectorTable.Num());
		SearchIndexBase->PoseMetadata.Append(Output.PoseMetadata);

		for (int32 i = PoseMetadataStartIdx; i < PoseMetadataEndIdx; ++i)
		{
			SearchIndexBase->PoseMetadata[i].AssetIndex = AssetIdx;
			SearchIndexBase->OverallFlags |= SearchIndexBase->PoseMetadata[i].Flags;
		}

		TotalPoses += Output.NumIndexedPoses;
		TotalFloats += Output.FeatureVectorTable.Num();

		const FAssetIndexer::FStats& Stats = Indexers[AssetIdx].GetStats();
		SearchIndexBase->Stats.AverageSpeed += Stats.AccumulatedSpeed;
		SearchIndexBase->Stats.MaxSpeed = FMath::Max(SearchIndexBase->Stats.MaxSpeed, Stats.MaxSpeed);
		SearchIndexBase->Stats.AverageAcceleration += Stats.AccumulatedAcceleration;
		SearchIndexBase->Stats.MaxAcceleration = FMath::Max(SearchIndexBase->Stats.MaxAcceleration, Stats.MaxAcceleration);

		NumAccumulatedSamples += Stats.NumAccumulatedSamples;
	}

	if (NumAccumulatedSamples > 0)
	{
		const float Denom = 1.f / float(NumAccumulatedSamples);
		SearchIndexBase->Stats.AverageSpeed *= Denom;
		SearchIndexBase->Stats.AverageAcceleration *= Denom;
	}

	SearchIndexBase->NumPoses = TotalPoses;
	SearchIndexBase->MinCostAddend = CalculateMinCostAddend();
}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
