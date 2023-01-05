// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchDerivedData.h"

#if WITH_EDITOR
#include "Animation/BlendSpace.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Misc/CoreDelegates.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearchEigenHelper.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/BulkDataRegistry.h"
#include "UObject/NoExportTypes.h"

namespace UE::PoseSearch
{
static const UE::DerivedData::FValueId Id(UE::DerivedData::FValueId::FromName("Data"));
static const UE::DerivedData::FCacheBucket Bucket("PoseSearchDatabase");

#if ENABLE_COOK_STATS
static FCookStats::FDDCResourceUsageStats UsageStats;
static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("MotionMatching.Usage"), TEXT(""));
	});
#endif

static float ComputeFeatureMeanDeviation(TConstArrayView<FFeatureChannelLayoutSet::FEntry> Entries, TConstArrayView<FPoseSearchIndexBase> SearchIndexBases, TConstArrayView<const UPoseSearchSchema*> Schemas)
{
	check(Schemas.Num() == SearchIndexBases.Num());
	
	const int32 EntriesNum = Entries.Num();
	check(EntriesNum > 0);

	const int32 Cardinality = Entries[0].Cardinality;
	check(Cardinality > 0);

	int32 TotalNumPoses = 0;
	for (int32 EntryIdx = 0; EntryIdx < EntriesNum; ++EntryIdx)
	{
		TotalNumPoses += SearchIndexBases[Entries[EntryIdx].SchemaIndex].NumPoses;
	}

	int32 AccumulatedNumPoses = 0;
	RowMajorMatrix CenteredSubPoseMatrix(TotalNumPoses, Cardinality);
	for (int32 EntryIdx = 0; EntryIdx < EntriesNum; ++EntryIdx)
	{
		const FFeatureChannelLayoutSet::FEntry& Entry = Entries[EntryIdx];
		check(Cardinality == Entry.Cardinality);

		const int32 DataSetIdx = Entry.SchemaIndex;

		const UPoseSearchSchema* Schema = Schemas[DataSetIdx];
		const FPoseSearchIndexBase& SearchIndex = SearchIndexBases[DataSetIdx];

		const int32 NumPoses = SearchIndex.NumPoses;

		// Map input buffer with NumPoses as rows and NumDimensions	as cols
		RowMajorMatrixMapConst PoseMatrixSourceMap(SearchIndex.Values.GetData(), NumPoses, Schema->SchemaCardinality);

		// Given the sub matrix for the features, find the average distance to the feature's centroid.
		CenteredSubPoseMatrix.block(AccumulatedNumPoses, 0, NumPoses, Cardinality) = PoseMatrixSourceMap.block(0, Entry.DataOffset, NumPoses, Cardinality);
		AccumulatedNumPoses += NumPoses;
	}

	RowMajorVector SampleMean = CenteredSubPoseMatrix.colwise().mean();
	CenteredSubPoseMatrix = CenteredSubPoseMatrix.rowwise() - SampleMean;

	// after mean centering the data, the average distance to the centroid is simply the average norm.
	const float FeatureMeanDeviation = CenteredSubPoseMatrix.rowwise().norm().mean();

	return FeatureMeanDeviation;
}

// it collects FFeatureChannelLayoutSet from all the Schemas (for example, figuring out the data offsets of SampledBones at a specific 
// SampleTimes for a UPoseSearchFeatureChannel_Pose for all the SearchIndexBases), and call ComputeFeatureMeanDeviation
static TArray<float> ComputeChannelsDeviations(TConstArrayView<FPoseSearchIndexBase> SearchIndexBases, TConstArrayView<const UPoseSearchSchema*> Schemas)
{
	// This function performs a modified z-score normalization where features are normalized
	// by mean absolute deviation rather than standard deviation. Both methods are preferable
	// here to min-max scaling because they preserve outliers.
	// 
	// Mean absolute deviation is preferred here over standard deviation because the latter
	// emphasizes outliers since squaring the distance from the mean increases variance 
	// exponentially rather than additively and square rooting the sum of squares does not 
	// remove that bias. [1]
	//
	// References:
	// [1] Gorard, S. (2005), "Revisiting a 90-Year-Old Debate: The Advantages of the Mean Deviation."
	//     British Journal of Educational Studies, 53: 417-430.

	using namespace Eigen;
	using namespace UE::PoseSearch;

	int32 ThisSchemaIndex = 0;
	check(SearchIndexBases.Num() == Schemas.Num() && Schemas.Num() > ThisSchemaIndex);
	const UPoseSearchSchema* ThisSchema = Schemas[ThisSchemaIndex];
	check(ThisSchema->IsValid());
	const int32 NumDimensions = ThisSchema->SchemaCardinality;

	TArray<float> MeanDeviations;
	MeanDeviations.Init(1.f, NumDimensions);
	RowMajorVectorMap MeanDeviationsMap(MeanDeviations.GetData(), 1, NumDimensions);

	const EPoseSearchDataPreprocessor DataPreprocessor = ThisSchema->DataPreprocessor;
	if (SearchIndexBases[ThisSchemaIndex].NumPoses > 0 && (DataPreprocessor == EPoseSearchDataPreprocessor::Normalize || DataPreprocessor == EPoseSearchDataPreprocessor::NormalizeOnlyByDeviation))
	{
		FFeatureChannelLayoutSet FeatureChannelLayoutSet;
		for (int32 SchemaIndex = 0; SchemaIndex < Schemas.Num(); ++SchemaIndex)
		{
			const UPoseSearchSchema* Schema = Schemas[SchemaIndex];

			FeatureChannelLayoutSet.CurrentSchemaIndex = SchemaIndex;
			FeatureChannelLayoutSet.CurrentSchema = Schema;
			for (int ChannelIdx = 0; ChannelIdx != Schema->Channels.Num(); ++ChannelIdx)
			{
				const UPoseSearchFeatureChannel* Channel = Schema->Channels[ChannelIdx].Get();
				Channel->PopulateChannelLayoutSet(FeatureChannelLayoutSet);
			}
		}

		for (auto Pair : FeatureChannelLayoutSet.EntriesMap)
		{
			const TArray<FFeatureChannelLayoutSet::FEntry>& Entries = Pair.Value;
			for (const FFeatureChannelLayoutSet::FEntry& Entry : Entries)
			{
				if (Entry.SchemaIndex == ThisSchemaIndex)
				{
					const float FeatureMeanDeviation = ComputeFeatureMeanDeviation(Entries, SearchIndexBases, Schemas);
					// the associated data to all the Entries data is going to be used to calculate the deviation of Deviation[Entry.DataOffset] to Deviation[Entry.DataOffset + Entry.Cardinality]

					// Fill the feature's corresponding scaling axes with the average distance
					// Avoid scaling by zero by leaving near-zero deviations as 1.0
					static const float MinFeatureMeanDeviation = 0.1f;
					MeanDeviationsMap.segment(Entry.DataOffset, Entry.Cardinality).setConstant(FeatureMeanDeviation > MinFeatureMeanDeviation ? FeatureMeanDeviation : 1.f);
				}
			}
		}
	}
	
	return MeanDeviations;
}
static inline FFloatInterval GetEffectiveSamplingRange(const UAnimSequenceBase* Sequence, FFloatInterval RequestedSamplingRange)
{
	const bool bSampleAll = (RequestedSamplingRange.Min == 0.0f) && (RequestedSamplingRange.Max == 0.0f);
	const float SequencePlayLength = Sequence->GetPlayLength();
	FFloatInterval Range;
	Range.Min = bSampleAll ? 0.0f : RequestedSamplingRange.Min;
	Range.Max = bSampleAll ? SequencePlayLength : FMath::Min(SequencePlayLength, RequestedSamplingRange.Max);
	return Range;
}

static void FindValidSequenceIntervals(const UAnimSequenceBase* SequenceBase, FFloatInterval SamplingRange, bool bIsLooping,
	const FPoseSearchExcludeFromDatabaseParameters& ExcludeFromDatabaseParameters, TArray<FFloatRange>& ValidRanges)
{
	check(SequenceBase);

	const float SequenceLength = SequenceBase->GetPlayLength();

	const FFloatInterval EffectiveSamplingInterval = GetEffectiveSamplingRange(SequenceBase, SamplingRange);
	FFloatRange EffectiveSamplingRange = FFloatRange::Inclusive(EffectiveSamplingInterval.Min, EffectiveSamplingInterval.Max);
	if (!bIsLooping)
	{
		const FFloatRange ExcludeFromDatabaseRange(ExcludeFromDatabaseParameters.SequenceStartInterval, SequenceLength - ExcludeFromDatabaseParameters.SequenceEndInterval);
		EffectiveSamplingRange = FFloatRange::Intersection(EffectiveSamplingRange, ExcludeFromDatabaseRange);
	}

	// start from a single interval defined by the database sequence sampling range
	ValidRanges.Empty();
	ValidRanges.Add(EffectiveSamplingRange);

	FAnimNotifyContext NotifyContext;
	SequenceBase->GetAnimNotifies(0.0f, SequenceLength, NotifyContext);

	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		if (const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify())
		{
			if (const UAnimNotifyState_PoseSearchExcludeFromDatabase* ExclusionNotifyState = Cast<const UAnimNotifyState_PoseSearchExcludeFromDatabase>(NotifyEvent->NotifyStateClass))
			{
				FFloatRange ExclusionRange = FFloatRange::Inclusive(NotifyEvent->GetTriggerTime(), NotifyEvent->GetEndTriggerTime());

				// Split every valid range based on the exclusion range just found. Because this might increase the 
				// number of ranges in ValidRanges, the algorithm iterates from end to start.
				for (int RangeIdx = ValidRanges.Num() - 1; RangeIdx >= 0; --RangeIdx)
				{
					FFloatRange EvaluatedRange = ValidRanges[RangeIdx];
					ValidRanges.RemoveAt(RangeIdx);

					TArray<FFloatRange> Diff = FFloatRange::Difference(EvaluatedRange, ExclusionRange);
					ValidRanges.Append(Diff);
				}
			}
		}
	}
}

static void InitSearchIndexAssets(FPoseSearchIndexBase& SearchIndex, UPoseSearchDatabase* Database)
{
	using namespace UE::PoseSearch;

	SearchIndex.Assets.Empty();
	TArray<FFloatRange> ValidRanges;
	TArray<FBlendSampleData> BlendSamples;

	for (int32 AnimationAssetIndex = 0; AnimationAssetIndex < Database->AnimationAssets.Num(); ++AnimationAssetIndex)
	{
		const FInstancedStruct& DatabaseAssetStruct = Database->GetAnimationAssetStruct(AnimationAssetIndex);
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseAnimationAssetBase>())
		{
			if (!DatabaseAsset->IsEnabled() || !DatabaseAsset->GetAnimationAsset())
			{
				continue;
			}

			const bool bAddUnmirrored = DatabaseAsset->GetMirrorOption() == EPoseSearchMirrorOption::UnmirroredOnly || DatabaseAsset->GetMirrorOption() == EPoseSearchMirrorOption::UnmirroredAndMirrored;
			const bool bAddMirrored = DatabaseAsset->GetMirrorOption() == EPoseSearchMirrorOption::MirroredOnly || DatabaseAsset->GetMirrorOption() == EPoseSearchMirrorOption::UnmirroredAndMirrored;

			if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseSequence>())
			{
				ValidRanges.Reset();
				FindValidSequenceIntervals(DatabaseSequence->Sequence, DatabaseSequence->SamplingRange, DatabaseSequence->IsLooping(), Database->ExcludeFromDatabaseParameters, ValidRanges);
				for (const FFloatRange& Range : ValidRanges)
				{
					if (bAddUnmirrored)
					{
						SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::Sequence, AnimationAssetIndex, false, FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
					}

					if (bAddMirrored)
					{
						SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::Sequence, AnimationAssetIndex, true, FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
					}
				}
			}
			else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseAnimComposite>())
			{
				ValidRanges.Reset();
				FindValidSequenceIntervals(DatabaseAnimComposite->AnimComposite, DatabaseAnimComposite->SamplingRange, DatabaseAnimComposite->IsLooping(), Database->ExcludeFromDatabaseParameters, ValidRanges);
				for (const FFloatRange& Range : ValidRanges)
				{
					if (bAddUnmirrored)
					{
						SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::AnimComposite, AnimationAssetIndex, false, FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
					}

					if (bAddMirrored)
					{
						SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::AnimComposite, AnimationAssetIndex, true, FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
					}
				}
			}
			else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseBlendSpace>())
			{
				int32 HorizontalBlendNum, VerticalBlendNum;
				DatabaseBlendSpace->GetBlendSpaceParameterSampleRanges(HorizontalBlendNum, VerticalBlendNum);

				const bool bWrapInputOnHorizontalAxis = DatabaseBlendSpace->BlendSpace->GetBlendParameter(0).bWrapInput;
				const bool bWrapInputOnVerticalAxis = DatabaseBlendSpace->BlendSpace->GetBlendParameter(1).bWrapInput;
				for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalBlendNum; HorizontalIndex++)
				{
					for (int32 VerticalIndex = 0; VerticalIndex < VerticalBlendNum; VerticalIndex++)
					{
						const FVector BlendParameters = DatabaseBlendSpace->BlendParameterForSampleRanges(HorizontalIndex, VerticalIndex);

						int32 TriangulationIndex = 0;
						DatabaseBlendSpace->BlendSpace->GetSamplesFromBlendInput(BlendParameters, BlendSamples, TriangulationIndex, true);

						float PlayLength = DatabaseBlendSpace->BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

						if (bAddUnmirrored)
						{
							SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::BlendSpace, AnimationAssetIndex, false, FFloatInterval(0.0f, PlayLength), BlendParameters));
						}

						if (bAddMirrored)
						{
							SearchIndex.Assets.Add(FPoseSearchIndexAsset(ESearchIndexAssetType::BlendSpace, AnimationAssetIndex, true, FFloatInterval(0.0f, PlayLength), BlendParameters));
						}
					}
				}
			}
			else
			{
				checkNoEntry();
			}
		}
	}
}

static void PreprocessSearchIndexWeights(FPoseSearchIndex& SearchIndex, const UPoseSearchSchema* Schema, TConstArrayView<float> Deviation)
{
	const int32 NumDimensions = Schema->SchemaCardinality;
	SearchIndex.WeightsSqrt.Init(1.f, NumDimensions);
	for (int ChannelIdx = 0; ChannelIdx != Schema->Channels.Num(); ++ChannelIdx)
	{
		const UPoseSearchFeatureChannel* Channel = Schema->Channels[ChannelIdx].Get();
		Channel->FillWeights(SearchIndex.WeightsSqrt);
	}

	EPoseSearchDataPreprocessor DataPreprocessor = Schema->DataPreprocessor;
	if (DataPreprocessor == EPoseSearchDataPreprocessor::Normalize)
	{
		// normalizing user weights: the idea behind this step is to be able to compare poses from databases using different schemas
		RowMajorVectorMap MapWeights(SearchIndex.WeightsSqrt.GetData(), 1, NumDimensions);
		const float WeightsSum = MapWeights.sum();
		if (!FMath::IsNearlyZero(WeightsSum))
		{
			MapWeights *= (1.f / WeightsSum);
		}
	}

	// extracting the square root
	for (int32 Dimension = 0; Dimension != NumDimensions; ++Dimension)
	{
		SearchIndex.WeightsSqrt[Dimension] = FMath::Sqrt(SearchIndex.WeightsSqrt[Dimension]);
	}

	if (DataPreprocessor == EPoseSearchDataPreprocessor::Normalize || DataPreprocessor == EPoseSearchDataPreprocessor::NormalizeOnlyByDeviation)
	{
		for (int32 Dimension = 0; Dimension != NumDimensions; ++Dimension)
		{
			// the idea here is to pre-multiply the weights by the inverse of the variance (proportional to the square of the deviation) to have a "weighted Mahalanobis" distance
			SearchIndex.WeightsSqrt[Dimension] /= Deviation[Dimension];
		}
	}
}

// it calculates Mean, PCAValues, and PCAProjectionMatrix
static void PreprocessSearchIndexPCAData(FPoseSearchIndex& SearchIndex, int32 NumDimensions, uint32 NumberOfPrincipalComponents, EPoseSearchMode PoseSearchMode)
{
	// binding SearchIndex.Values and SearchIndex.PCAValues Eigen row major matrix maps
	const int32 NumPoses = SearchIndex.NumPoses;

	SearchIndex.PCAValues.Reset();
	SearchIndex.Mean.Reset();
	SearchIndex.PCAProjectionMatrix.Reset();

	SearchIndex.PCAValues.AddZeroed(NumPoses * NumberOfPrincipalComponents);
	SearchIndex.Mean.AddZeroed(NumDimensions);
	SearchIndex.PCAProjectionMatrix.AddZeroed(NumDimensions * NumberOfPrincipalComponents);

#if WITH_EDITORONLY_DATA
	SearchIndex.PCAExplainedVariance = 0.f;
#endif

	if (NumDimensions > 0)
	{
		const RowMajorVectorMapConst MapWeightsSqrt(SearchIndex.WeightsSqrt.GetData(), 1, NumDimensions);
		const RowMajorMatrixMapConst MapValues(SearchIndex.Values.GetData(), NumPoses, NumDimensions);
		const RowMajorMatrix WeightedValues = MapValues.array().rowwise() * MapWeightsSqrt.array();
		RowMajorMatrixMap MapPCAValues(SearchIndex.PCAValues.GetData(), NumPoses, NumberOfPrincipalComponents);

		// calculating the mean
		RowMajorVectorMap Mean(SearchIndex.Mean.GetData(), 1, NumDimensions);
		Mean = WeightedValues.colwise().mean();

		// use the mean to center the data points
		const RowMajorMatrix CenteredValues = WeightedValues.rowwise() - Mean;

		// estimating the covariance matrix (with dimensionality of NumDimensions, NumDimensions)
		// formula: https://en.wikipedia.org/wiki/Covariance_matrix#Estimation
		// details: https://en.wikipedia.org/wiki/Estimation_of_covariance_matrices
		const ColMajorMatrix CovariantMatrix = (CenteredValues.transpose() * CenteredValues) / float(NumPoses - 1);
		const Eigen::SelfAdjointEigenSolver<ColMajorMatrix> EigenSolver(CovariantMatrix);

		check(EigenSolver.info() == Eigen::Success);

		// validating EigenSolver results
		const ColMajorMatrix EigenVectors = EigenSolver.eigenvectors().real();

		if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate && NumberOfPrincipalComponents == NumDimensions)
		{
			const RowMajorVector ReciprocalWeightsSqrt = MapWeightsSqrt.cwiseInverse();
			const RowMajorMatrix ProjectedValues = CenteredValues * EigenVectors;
			for (Eigen::Index RowIndex = 0; RowIndex < MapValues.rows(); ++RowIndex)
			{
				const RowMajorVector WeightedReconstructedPoint = ProjectedValues.row(RowIndex) * EigenVectors.transpose() + Mean;
				const RowMajorVector ReconstructedPoint = WeightedReconstructedPoint.array() * ReciprocalWeightsSqrt.array();
				const float Error = (ReconstructedPoint - MapValues.row(RowIndex)).squaredNorm();
				check(Error < UE_KINDA_SMALL_NUMBER);
			}
		}

		// sorting EigenVectors by EigenValues, so we pick the most significant ones to compose our PCA projection matrix.
		const RowMajorVector EigenValues = EigenSolver.eigenvalues().real();
		TArray<size_t> Indexer;
		Indexer.Reserve(NumDimensions);
		for (size_t DimensionIndex = 0; DimensionIndex < NumDimensions; ++DimensionIndex)
		{
			Indexer.Push(DimensionIndex);
		}
		Indexer.Sort([&EigenValues](size_t a, size_t b)
			{
				return EigenValues[a] > EigenValues[b];
			});

		// composing the PCA projection matrix with the PCANumComponents most significant EigenVectors
		ColMajorMatrixMap PCAProjectionMatrix(SearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);
		float AccumulatedVariance = 0.f;
		for (size_t PCAComponentIndex = 0; PCAComponentIndex < NumberOfPrincipalComponents; ++PCAComponentIndex)
		{
			PCAProjectionMatrix.col(PCAComponentIndex) = EigenVectors.col(Indexer[PCAComponentIndex]);
			AccumulatedVariance += EigenValues[Indexer[PCAComponentIndex]];
		}

#if WITH_EDITORONLY_DATA
		// calculating the total variance knowing that eigen values measure variance along the principal components:
		const float TotalVariance = EigenValues.sum();
		// and explained variance as ratio between AccumulatedVariance and TotalVariance: https://ro-che.info/articles/2017-12-11-pca-explained-variance
		SearchIndex.PCAExplainedVariance = TotalVariance > UE_KINDA_SMALL_NUMBER ? AccumulatedVariance / TotalVariance : 0.f;
#endif // WITH_EDITORONLY_DATA

		MapPCAValues = CenteredValues * PCAProjectionMatrix;

		if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate && NumberOfPrincipalComponents == NumDimensions)
		{
			const RowMajorVector ReciprocalWeightsSqrt = MapWeightsSqrt.cwiseInverse();
			for (Eigen::Index RowIndex = 0; RowIndex < MapValues.rows(); ++RowIndex)
			{
				const RowMajorVector WeightedReconstructedValues = MapPCAValues.row(RowIndex) * PCAProjectionMatrix.transpose() + Mean;
				const RowMajorVector ReconstructedValues = WeightedReconstructedValues.array() * ReciprocalWeightsSqrt.array();
				const float Error = (ReconstructedValues - MapValues.row(RowIndex)).squaredNorm();
				check(Error < UE_KINDA_SMALL_NUMBER);
			}
		}
	}
}

static void PreprocessSearchIndexKDTree(FPoseSearchIndex& SearchIndex, int32 NumDimensions, uint32 NumberOfPrincipalComponents, EPoseSearchMode PoseSearchMode, int32 KDTreeMaxLeafSize, int32 KDTreeQueryNumNeighbors)
{
	const int32 NumPoses = SearchIndex.NumPoses;
	SearchIndex.KDTree.Construct(NumPoses, NumberOfPrincipalComponents, SearchIndex.PCAValues.GetData(), KDTreeMaxLeafSize);

	if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate)
	{
		// testing the KDTree is returning the proper searches for all the points in pca space
		int32 NumberOfFailingPoints = 0;
		for (size_t PointIndex = 0; PointIndex < NumPoses; ++PointIndex)
		{
			TArray<size_t> ResultIndexes;
			TArray<float> ResultDistanceSqr;
			ResultIndexes.SetNum(KDTreeQueryNumNeighbors + 1);
			ResultDistanceSqr.SetNum(KDTreeQueryNumNeighbors + 1);
			FKDTree::KNNResultSet ResultSet(KDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr);
			SearchIndex.KDTree.FindNeighbors(ResultSet, &SearchIndex.PCAValues[PointIndex * NumberOfPrincipalComponents]);

			size_t ResultIndex = 0;
			for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
			{
				if (PointIndex == ResultIndexes[ResultIndex])
				{
					check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
					break;
				}
			}
			if (ResultIndex == ResultSet.Num())
			{
				++NumberOfFailingPoints;
			}
		}

		check(NumberOfFailingPoints == 0);

		// testing the KDTree is returning the proper searches for all the original points transformed in pca space
		NumberOfFailingPoints = 0;
		for (size_t PointIndex = 0; PointIndex < NumPoses; ++PointIndex)
		{
			TArray<size_t> ResultIndexes;
			TArray<float> ResultDistanceSqr;
			ResultIndexes.SetNum(KDTreeQueryNumNeighbors + 1);
			ResultDistanceSqr.SetNum(KDTreeQueryNumNeighbors + 1);
			FKDTree::KNNResultSet ResultSet(KDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr);

			const RowMajorVectorMapConst MapValues(&SearchIndex.Values[PointIndex * NumDimensions], 1, NumDimensions);
			const RowMajorVectorMapConst MapWeightsSqrt(SearchIndex.WeightsSqrt.GetData(), 1, NumDimensions);
			const RowMajorVectorMapConst Mean(SearchIndex.Mean.GetData(), 1, NumDimensions);
			const ColMajorMatrixMapConst PCAProjectionMatrix(SearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);

			const RowMajorMatrix WeightedValues = MapValues.array() * MapWeightsSqrt.array();
			const RowMajorMatrix CenteredValues = WeightedValues - Mean;
			const RowMajorVector ProjectedValues = CenteredValues * PCAProjectionMatrix;

			SearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedValues.data());

			size_t ResultIndex = 0;
			for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
			{
				if (PointIndex == ResultIndexes[ResultIndex])
				{
					check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
					break;
				}
			}
			if (ResultIndex == ResultSet.Num())
			{
				++NumberOfFailingPoints;
			}
		}

		check(NumberOfFailingPoints == 0);
	}
}

//////////////////////////////////////////////////////////////////////////
// FSamplingParam helpers
struct FSamplingParam
{
	float WrappedParam = 0.0f;
	int32 NumCycles = 0;

	// If the animation can't loop, WrappedParam contains the clamped value and whatever is left is stored here
	float Extrapolation = 0.0f;
};

static FSamplingParam WrapOrClampSamplingParam(bool bCanWrap, float SamplingParamExtent, float SamplingParam)
{
	// This is a helper function used by both time and distance sampling. A schema may specify time or distance
	// offsets that are multiple cycles of a clip away from the current pose being sampled.
	// And that time or distance offset may before the beginning of the clip (SamplingParam < 0.0f)
	// or after the end of the clip (SamplingParam > SamplingParamExtent). So this function
	// helps determine how many cycles need to be applied and what the wrapped value should be, clamping
	// if necessary.

	FSamplingParam Result;

	Result.WrappedParam = SamplingParam;

	const bool bIsSamplingParamExtentKindaSmall = SamplingParamExtent <= UE_KINDA_SMALL_NUMBER;
	if (!bIsSamplingParamExtentKindaSmall && bCanWrap)
	{
		if (SamplingParam < 0.0f)
		{
			while (Result.WrappedParam < 0.0f)
			{
				Result.WrappedParam += SamplingParamExtent;
				++Result.NumCycles;
			}
		}

		else
		{
			while (Result.WrappedParam > SamplingParamExtent)
			{
				Result.WrappedParam -= SamplingParamExtent;
				++Result.NumCycles;
			}
		}
	}

	const float ParamClamped = FMath::Clamp(Result.WrappedParam, 0.0f, SamplingParamExtent);
	if (ParamClamped != Result.WrappedParam)
	{
		check(bIsSamplingParamExtentKindaSmall || !bCanWrap);
		Result.Extrapolation = Result.WrappedParam - ParamClamped;
		Result.WrappedParam = ParamClamped;
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////
// FAssetIndexer
class FAssetIndexer : public IAssetIndexer
{
public:

	struct FOutput
	{
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedPoses = 0;

		TArray<float> FeatureVectorTable;
		TArray<FPoseSearchPoseMetadata> PoseMetadata;
		TBitArray<> AllFeaturesNotAdded;
	} Output;

	void Reset();
	void Init(const FAssetIndexingContext& IndexingContext, const FBoneContainer& InBoneContainer);
	bool Process();

public: // IAssetIndexer

	const FAssetIndexingContext& GetIndexingContext() const override { return IndexingContext; }
	FSampleInfo GetSampleInfo(float SampleTime) const override;
	FSampleInfo GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const override;
	const float GetSampleTimeFromDistance(float Distance) const override;
	FTransform MirrorTransform(const FTransform& Transform) const override;
	FTransform GetTransformAndCacheResults(float SampleTime, float OriginTime, int8 SchemaBoneIdx, bool& Clamped) override;

private:
	FPoseSearchPoseMetadata GetMetadata(int32 SampleIdx) const;

	struct CachedEntry
	{
		float SampleTime;
		float OriginTime;
		bool Clamped;

		// @todo: minimize the Entry memory footprint
		FTransform RootTransform;
		FCompactPose Pose;
		FCSPose<FCompactPose> ComponentSpacePose;
		FBlendedCurve UnusedCurve;
		UE::Anim::FStackAttributeContainer UnusedAtrribute;
		FAnimationPoseData AnimPoseData = { Pose, UnusedCurve, UnusedAtrribute };
	};

	FBoneContainer BoneContainer;
	FAssetIndexingContext IndexingContext;
	TArray<CachedEntry> CachedEntries;
};

void FAssetIndexer::Reset()
{
	Output.FirstIndexedSample = 0;
	Output.LastIndexedSample = 0;
	Output.NumIndexedPoses = 0;

	Output.FeatureVectorTable.Reset(0);
	Output.PoseMetadata.Reset(0);
	Output.AllFeaturesNotAdded.Reset();
}

void FAssetIndexer::Init(const FAssetIndexingContext& InIndexingContext, const FBoneContainer& InBoneContainer)
{
	check(InIndexingContext.Schema);
	check(InIndexingContext.Schema->IsValid());
	check(InIndexingContext.MainSampler);

	BoneContainer = InBoneContainer;
	IndexingContext = InIndexingContext;

	Reset();

	Output.FirstIndexedSample = FMath::FloorToInt(IndexingContext.RequestedSamplingRange.Min * IndexingContext.Schema->SampleRate);
	Output.LastIndexedSample = FMath::Max(0, FMath::CeilToInt(IndexingContext.RequestedSamplingRange.Max * IndexingContext.Schema->SampleRate));
	Output.NumIndexedPoses = Output.LastIndexedSample - Output.FirstIndexedSample + 1;

	Output.FeatureVectorTable.SetNumZeroed(IndexingContext.Schema->SchemaCardinality * Output.NumIndexedPoses);
	Output.PoseMetadata.SetNum(Output.NumIndexedPoses);
}

bool FAssetIndexer::Process()
{
	check(IndexingContext.Schema);
	check(IndexingContext.Schema->IsValid());
	check(IndexingContext.MainSampler);

	FMemMark Mark(FMemStack::Get());

	IndexingContext.BeginSampleIdx = Output.FirstIndexedSample;
	IndexingContext.EndSampleIdx = Output.LastIndexedSample + 1;

	if (IndexingContext.Schema->SchemaCardinality > 0)
	{
		// Index each channel
		FAssetIndexingOutput AssetIndexingOutput(IndexingContext.Schema->SchemaCardinality, Output.FeatureVectorTable);
		for (int32 ChannelIdx = 0; ChannelIdx != IndexingContext.Schema->Channels.Num(); ++ChannelIdx)
		{
			IndexingContext.Schema->Channels[ChannelIdx]->IndexAsset(*this, AssetIndexingOutput);
		}
	}

	// Generate pose metadata
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		const int32 PoseIdx = SampleIdx - Output.FirstIndexedSample;
		FPoseSearchPoseMetadata& OutputPoseMetadata = Output.PoseMetadata[PoseIdx];
		OutputPoseMetadata = GetMetadata(SampleIdx);
	}

	return true;
}

const float FAssetIndexer::GetSampleTimeFromDistance(float SampleDistance) const
{
	auto CanWrapDistanceSamples = [](const IAssetSampler* Sampler) -> bool
	{
		constexpr float SMALL_ROOT_DISTANCE = 1.0f;
		return Sampler->IsLoopable() && Sampler->GetTotalRootDistance() > SMALL_ROOT_DISTANCE;
	};

	float MainTotalDistance = IndexingContext.MainSampler->GetTotalRootDistance();
	bool bMainCanWrap = CanWrapDistanceSamples(IndexingContext.MainSampler);

	float SampleTime = MAX_flt;

	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (IndexingContext.LeadInSampler && (SampleDistance < 0.0f))
		{
			const IAssetSampler* ClipSampler = IndexingContext.LeadInSampler;

			bool bLeadInCanWrap = CanWrapDistanceSamples(IndexingContext.LeadInSampler);
			float LeadRelativeDistance = SampleDistance + ClipSampler->GetTotalRootDistance();
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipSampler->GetTotalRootDistance(), LeadRelativeDistance);

			float ClipTime = ClipSampler->GetTimeFromRootDistance(
				SamplingParam.WrappedParam + SamplingParam.Extrapolation);

			// Make the lead in clip time relative to the main sequence again and unwrap
			SampleTime = -((SamplingParam.NumCycles * ClipSampler->GetPlayLength()) + (ClipSampler->GetPlayLength() - ClipTime));
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (IndexingContext.FollowUpSampler && (SampleDistance > MainTotalDistance))
		{
			const IAssetSampler* ClipSampler = IndexingContext.FollowUpSampler;

			bool bFollowUpCanWrap = CanWrapDistanceSamples(IndexingContext.FollowUpSampler);
			float FollowRelativeDistance = SampleDistance - MainTotalDistance;
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipSampler->GetTotalRootDistance(), FollowRelativeDistance);

			float ClipTime = ClipSampler->GetTimeFromRootDistance(
				SamplingParam.WrappedParam + SamplingParam.Extrapolation);

			// Make the follow up clip time relative to the main sequence again and unwrap
			SampleTime = IndexingContext.MainSampler->GetPlayLength() + SamplingParam.NumCycles * ClipSampler->GetPlayLength() + ClipTime;
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (SampleTime == MAX_flt)
	{
		float MainRelativeDistance = SampleDistance;
		if (SampleDistance < 0.0f && bMainCanWrap)
		{
			// In this case we're sampling a loop backwards, so MainRelativeDistance must adjust so the number of cycles 
			// is counted correctly.
			MainRelativeDistance += IndexingContext.MainSampler->GetTotalRootDistance();
		}

		FSamplingParam SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainTotalDistance, MainRelativeDistance);
		float ClipTime = IndexingContext.MainSampler->GetTimeFromRootDistance(
			SamplingParam.WrappedParam + SamplingParam.Extrapolation);

		// Unwrap the main clip time
		if (bMainCanWrap)
		{
			if (SampleDistance < 0.0f)
			{
				SampleTime = -((SamplingParam.NumCycles * IndexingContext.MainSampler->GetPlayLength()) + (IndexingContext.MainSampler->GetPlayLength() - ClipTime));
			}
			else
			{
				SampleTime = SamplingParam.NumCycles * IndexingContext.MainSampler->GetPlayLength() + ClipTime;
			}
		}
		else
		{
			SampleTime = ClipTime;
		}
	}

	return SampleTime;
}

FAssetIndexer::FSampleInfo FAssetIndexer::GetSampleInfo(float SampleTime) const
{
	FSampleInfo Sample;

	FTransform RootMotionLast = FTransform::Identity;
	FTransform RootMotionInitial = FTransform::Identity;

	float RootDistanceLast = 0.0f;
	float RootDistanceInitial = 0.0f;

	check(IndexingContext.MainSampler);

	const float MainPlayLength = IndexingContext.MainSampler->GetPlayLength();
	const bool bMainCanWrap = IndexingContext.MainSampler->IsLoopable();

	FSamplingParam SamplingParam;
	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (IndexingContext.LeadInSampler && (SampleTime < 0.0f))
		{
			const IAssetSampler* ClipSampler = IndexingContext.LeadInSampler;

			const bool bLeadInCanWrap = IndexingContext.LeadInSampler->IsLoopable();
			const float LeadRelativeTime = SampleTime + ClipSampler->GetPlayLength();
			SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipSampler->GetPlayLength(), LeadRelativeTime);

			Sample.Clip = IndexingContext.LeadInSampler;

			check(SamplingParam.Extrapolation <= 0.0f);
			if (SamplingParam.Extrapolation < 0.0f)
			{
				RootMotionInitial = IndexingContext.LeadInSampler->GetTotalRootTransform().Inverse();
				RootDistanceInitial = -IndexingContext.LeadInSampler->GetTotalRootDistance();
			}
			else
			{
				RootMotionInitial = FTransform::Identity;
				RootDistanceInitial = 0.0f;
			}

			RootMotionLast = IndexingContext.LeadInSampler->GetTotalRootTransform();
			RootDistanceLast = IndexingContext.LeadInSampler->GetTotalRootDistance();
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (IndexingContext.FollowUpSampler && (SampleTime > MainPlayLength))
		{
			const IAssetSampler* ClipSampler = IndexingContext.FollowUpSampler;

			const bool bFollowUpCanWrap = IndexingContext.FollowUpSampler->IsLoopable();
			const float FollowRelativeTime = SampleTime - MainPlayLength;
			SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipSampler->GetPlayLength(), FollowRelativeTime);

			Sample.Clip = IndexingContext.FollowUpSampler;

			RootMotionInitial = IndexingContext.MainSampler->GetTotalRootTransform();
			RootDistanceInitial = IndexingContext.MainSampler->GetTotalRootDistance();

			RootMotionLast = IndexingContext.FollowUpSampler->GetTotalRootTransform();
			RootDistanceLast = IndexingContext.FollowUpSampler->GetTotalRootDistance();
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (!Sample.IsValid())
	{
		float MainRelativeTime = SampleTime;
		if (SampleTime < 0.0f && bMainCanWrap)
		{
			// In this case we're sampling a loop backwards, so MainRelativeTime must adjust so the number of cycles is
			// counted correctly.
			MainRelativeTime += MainPlayLength;
		}

		SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainPlayLength, MainRelativeTime);

		Sample.Clip = IndexingContext.MainSampler;

		RootMotionInitial = FTransform::Identity;
		RootDistanceInitial = 0.0f;

		RootMotionLast = IndexingContext.MainSampler->GetTotalRootTransform();
		RootDistanceLast = IndexingContext.MainSampler->GetTotalRootDistance();
	}


	if (FMath::Abs(SamplingParam.Extrapolation) > SMALL_NUMBER)
	{
		Sample.bClamped = true;
		Sample.ClipTime = SamplingParam.WrappedParam + SamplingParam.Extrapolation;
		const FTransform ClipRootMotion = Sample.Clip->ExtractRootTransform(Sample.ClipTime);
		const float ClipDistance = Sample.Clip->ExtractRootDistance(Sample.ClipTime);

		Sample.RootTransform = ClipRootMotion * RootMotionInitial;
		Sample.RootDistance = RootDistanceInitial + ClipDistance;
	}
	else
	{
		Sample.ClipTime = SamplingParam.WrappedParam;

		// Determine how to accumulate motion for every cycle of the anim. If the sample
		// had to be clamped, this motion will end up not getting applied below.
		// Also invert the accumulation direction if the requested sample was wrapped backwards.
		FTransform RootMotionPerCycle = RootMotionLast;
		float RootDistancePerCycle = RootDistanceLast;
		if (SampleTime < 0.0f)
		{
			RootMotionPerCycle = RootMotionPerCycle.Inverse();
			RootDistancePerCycle *= -1.f;
		}

		// Find the remaining motion deltas after wrapping
		FTransform RootMotionRemainder = Sample.Clip->ExtractRootTransform(Sample.ClipTime);
		float RootDistanceRemainder = Sample.Clip->ExtractRootDistance(Sample.ClipTime);

		// Invert motion deltas if we wrapped backwards
		if (SampleTime < 0.0f)
		{
			RootMotionRemainder.SetToRelativeTransform(RootMotionLast);
			RootDistanceRemainder = -(RootDistanceLast - RootDistanceRemainder);
		}

		Sample.RootTransform = RootMotionInitial;
		Sample.RootDistance = RootDistanceInitial;

		// Note if the sample was clamped, no motion will be applied here because NumCycles will be zero
		int32 CyclesRemaining = SamplingParam.NumCycles;
		while (CyclesRemaining--)
		{
			Sample.RootTransform = RootMotionPerCycle * Sample.RootTransform;
			Sample.RootDistance += RootDistancePerCycle;
		}

		Sample.RootTransform = RootMotionRemainder * Sample.RootTransform;
		Sample.RootDistance += RootDistanceRemainder;
	}

	return Sample;
}

FAssetIndexer::FSampleInfo FAssetIndexer::GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const
{
	FSampleInfo Sample = GetSampleInfo(SampleTime);
	Sample.RootTransform.SetToRelativeTransform(Origin.RootTransform);
	Sample.RootDistance = Origin.RootDistance - Sample.RootDistance;
	return Sample;
}

FTransform FAssetIndexer::MirrorTransform(const FTransform& Transform) const
{
	return IndexingContext.bMirrored ? IndexingContext.SamplingContext->MirrorTransform(Transform) : Transform;
}

FPoseSearchPoseMetadata FAssetIndexer::GetMetadata(int32 SampleIdx) const
{
	const float SequenceLength = IndexingContext.MainSampler->GetPlayLength();
	const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->GetSamplingInterval(), SequenceLength);

	FPoseSearchPoseMetadata Metadata;
	Metadata.CostAddend = IndexingContext.Schema->BaseCostBias;
	Metadata.ContinuingPoseCostAddend = IndexingContext.Schema->ContinuingPoseCostBias;

	TArray<UAnimNotifyState_PoseSearchBase*> NotifyStates;
	IndexingContext.MainSampler->ExtractPoseSearchNotifyStates(SampleTime, NotifyStates);
	for (const UAnimNotifyState_PoseSearchBase* PoseSearchNotify : NotifyStates)
	{
		if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchBlockTransition>())
		{
			EnumAddFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition);
		}
		else if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchModifyCost>())
		{
			const UAnimNotifyState_PoseSearchModifyCost* ModifyCostNotify =
				Cast<const UAnimNotifyState_PoseSearchModifyCost>(PoseSearchNotify);
			Metadata.CostAddend = ModifyCostNotify->CostAddend;
		}
		else if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>())
		{
			const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingPoseCostBias =
				Cast<const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(PoseSearchNotify);
			Metadata.ContinuingPoseCostAddend = ContinuingPoseCostBias->CostAddend;
		}
	}
	return Metadata;
}

FTransform FAssetIndexer::GetTransformAndCacheResults(float SampleTime, float OriginTime, int8 SchemaBoneIdx, bool& Clamped)
{
	// @todo: use an hashmap if we end up having too many entries
	CachedEntry* Entry = CachedEntries.FindByPredicate([SampleTime, OriginTime](const FAssetIndexer::CachedEntry& Entry)
		{
			return Entry.SampleTime == SampleTime && Entry.OriginTime == OriginTime;
		});

	const FAssetSamplingContext* SamplingContext = IndexingContext.SamplingContext;

	if (!Entry)
	{
		Entry = &CachedEntries[CachedEntries.AddDefaulted()];

		Entry->SampleTime = SampleTime;
		Entry->OriginTime = OriginTime;

		if (!BoneContainer.IsValid())
		{
			UE_LOG(LogPoseSearch,
				Warning,
				TEXT("Invalid BoneContainer encountered in FAssetIndexer::GetTransformAndCacheResults. Asset: %s. Schema: %s. BoneContainerAsset: %s. NumBoneIndices: %d"),
				*GetNameSafe(IndexingContext.MainSampler->GetAsset()),
				*GetNameSafe(IndexingContext.Schema),
				*GetNameSafe(BoneContainer.GetAsset()),
				BoneContainer.GetCompactPoseNumBones());
		}

		Entry->Pose.SetBoneContainer(&BoneContainer);
		Entry->UnusedCurve.InitFrom(BoneContainer);

		IAssetIndexer::FSampleInfo Origin = GetSampleInfo(OriginTime);
		IAssetIndexer::FSampleInfo Sample = GetSampleInfoRelative(SampleTime, Origin);

		float CurrentTime = Sample.ClipTime;
		float PreviousTime = CurrentTime - SamplingContext->FiniteDelta;

		check(Sample.Clip->IsLoopable() || PreviousTime <= Sample.Clip->GetPlayLength());

		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(static_cast<double>(CurrentTime), true, DeltaTimeRecord, Sample.Clip->IsLoopable());

		Sample.Clip->ExtractPose(ExtractionCtx, Entry->AnimPoseData);

		if (IndexingContext.bMirrored)
		{
			FAnimationRuntime::MirrorPose(
				Entry->AnimPoseData.GetPose(),
				IndexingContext.Schema->MirrorDataTable->MirrorAxis,
				SamplingContext->CompactPoseMirrorBones,
				SamplingContext->ComponentSpaceRefRotations
			);
			// Note curves and attributes are not used during the indexing process and therefore don't need to be mirrored
		}

		Entry->ComponentSpacePose.InitPose(Entry->Pose);
		Entry->RootTransform = Sample.RootTransform;
		Entry->Clamped = Sample.bClamped;
	}

	const FBoneReference& BoneReference = IndexingContext.Schema->BoneReferences[SchemaBoneIdx];
	FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneReference.BoneIndex));

	const FTransform BoneTransform = Entry->ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex) * MirrorTransform(Entry->RootTransform);
	Clamped = Entry->Clamped;

	return BoneTransform;
}

//////////////////////////////////////////////////////////////////////////
// FDatabaseIndexingContext
struct FDatabaseIndexingContext
{
	FPoseSearchIndexBase* SearchIndexBase = nullptr;

	FAssetSamplingContext SamplingContext;
	TArray<FSequenceBaseSampler> SequenceSamplers; // Composite and sequence samplers
	TArray<FBlendSpaceSampler> BlendSpaceSamplers;

	TArray<FAssetIndexer> Indexers;

	void Prepare(const UPoseSearchDatabase* Database);
	bool IndexAssets();
	void JoinIndex();
	float CalculateMinCostAddend() const;
};

void FDatabaseIndexingContext::Prepare(const UPoseSearchDatabase* Database)
{
	const UPoseSearchSchema* Schema = Database->Schema;
	check(Schema);

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Schema->Skeleton);

	TMap<const UAnimSequenceBase*, int32> SequenceSamplerMap;
	TMap<TPair<const UBlendSpace*, FVector>, int32> BlendSpaceSamplerMap;

	SamplingContext.Init(Schema->MirrorDataTable, BoneContainer);

	// Prepare samplers for all animation assets.
	for (const FInstancedStruct& DatabaseAssetStruct : Database->AnimationAssets)
	{
		auto AddSequenceBaseSampler = [&](const UAnimSequenceBase* Sequence)
		{
			if (Sequence && !SequenceSamplerMap.Contains(Sequence))
			{
				int32 SequenceSamplerIdx = SequenceSamplers.AddDefaulted();
				SequenceSamplerMap.Add(Sequence, SequenceSamplerIdx);

				FSequenceBaseSampler::FInput Input;
				Input.ExtrapolationParameters = Database->ExtrapolationParameters;
				Input.SequenceBase = Sequence;
				SequenceSamplers[SequenceSamplerIdx].Init(Input);
			}
		};

		if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAssetStruct.GetPtr<FPoseSearchDatabaseSequence>())
		{
			AddSequenceBaseSampler(DatabaseSequence->Sequence);
			AddSequenceBaseSampler(DatabaseSequence->LeadInSequence);
			AddSequenceBaseSampler(DatabaseSequence->FollowUpSequence);
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
							int32 BlendSpaceSamplerIdx = BlendSpaceSamplers.AddDefaulted();
							BlendSpaceSamplerMap.Add({ DatabaseBlendSpace->BlendSpace, BlendParameters }, BlendSpaceSamplerIdx);

							FBlendSpaceSampler::FInput Input;
							Input.BoneContainer = BoneContainer;
							Input.ExtrapolationParameters = Database->ExtrapolationParameters;
							Input.BlendSpace = DatabaseBlendSpace->BlendSpace;
							Input.BlendParameters = BlendParameters;

							BlendSpaceSamplers[BlendSpaceSamplerIdx].Init(Input);
						}
					}
				}
			}
		}
	}

	TArray<IAssetSampler*, TInlineAllocator<512>> AssetSampler;
	AssetSampler.SetNumUninitialized(SequenceSamplers.Num() + BlendSpaceSamplers.Num());

	for (int i = 0; i < SequenceSamplers.Num(); ++i)
	{
		AssetSampler[i] = &SequenceSamplers[i];
	}
	for (int i = 0; i < BlendSpaceSamplers.Num(); ++i)
	{
		AssetSampler[i + SequenceSamplers.Num()] = &BlendSpaceSamplers[i];
	}

	ParallelFor(AssetSampler.Num(), [AssetSampler](int32 SamplerIdx) { AssetSampler[SamplerIdx]->Process(); }, ParallelForFlags);

	// prepare indexers
	Indexers.Reserve(SearchIndexBase->Assets.Num());

	auto GetSequenceBaseSampler = [&](const UAnimSequenceBase* Sequence) -> const FSequenceBaseSampler*
	{
		return Sequence ? &SequenceSamplers[SequenceSamplerMap[Sequence]] : nullptr;
	};

	auto GetBlendSpaceSampler = [&](const UBlendSpace* BlendSpace, const FVector BlendParameters) -> const FBlendSpaceSampler*
	{
		return BlendSpace ? &BlendSpaceSamplers[BlendSpaceSamplerMap[{BlendSpace, BlendParameters}]] : nullptr;
	};

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
			const float SequenceLength = DatabaseSequence->Sequence->GetPlayLength();
			IndexerContext.MainSampler = GetSequenceBaseSampler(DatabaseSequence->Sequence);
			IndexerContext.LeadInSampler = SearchIndexAsset.SamplingInterval.Min == 0.0f ? GetSequenceBaseSampler(DatabaseSequence->LeadInSequence) : nullptr;
			IndexerContext.FollowUpSampler = SearchIndexAsset.SamplingInterval.Max == SequenceLength ? GetSequenceBaseSampler(DatabaseSequence->FollowUpSequence) : nullptr;
		}
		else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			IndexerContext.MainSampler = GetSequenceBaseSampler(DatabaseAnimComposite->AnimComposite);
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			IndexerContext.MainSampler = GetBlendSpaceSampler(DatabaseBlendSpace->BlendSpace, SearchIndexAsset.BlendParameters);
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

	for (int32 AssetIdx = 0; AssetIdx != SearchIndexBase->Assets.Num(); ++AssetIdx)
	{
		const FAssetIndexer::FOutput& Output = Indexers[AssetIdx].Output;

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
	}

	SearchIndexBase->NumPoses = TotalPoses;
	SearchIndexBase->MinCostAddend = CalculateMinCostAddend();
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchDatabaseAsyncCacheTask
struct FPoseSearchDatabaseAsyncCacheTask
{
	enum class EState
	{
		Prestarted,
		Cancelled,
		Ended,
		Failed
	};

	// these methods MUST be protected by FPoseSearchDatabaseAsyncCacheTask::Mutex! and to make sure we pass the mutex as input param
	FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase* InDatabase, FCriticalSection& OuterMutex);
	void StartNewRequestIfNeeded(FCriticalSection& OuterMutex);
	bool CancelIfDependsOn(const UObject* Object, FCriticalSection& OuterMutex);
	void Update(FCriticalSection& OuterMutex);
	void Wait(FCriticalSection& OuterMutex);
	void Cancel(FCriticalSection& OuterMutex);
	bool Poll(FCriticalSection& OuterMutex) const;
	bool ContainsDatabase(const UPoseSearchDatabase* OtherDatabase, FCriticalSection& OuterMutex) const;

	~FPoseSearchDatabaseAsyncCacheTask();
	EState GetState() const { return EState(ThreadSafeState.GetValue()); }

private:
	FPoseSearchDatabaseAsyncCacheTask(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
	FPoseSearchDatabaseAsyncCacheTask(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;
	FPoseSearchDatabaseAsyncCacheTask& operator=(const FPoseSearchDatabaseAsyncCacheTask& Other) = delete;
	FPoseSearchDatabaseAsyncCacheTask& operator=(FPoseSearchDatabaseAsyncCacheTask&& Other) = delete;

	void OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response);
	void SetState(EState State) { ThreadSafeState.Set(int32(State)); }

	TWeakObjectPtr<UPoseSearchDatabase> Database;
	// @todo: this is not relevant when the async task is completed, so to save memory we should move it as pointer perhaps
	FPoseSearchIndex SearchIndex;
	UE::DerivedData::FRequestOwner Owner;
	FIoHash DerivedDataKey = FIoHash::Zero;
	TSet<TWeakObjectPtr<const UObject>> DatabaseDependencies; // @todo: make this const
		
	FThreadSafeCounter ThreadSafeState = int32(EState::Prestarted);
	bool bBroadcastOnDerivedDataRebuild = false;
};

class FPoseSearchDatabaseAsyncCacheTasks : public TArray<TUniquePtr<FPoseSearchDatabaseAsyncCacheTask>> {};

FPoseSearchDatabaseAsyncCacheTask::FPoseSearchDatabaseAsyncCacheTask(UPoseSearchDatabase* InDatabase, FCriticalSection& OuterMutex)
	: Database(InDatabase)
	, Owner(UE::DerivedData::EPriority::Normal)
	, DerivedDataKey(FIoHash::Zero)
{
	StartNewRequestIfNeeded(OuterMutex);
}

FPoseSearchDatabaseAsyncCacheTask::~FPoseSearchDatabaseAsyncCacheTask()
{
	Database = nullptr;
	SearchIndex.Reset();
	Owner.Cancel();
	DerivedDataKey = FIoHash::Zero;
	DatabaseDependencies.Reset();
}

void FPoseSearchDatabaseAsyncCacheTask::StartNewRequestIfNeeded(FCriticalSection& OuterMutex)
{
	using namespace UE::DerivedData;

	FScopeLock Lock(&OuterMutex);

	// making sure there are no active requests
	Owner.Cancel();

	// composing the key
	const FKeyBuilder KeyBuilder(Database.Get(), true);
	const FIoHash NewDerivedDataKey(KeyBuilder.Finalize());
	const bool bHasKeyChanged = NewDerivedDataKey != DerivedDataKey;
	if (bHasKeyChanged)
	{
		DerivedDataKey = NewDerivedDataKey;

		DatabaseDependencies.Reset();
		for (const UObject* Dependency : KeyBuilder.GetDependencies())
		{
			DatabaseDependencies.Add(Dependency);
		}

		SetState(EState::Prestarted);

		UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BeginCache"), *LexToString(DerivedDataKey), *Database->GetName());

		TArray<FCacheGetRequest> CacheRequests;
		const FCacheKey CacheKey{ Bucket, DerivedDataKey };
		CacheRequests.Add({ { Database->GetPathName() }, CacheKey, ECachePolicy::Default });

		Owner = FRequestOwner(EPriority::Normal);
		GetCache().Get(CacheRequests, Owner, [this](FCacheGetResponse&& Response)
			{
				OnGetComplete(MoveTemp(Response));
			});
	}
}

// it cancels and waits for the task to be done and reset the local SearchIndex. SetState to Cancelled
void FPoseSearchDatabaseAsyncCacheTask::Cancel(FCriticalSection& OuterMutex)
{
	FScopeLock Lock(&OuterMutex);

	Owner.Cancel();
	SearchIndex.Reset();
	DerivedDataKey = FIoHash::Zero;
	SetState(EState::Cancelled);
}

bool FPoseSearchDatabaseAsyncCacheTask::CancelIfDependsOn(const UObject* Object, FCriticalSection& OuterMutex)
{
	FScopeLock Lock(&OuterMutex);

	// DatabaseDependencies is updated only in StartNewRequestIfNeeded when there are no active requests, so it's thread safe to access it 
	if (DatabaseDependencies.Contains(Object))
	{
		Cancel(OuterMutex);
		return true;
	}
	return false;
}

void FPoseSearchDatabaseAsyncCacheTask::Update(FCriticalSection& OuterMutex)
{
	check(IsInGameThread());

	FScopeLock Lock(&OuterMutex);

	check(GetState() != EState::Cancelled); // otherwise FPoseSearchDatabaseAsyncCacheTask should have been already removed

	if (GetState() == EState::Prestarted && Poll(OuterMutex))
	{
		// task is done: we need to update the state form Prestarted to Ended/Failed
		Wait(OuterMutex);
	}

	if (bBroadcastOnDerivedDataRebuild)
	{
		Database->NotifyDerivedDataRebuild();
		bBroadcastOnDerivedDataRebuild = false;
	}
}

// it waits for the task to be done and SetSearchIndex on the database. SetState to Ended/Failed
void FPoseSearchDatabaseAsyncCacheTask::Wait(FCriticalSection& OuterMutex)
{
	check(GetState() == EState::Prestarted);

	Owner.Wait();

	FScopeLock Lock(&OuterMutex);

	const bool bFailedIndexing = SearchIndex.IsEmpty();
	if (!bFailedIndexing)
	{
		Database->SetSearchIndex(SearchIndex); // @todo: implement FPoseSearchIndex move ctor and assignment operator and use a MoveTemp(SearchIndex) here
		SetState(EState::Ended);
		bBroadcastOnDerivedDataRebuild = true;
	}
	else
	{
		check(!bBroadcastOnDerivedDataRebuild);
		SetState(EState::Failed);
	}
	SearchIndex.Reset();
}

// true is the task is done executing
bool FPoseSearchDatabaseAsyncCacheTask::Poll(FCriticalSection& OuterMutex) const
{
	return Owner.Poll();
}

bool FPoseSearchDatabaseAsyncCacheTask::ContainsDatabase(const UPoseSearchDatabase* OtherDatabase, FCriticalSection& OuterMutex) const
{
	FScopeLock Lock(&OuterMutex);
	return Database.Get() == OtherDatabase;
}

// called once the task is done:
// if EStatus::Ok (data has been retrieved from DDC) we deserialize the payload into the local SearchIndex
// if EStatus::Error we BuildIndex and if that's successful we 'Put' it on DDC
void FPoseSearchDatabaseAsyncCacheTask::OnGetComplete(UE::DerivedData::FCacheGetResponse&& Response)
{
	using namespace UE::DerivedData;

	const FCacheKey FullIndexKey = Response.Record.GetKey();

	// The database is part of the derived data cache and up to date, skip re-building it.
	if (Response.Status == EStatus::Ok)
	{
		COOK_STAT(auto Timer = UsageStats.TimeAsyncWait());

		// we found the cached data associated to the PendingDerivedDataKey: we'll deserialized into SearchIndex
		SearchIndex.Reset();
		FSharedBuffer RawData = Response.Record.GetValue(Id).GetData().Decompress();
		FMemoryReaderView Reader(RawData);
		Reader << SearchIndex;

		UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex From Cache"), *LexToString(FullIndexKey.Hash), *Database->GetName());

		COOK_STAT(Timer.AddHit(RawData.GetSize()));
	}
	else if (Response.Status == EStatus::Canceled)
	{
		SearchIndex.Reset();
		UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
	}
	else if (Response.Status == EStatus::Error)
	{
		// we didn't find the cached data associated to the PendingDerivedDataKey: we'll BuildIndex to update SearchIndex and "Put" the data over the DDC
		Owner.LaunchTask(TEXT("PoseSearchDatabaseBuild"), [this, FullIndexKey]
			{
				COOK_STAT(auto Timer = UsageStats.TimeSyncWork());

				// collecting all the databases that need to be built to gather their FPoseSearchIndexBase
				TArray<TWeakObjectPtr<const UPoseSearchDatabase>> IndexBaseDatabases;
				IndexBaseDatabases.Add(Database); // the first one is always this Database
				if (Database->NormalizationSet)
				{
					for (auto OtherDatabase : Database->NormalizationSet->Databases)
					{
						if (OtherDatabase)
						{
							IndexBaseDatabases.AddUnique(OtherDatabase);
						}
					}
				}

				// @todo: DDC or parallelize this code
				TArray<FPoseSearchIndexBase> SearchIndexBases;
				TArray<const UPoseSearchSchema*> Schemas;
				SearchIndexBases.AddDefaulted(IndexBaseDatabases.Num());
				Schemas.AddDefaulted(IndexBaseDatabases.Num());
				for (int32 IndexBaseIdx = 0; IndexBaseIdx < IndexBaseDatabases.Num(); ++IndexBaseIdx)
				{
					auto IndexBaseDatabase = IndexBaseDatabases[IndexBaseIdx];
					FPoseSearchIndexBase& SearchIndexBase = SearchIndexBases[IndexBaseIdx];
					Schemas[IndexBaseIdx] = IndexBaseDatabase->Schema;

					// early out for invalid indexing conditions
					if (!IndexBaseDatabase->Schema || !IndexBaseDatabase->Schema->IsValid() || IndexBaseDatabase->Schema->SchemaCardinality <= 0)
					{
						if (IndexBaseDatabase == Database)
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						}
						else
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed because of dependent database fail '%s'"), *LexToString(FullIndexKey.Hash), *Database->GetName(), *IndexBaseDatabase->GetName());
						}
						SearchIndex.Reset();
						return;
					}

					if (Owner.IsCanceled())
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						SearchIndex.Reset();
						return;
					}

					// Building all the related FPoseSearchBaseIndex first
					InitSearchIndexAssets(SearchIndexBase, Database.Get());

					if (Owner.IsCanceled())
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						SearchIndex.Reset();
						return;
					}

					FDatabaseIndexingContext DbIndexingContext;
					DbIndexingContext.SearchIndexBase = &SearchIndexBase;
					DbIndexingContext.Prepare(IndexBaseDatabase.Get());

					if (Owner.IsCanceled())
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						SearchIndex.Reset();
						return;
					}

					const bool bSuccess = DbIndexingContext.IndexAssets();
					if (!bSuccess)
					{
						if (IndexBaseDatabase == Database)
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						}
						else
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Failed because of dependent database fail '%s'"), *LexToString(FullIndexKey.Hash), *Database->GetName(), *IndexBaseDatabase->GetName());
						}
						SearchIndex.Reset();
						return;
					}

					if (Owner.IsCanceled())
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						SearchIndex.Reset();
						return;
					}

					DbIndexingContext.JoinIndex();
					if (Owner.IsCanceled())
					{
						UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						SearchIndex.Reset();
						return;
					}
				}

				static_cast<FPoseSearchIndexBase&>(SearchIndex) = SearchIndexBases[0];
				
				TArray<float> Deviation = ComputeChannelsDeviations(SearchIndexBases, Schemas);

				#if WITH_EDITORONLY_DATA
				SearchIndex.Deviation = Deviation;
				#endif // WITH_EDITORONLY_DATA

				// Building FPoseSearchIndex
				PreprocessSearchIndexWeights(SearchIndex, Database->Schema, Deviation);
				if (Owner.IsCanceled())
				{
					UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
					SearchIndex.Reset();
					return;
				}

				PreprocessSearchIndexPCAData(SearchIndex, Database->Schema->SchemaCardinality, Database->GetNumberOfPrincipalComponents(), Database->PoseSearchMode);
				if (Owner.IsCanceled())
				{
					UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
					SearchIndex.Reset();
					return;
				}

				PreprocessSearchIndexKDTree(SearchIndex, Database->Schema->SchemaCardinality, Database->GetNumberOfPrincipalComponents(), Database->PoseSearchMode, Database->KDTreeMaxLeafSize, Database->KDTreeQueryNumNeighbors);
				if (Owner.IsCanceled())
				{
					UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Cancelled"), *LexToString(FullIndexKey.Hash), *Database->GetName());
					SearchIndex.Reset();
					return;
				}

				UE_LOG(LogPoseSearch, Log, TEXT("%s - %s BuildIndex Succeeded"), *LexToString(FullIndexKey.Hash), *Database->GetName());

				// putting SearchIndex to DDC
				TArray<uint8> RawBytes;
				FMemoryWriter Writer(RawBytes);
				Writer << SearchIndex;
				FSharedBuffer RawData = MakeSharedBufferFromArray(MoveTemp(RawBytes));
				const int32 BytesProcessed = RawData.GetSize();

				FCacheRecordBuilder Builder(FullIndexKey);
				Builder.AddValue(Id, RawData);
				GetCache().Put({ { { Database->GetPathName() }, Builder.Build() } }, Owner, [this, FullIndexKey](FCachePutResponse&& Response)
					{
						if (Response.Status == EStatus::Error)
						{
							UE_LOG(LogPoseSearch, Log, TEXT("%s - %s Failed to store DDC"), *LexToString(FullIndexKey.Hash), *Database->GetName());
						}
					});

				COOK_STAT(Timer.AddMiss(BytesProcessed));
			});
	}
}

//////////////////////////////////////////////////////////////////////////
// FAsyncPoseSearchDatabasesManagement
FCriticalSection FAsyncPoseSearchDatabasesManagement::Mutex;

FAsyncPoseSearchDatabasesManagement& FAsyncPoseSearchDatabasesManagement::Get()
{
	FScopeLock Lock(&Mutex);

	static FAsyncPoseSearchDatabasesManagement SingletonInstance;
	return SingletonInstance;
}

FAsyncPoseSearchDatabasesManagement::FAsyncPoseSearchDatabasesManagement()
	: Tasks(*(new FPoseSearchDatabaseAsyncCacheTasks()))
{
	FScopeLock Lock(&Mutex);

	OnObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::OnObjectModified);
	FCoreDelegates::OnPreExit.AddRaw(this, &FAsyncPoseSearchDatabasesManagement::Shutdown);
}

FAsyncPoseSearchDatabasesManagement::~FAsyncPoseSearchDatabasesManagement()
{
	FScopeLock Lock(&Mutex);

	FCoreDelegates::OnPreExit.RemoveAll(this);
	Shutdown();

	delete &Tasks;
}

// we're listening to OnObjectModified to cancel any pending Task indexing databases depending from Object to avoid multi threading issues
void FAsyncPoseSearchDatabasesManagement::OnObjectModified(UObject* Object)
{
	FScopeLock Lock(&Mutex);

	// iterating backwards because of the possible RemoveAtSwap
	for (int32 TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
	{
		if (Tasks[TaskIndex]->CancelIfDependsOn(Object, Mutex))
		{
			Tasks.RemoveAtSwap(TaskIndex, 1, false);
		}
	}
}

void FAsyncPoseSearchDatabasesManagement::Shutdown()
{
	FScopeLock Lock(&Mutex);

	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedHandle);
	OnObjectModifiedHandle.Reset();
}

void FAsyncPoseSearchDatabasesManagement::Tick(float DeltaTime)
{
	FScopeLock Lock(&Mutex);

	check(IsInGameThread());

	// iterating backwards because of the possible RemoveAtSwap 
	for (int32 TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
	{
		Tasks[TaskIndex]->Update(Mutex);
			
		// @todo: check key validity every few ticks, or perhaps delete unused for a long time Tasks
	}
}

void FAsyncPoseSearchDatabasesManagement::TickCook(float DeltaTime, bool bCookCompete)
{
	FScopeLock Lock(&Mutex);

	Tick(DeltaTime);
}

TStatId FAsyncPoseSearchDatabasesManagement::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncPoseSearchDatabasesManagement, STATGROUP_Tickables);
}

void FAsyncPoseSearchDatabasesManagement::AddReferencedObjects(FReferenceCollector& Collector)
{
}

// returns true if the index has been built and the Database updated correctly  
bool FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(const UPoseSearchDatabase* Database, ERequestAsyncBuildFlag Flag)
{
	if (!Database)
	{
		return false;
	}

	FScopeLock Lock(&Mutex);

	check(Database);
	check(EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::ContinueRequest));

	FAsyncPoseSearchDatabasesManagement& This = FAsyncPoseSearchDatabasesManagement::Get();

	FPoseSearchDatabaseAsyncCacheTask* Task = nullptr;
	for (TUniquePtr<FPoseSearchDatabaseAsyncCacheTask>& TaskPtr : This.Tasks)
	{
		if (TaskPtr->ContainsDatabase(Database, Mutex))
		{
			Task = TaskPtr.Get();

			if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::NewRequest))
			{
				if (Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
				{
					if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitPreviousRequest))
					{
						Task->Wait(Mutex);
					}
					else
					{
						Task->Cancel(Mutex);
					}
				}

				Task->StartNewRequestIfNeeded(Mutex);
			}
			else // if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::ContinueRequest))
			{
				if (Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
				{
					if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitPreviousRequest))
					{
						Task->Wait(Mutex);
					}
				}
			}
			break;
		}
	}
		
	if (!Task)
	{
		// we didn't find the Task, so we Emplace a new one
		This.Tasks.Emplace(MakeUnique<FPoseSearchDatabaseAsyncCacheTask>(const_cast<UPoseSearchDatabase*>(Database), Mutex));
		Task = This.Tasks.Last().Get();
	}

	if (EnumHasAnyFlags(Flag, ERequestAsyncBuildFlag::WaitForCompletion) && Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Prestarted)
	{
		Task->Wait(Mutex);
	}

	return Task->GetState() == FPoseSearchDatabaseAsyncCacheTask::EState::Ended;
}

} // namespace UE::PoseSearch
#endif // WITH_EDITOR
