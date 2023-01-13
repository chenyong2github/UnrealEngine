// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearch.h"
#include "AssetRegistry/AssetData.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearchEigenHelper.h"

#include "Algo/BinarySearch.h"
#include "Async/ParallelFor.h"
#include "Templates/IdentityFunctor.h"
#include "Templates/Invoke.h"
#include "Templates/Less.h"
#include "Features/IModularFeatures.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/MirrorDataTable.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Trace/PoseSearchTraceLogger.h"
#include "UObject/ObjectSaveContext.h"
#include "Misc/MemStack.h"

#include "Animation/BuiltInAttributeTypes.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace1D.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearch)
#if WITH_EDITOR
#include "DerivedDataRequestOwner.h"
#endif

IMPLEMENT_ANIMGRAPH_MESSAGE(UE::PoseSearch::IPoseHistoryProvider);

#define LOCTEXT_NAMESPACE "PoseSearch"

DEFINE_LOG_CATEGORY(LogPoseSearch);

DECLARE_STATS_GROUP(TEXT("PoseSearch"), STATGROUP_PoseSearch, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Search Brute Force"), STAT_PoseSearchBruteForce, STATGROUP_PoseSearch, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Search PCA/KNN"), STAT_PoseSearchPCAKNN, STATGROUP_PoseSearch, );
DEFINE_STAT(STAT_PoseSearchBruteForce);
DEFINE_STAT(STAT_PoseSearchPCAKNN);

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// Constants and utilities

static inline float ArraySum(TConstArrayView<float> View, int32 StartIndex, int32 Offset)
{
	float Sum = 0.f;
	const int32 EndIndex = StartIndex + Offset;
	for (int i = StartIndex; i < EndIndex; ++i)
	{
		Sum += View[i];
	}
	return Sum;
}

static inline float CompareFeatureVectors(TConstArrayView<float> A, TConstArrayView<float> B, TConstArrayView<float> WeightsSqrt)
{
	check(A.Num() == B.Num() && A.Num() == WeightsSqrt.Num());

	Eigen::Map<const Eigen::ArrayXf> VA(A.GetData(), A.Num());
	Eigen::Map<const Eigen::ArrayXf> VB(B.GetData(), B.Num());
	Eigen::Map<const Eigen::ArrayXf> VW(WeightsSqrt.GetData(), WeightsSqrt.Num());

	return ((VA - VB) * VW).square().sum();
}

static inline float CompareFeatureVectors(TConstArrayView<float> A, TConstArrayView<float> B)
{
	check(A.Num() == B.Num());

	Eigen::Map<const Eigen::ArrayXf> VA(A.GetData(), A.Num());
	Eigen::Map<const Eigen::ArrayXf> VB(B.GetData(), B.Num());

	return (VA - VB).square().sum();
}

void CompareFeatureVectors(TConstArrayView<float> A, TConstArrayView<float> B, TConstArrayView<float> WeightsSqrt, TArrayView<float> Result)
{
	check(A.Num() == B.Num() && A.Num() == WeightsSqrt.Num() && A.Num() == Result.Num());

	Eigen::Map<const Eigen::ArrayXf> VA(A.GetData(), A.Num());
	Eigen::Map<const Eigen::ArrayXf> VB(B.GetData(), B.Num());
	Eigen::Map<const Eigen::ArrayXf> VW(WeightsSqrt.GetData(), WeightsSqrt.Num());
	Eigen::Map<Eigen::ArrayXf> VR(Result.GetData(), Result.Num());

	VR = ((VA - VB) * VW).square();
}

/**
* Algo::LowerBound adapted to TIndexedContainerIterator for use with indexable but not necessarily contiguous containers. Used here with TRingBuffer.
*
* Performs binary search, resulting in position of the first element >= Value using predicate
*
* @param First TIndexedContainerIterator beginning of range to search through, must be already sorted by SortPredicate
* @param Last TIndexedContainerIterator end of range
* @param Value Value to look for
* @param SortPredicate Predicate for sort comparison, defaults to <
*
* @returns Position of the first element >= Value, may be position after last element in range
*/
template <typename IteratorType, typename ValueType, typename ProjectionType, typename SortPredicateType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate) -> decltype(First.GetIndex())
{
	using SizeType = decltype(First.GetIndex());

	check(First.GetIndex() <= Last.GetIndex());

	// Current start of sequence to check
	SizeType Start = First.GetIndex();

	// Size of sequence to check
	SizeType Size = Last.GetIndex() - Start;

	// With this method, if Size is even it will do one more comparison than necessary, but because Size can be predicted by the CPU it is faster in practice
	while (Size > 0)
	{
		const SizeType LeftoverSize = Size % 2;
		Size = Size / 2;

		const SizeType CheckIndex = Start + Size;
		const SizeType StartIfLess = CheckIndex + LeftoverSize;

		auto&& CheckValue = Invoke(Projection, *(First + CheckIndex));
		Start = SortPredicate(CheckValue, Value) ? StartIfLess : Start;
	}
	return Start;
}

template <typename IteratorType, typename ValueType, typename SortPredicateType = TLess<>()>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value, SortPredicateType SortPredicate) -> decltype(First.GetIndex())
{
	return LowerBound(First, Last, Value, FIdentityFunctor(), SortPredicate);
}

typedef TArray<size_t, TInlineAllocator<128>> FNonSelectableIdx;
static void PopulateNonSelectableIdx(FNonSelectableIdx& NonSelectableIdx, FSearchContext& SearchContext, const UPoseSearchDatabase* Database, TConstArrayView<float> QueryValues)
{
	check(Database);
#if UE_POSE_SEARCH_TRACE_ENABLED
	const FPoseSearchIndex& SearchIndex = Database->GetSearchIndex();
#endif

	const FPoseSearchIndexAsset* CurrentIndexAsset = SearchContext.CurrentResult.GetSearchIndexAsset();
	if (CurrentIndexAsset && SearchContext.IsCurrentResultFromDatabase(Database) && SearchContext.PoseJumpThresholdTime > 0.f)
	{
		const int32 PoseJumpIndexThreshold = FMath::FloorToInt(SearchContext.PoseJumpThresholdTime / Database->Schema->GetSamplingInterval());
		const bool IsLooping = Database->IsSourceAssetLooping(*CurrentIndexAsset);

		for (int32 i = -PoseJumpIndexThreshold; i <= -1; ++i)
		{
			int32 PoseIdx = SearchContext.CurrentResult.PoseIdx + i;
			bool bIsPoseInRange = false;
			if (IsLooping)
			{
				bIsPoseInRange = true;

				while (PoseIdx < CurrentIndexAsset->FirstPoseIdx)
				{
					PoseIdx += CurrentIndexAsset->NumPoses;
				}
			}
			else if (CurrentIndexAsset->IsPoseInRange(PoseIdx))
			{
				bIsPoseInRange = true;
			}

			if (bIsPoseInRange)
			{
				NonSelectableIdx.Add(PoseIdx);

#if UE_POSE_SEARCH_TRACE_ENABLED
				const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Database->Schema->MirrorMismatchCostBias, QueryValues);
				SearchContext.BestCandidates.Add(PoseCost, PoseIdx, Database, EPoseCandidateFlags::DiscardedBy_PoseJumpThresholdTime);
#endif
			}
			else
			{
				break;
			}
		}

		for (int32 i = 0; i <= PoseJumpIndexThreshold; ++i)
		{
			int32 PoseIdx = SearchContext.CurrentResult.PoseIdx + i;
			bool bIsPoseInRange = false;
			if (IsLooping)
			{
				bIsPoseInRange = true;

				while (PoseIdx >= CurrentIndexAsset->FirstPoseIdx + CurrentIndexAsset->NumPoses)
				{
					PoseIdx -= CurrentIndexAsset->NumPoses;
				}
			}
			else if (CurrentIndexAsset->IsPoseInRange(PoseIdx))
			{
				bIsPoseInRange = true;
			}

			if (bIsPoseInRange)
			{
				NonSelectableIdx.Add(PoseIdx);

#if UE_POSE_SEARCH_TRACE_ENABLED
				const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Database->Schema->MirrorMismatchCostBias, QueryValues);
				SearchContext.BestCandidates.Add(PoseCost, PoseIdx, Database, EPoseCandidateFlags::DiscardedBy_PoseJumpThresholdTime);
#endif
			}
			else
			{
				break;
			}
		}
	}

	if (SearchContext.PoseIndicesHistory)
	{
		const FObjectKey DatabaseKey(Database);
		for (auto It = SearchContext.PoseIndicesHistory->IndexToTime.CreateConstIterator(); It; ++It)
		{
			const FHistoricalPoseIndex& HistoricalPoseIndex = It.Key();
			if (HistoricalPoseIndex.DatabaseKey == DatabaseKey)
			{
				NonSelectableIdx.Add(HistoricalPoseIndex.PoseIndex);

#if UE_POSE_SEARCH_TRACE_ENABLED
				check(HistoricalPoseIndex.PoseIndex >= 0);
					
				// if we're editing the database and removing assets it's possible that the PoseIndicesHistory contains invalid pose indexes
				if (HistoricalPoseIndex.PoseIndex < SearchIndex.NumPoses)
				{
					const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(HistoricalPoseIndex.PoseIndex, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Database->Schema->MirrorMismatchCostBias, QueryValues);
					SearchContext.BestCandidates.Add(PoseCost, HistoricalPoseIndex.PoseIndex, Database, EPoseCandidateFlags::DiscardedBy_PoseReselectHistory);
				}
#endif
			}
		}
	}

	NonSelectableIdx.Sort();
}

struct FPoseFilters
{
	FPoseFilters(const UPoseSearchSchema* Schema, TConstArrayView<size_t> NonSelectableIdx, EPoseSearchPoseFlags OverallFlags)
	{
		NonSelectableIdxPoseFilter.NonSelectableIdx = NonSelectableIdx;

		if (EnumHasAnyFlags(OverallFlags, EPoseSearchPoseFlags::BlockTransition))
		{
			AllPoseFilters.Add(&BlockTransitionPoseFilter);
		}

		if (NonSelectableIdxPoseFilter.IsPoseFilterActive())
		{
			AllPoseFilters.Add(&NonSelectableIdxPoseFilter);
		}

		for (const IPoseFilter* ChannelPoseFilter : Schema->Channels)
		{
			if (ChannelPoseFilter->IsPoseFilterActive())
			{
				AllPoseFilters.Add(ChannelPoseFilter);
			}
		}
	}

	bool AreFiltersValid(const FPoseSearchIndex& SearchIndex, TConstArrayView<float> QueryValues, int32 PoseIdx, const FPoseSearchPoseMetadata& Metadata
#if UE_POSE_SEARCH_TRACE_ENABLED
		, UE::PoseSearch::FSearchContext& SearchContext, const UPoseSearchDatabase* Database
#endif
	) const
	{
		TConstArrayView<float> PoseValues = SearchIndex.GetPoseValues(PoseIdx);
		for (const IPoseFilter* PoseFilter : AllPoseFilters)
		{
			if (!PoseFilter->IsPoseValid(PoseValues, QueryValues, PoseIdx, Metadata))
			{
#if UE_POSE_SEARCH_TRACE_ENABLED
				if (PoseFilter == &NonSelectableIdxPoseFilter)
				{
					// candidate already added to SearchContext.BestCandidates by PopulateNonSelectableIdx
				}
				else if (PoseFilter == &BlockTransitionPoseFilter)
				{
					const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Database->Schema->MirrorMismatchCostBias, QueryValues);
					SearchContext.BestCandidates.Add(PoseCost, PoseIdx, Database, EPoseCandidateFlags::DiscardedBy_BlockTransition);
				}
				else
				{
					const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Database->Schema->MirrorMismatchCostBias, QueryValues);
					SearchContext.BestCandidates.Add(PoseCost, PoseIdx, Database, EPoseCandidateFlags::DiscardedBy_PoseFilter);
				}
#endif
				return false;
			}
		}
		return true;
	};

private:
	struct FNonSelectableIdxPoseFilter : public IPoseFilter
	{
		virtual bool IsPoseFilterActive() const override
		{
			return !NonSelectableIdx.IsEmpty();
		}

		virtual bool IsPoseValid(TConstArrayView<float> PoseValues, TConstArrayView<float> QueryValues, int32 PoseIdx, const FPoseSearchPoseMetadata& Metadata) const override
		{
			return Algo::BinarySearch(NonSelectableIdx, PoseIdx) == INDEX_NONE;
		}

		TConstArrayView<size_t> NonSelectableIdx;
	};

	struct FBlockTransitionPoseFilter : public IPoseFilter
	{
		virtual bool IsPoseFilterActive() const override
		{
			return true;
		}

		virtual bool IsPoseValid(TConstArrayView<float> PoseValues, TConstArrayView<float> QueryValues, int32 PoseIdx, const FPoseSearchPoseMetadata& Metadata) const override
		{
			return !EnumHasAnyFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition);
		}
	};

	FNonSelectableIdxPoseFilter NonSelectableIdxPoseFilter;
	FBlockTransitionPoseFilter BlockTransitionPoseFilter;

	TArray<const IPoseFilter*, TInlineAllocator<64>> AllPoseFilters;
};

} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel
#if WITH_EDITOR
void UPoseSearchFeatureChannel::PopulateChannelLayoutSet(UE::PoseSearch::FFeatureChannelLayoutSet& FeatureChannelLayoutSet) const
{
	FeatureChannelLayoutSet.Add(GetName(), UE::PoseSearch::FKeyBuilder(this).Finalize(), ChannelDataOffset, ChannelCardinality);
}

void UPoseSearchFeatureChannel::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData, const UPoseSearchSchema* Schema) const
{
	CostBreakDownData.AddEntireBreakDownSection(FText::FromString(GetName()), Schema, ChannelDataOffset, ChannelCardinality);
}
#endif // WITH_EDITOR

//////////////////////////////////////////////////////////////////////////
// UPoseSearchSchema
void UPoseSearchSchema::Finalize(bool bRemoveEmptyChannels)
{
	using namespace UE::PoseSearch;

	if (bRemoveEmptyChannels)
	{
		Channels.RemoveAll([](TObjectPtr<UPoseSearchFeatureChannel>& Channel) { return !Channel; });
	}

	BoneReferences.Reset();

	int32 CurrentChannelDataOffset = 0;

	SchemaCardinality = 0;
	for (int32 ChannelIdx = 0; ChannelIdx != Channels.Num(); ++ChannelIdx)
	{
		if (UPoseSearchFeatureChannel* Channel = Channels[ChannelIdx].Get())
		{
			Channel->InitializeSchema(this);
		}
	}

	ResolveBoneReferences();
}

void UPoseSearchSchema::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Finalize();

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchSchema::PostLoad()
{
	Super::PostLoad();
	ResolveBoneReferences();
}

#if WITH_EDITOR
void UPoseSearchSchema::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Finalize(false);
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPoseSearchSchema::ComputeCostBreakdowns(UE::PoseSearch::ICostBreakDownData& CostBreakDownData) const
{
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Channels)
	{
		if (Channel)
		{
			Channel->ComputeCostBreakdowns(CostBreakDownData, this);
		}
	}
}
#endif

bool UPoseSearchSchema::IsValid() const
{
	bool bValid = Skeleton != nullptr;

	for (const FBoneReference& BoneRef : BoneReferences)
	{
		bValid &= BoneRef.HasValidSetup();
	}

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel: Channels)
	{
		bValid &= Channel != nullptr;
	}

	bValid &= (BoneReferences.Num() == BoneIndices.Num());

	return bValid;
}

void UPoseSearchSchema::ResolveBoneReferences()
{
	// Initialize references to obtain bone indices
	for (FBoneReference& BoneRef : BoneReferences)
	{
		BoneRef.Initialize(Skeleton);
	}

	// Fill out bone index array
	BoneIndices.SetNum(BoneReferences.Num());
	for (int32 BoneRefIdx = 0; BoneRefIdx != BoneReferences.Num(); ++BoneRefIdx)
	{
		BoneIndices[BoneRefIdx] = BoneReferences[BoneRefIdx].BoneIndex;
	}

	// Build separate index array with parent indices guaranteed to be present. Sort for EnsureParentsPresent.
	BoneIndicesWithParents = BoneIndices;
	BoneIndicesWithParents.Sort();

	if (Skeleton)
	{
		FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, Skeleton->GetReferenceSkeleton());
	}

	// BoneIndicesWithParents should at least contain the root to support mirroring root motion
	if (BoneIndicesWithParents.Num() == 0)
	{
		BoneIndicesWithParents.Add(0);
	}
}

void UPoseSearchSchema::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& InOutQuery) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_BuildQuery);

	InOutQuery.Init(this);

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Channels)
	{
		Channel->BuildQuery(SearchContext, InOutQuery);
	}
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchBaseIndex
const FPoseSearchIndexAsset& FPoseSearchIndexBase::GetAssetForPose(int32 PoseIdx) const
{
	const int32 AssetIndex = PoseMetadata[PoseIdx].AssetIndex;
	return Assets[AssetIndex];
}

const FPoseSearchIndexAsset* FPoseSearchIndexBase::GetAssetForPoseSafe(int32 PoseIdx) const
{
	if (PoseMetadata.IsValidIndex(PoseIdx))
	{
		const int32 AssetIndex = PoseMetadata[PoseIdx].AssetIndex;
		if (Assets.IsValidIndex(AssetIndex))
		{
			return &Assets[AssetIndex];
		}
	}
	return nullptr;
}

float FPoseSearchIndexBase::GetAssetTime(int32 PoseIdx, float SamplingInterval) const
{
	const FPoseSearchIndexAsset& Asset = GetAssetForPose(PoseIdx);

	if (Asset.Type == ESearchIndexAssetType::Sequence || Asset.Type == ESearchIndexAssetType::AnimComposite)
	{
		const FFloatInterval SamplingRange = Asset.SamplingInterval;

		float AssetTime = FMath::Min(SamplingRange.Min + SamplingInterval * (PoseIdx - Asset.FirstPoseIdx), SamplingRange.Max);
		return AssetTime;
	}

	if (Asset.Type == ESearchIndexAssetType::BlendSpace)
	{
		const FFloatInterval SamplingRange = Asset.SamplingInterval;

		// For BlendSpaces the AssetTime is in the range [0, 1] while the Sampling Range
		// is in real time (seconds)
		float AssetTime = FMath::Min(SamplingRange.Min + SamplingInterval * (PoseIdx - Asset.FirstPoseIdx), SamplingRange.Max) / (Asset.NumPoses * SamplingInterval);
		return AssetTime;
	}
	
	checkNoEntry();
	return -1.0f;
}

bool FPoseSearchIndexBase::IsEmpty() const
{
	const bool bEmpty = Assets.Num() == 0 || NumPoses == 0;
	return bEmpty;
}

void FPoseSearchIndexBase::Reset()
{
	FPoseSearchIndexBase Default;
	*this = Default;
}

FArchive& operator<<(FArchive& Ar, FPoseSearchIndexBase& Index)
{
	int32 NumValues = 0;
	int32 NumAssets = 0;

	if (Ar.IsSaving())
	{
		NumValues = Index.Values.Num();
		NumAssets = Index.Assets.Num();
	}

	Ar << Index.NumPoses;
	Ar << NumValues;
	Ar << NumAssets;
	Ar << Index.OverallFlags;

	if (Ar.IsLoading())
	{
		Index.Values.SetNumUninitialized(NumValues);
		Index.PoseMetadata.SetNumUninitialized(Index.NumPoses);
		Index.Assets.SetNumUninitialized(NumAssets);
	}

	if (Index.Values.Num() > 0)
	{
		Ar.Serialize(&Index.Values[0], Index.Values.Num() * Index.Values.GetTypeSize());
	}

	if (Index.PoseMetadata.Num() > 0)
	{
		Ar.Serialize(&Index.PoseMetadata[0], Index.PoseMetadata.Num() * Index.PoseMetadata.GetTypeSize());
	}

	if (Index.Assets.Num() > 0)
	{
		Ar.Serialize(&Index.Assets[0], Index.Assets.Num() * Index.Assets.GetTypeSize());
	}

	Ar << Index.MinCostAddend;

	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndex
FPoseSearchIndex::FPoseSearchIndex(const FPoseSearchIndex& Other)
	: FPoseSearchIndexBase(Other)
	, PCAValues(Other.PCAValues)
	, PCAProjectionMatrix(Other.PCAProjectionMatrix)
	, Mean(Other.Mean)
	, WeightsSqrt(Other.WeightsSqrt)
	, KDTree(Other.KDTree)
#if WITH_EDITORONLY_DATA
	, PCAExplainedVariance(Other.PCAExplainedVariance)
	, Deviation(Other.Deviation)
#endif // WITH_EDITORONLY_DATA
{
	check(!PCAValues.IsEmpty() || KDTree.DataSource.PointCount == 0);
	KDTree.DataSource.Data = PCAValues.IsEmpty() ? nullptr : PCAValues.GetData();
}

FPoseSearchIndex& FPoseSearchIndex::operator=(const FPoseSearchIndex& Other)
{
	if (this != &Other)
	{
		this->~FPoseSearchIndex();
		new(this)FPoseSearchIndex(Other);
	}
	return *this;
}

void FPoseSearchIndex::Reset()
{
	FPoseSearchIndex Default;
	*this = Default;
}

TConstArrayView<float> FPoseSearchIndex::GetPoseValues(int32 PoseIdx) const
{
	const int32 SchemaCardinality = WeightsSqrt.Num();
	check(PoseIdx >= 0 && PoseIdx < NumPoses&& SchemaCardinality > 0);
	const int32 ValueOffset = PoseIdx * SchemaCardinality;
	return MakeArrayView(&Values[ValueOffset], SchemaCardinality);
}

TConstArrayView<float> FPoseSearchIndex::GetPoseValuesSafe(int32 PoseIdx) const
{
	if (PoseIdx >= 0 && PoseIdx < NumPoses)
	{
		const int32 SchemaCardinality = WeightsSqrt.Num();
		const int32 ValueOffset = PoseIdx * SchemaCardinality;
		return MakeArrayView(&Values[ValueOffset], SchemaCardinality);
	}
	return TConstArrayView<float>();
}

FPoseSearchCost FPoseSearchIndex::ComparePoses(int32 PoseIdx, EPoseSearchBooleanRequest QueryMirrorRequest, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags, float MirrorMismatchCostBias, TConstArrayView<float> QueryValues) const
{
	// base dissimilarity cost representing how the associated PoseIdx differ, in a weighted way, from the query pose (QueryValues)
	const float DissimilarityCost = UE::PoseSearch::CompareFeatureVectors(GetPoseValues(PoseIdx), QueryValues, WeightsSqrt);

	// cost addend associated to a mismatch in mirror state between query and analyzed PoseIdx
	float MirrorMismatchAddend = 0.f;
	if (QueryMirrorRequest != EPoseSearchBooleanRequest::Indifferent)
	{
		const FPoseSearchIndexAsset& IndexAsset = GetAssetForPose(PoseIdx);
		const bool bMirroringMismatch =
			(IndexAsset.bMirrored && QueryMirrorRequest == EPoseSearchBooleanRequest::FalseValue) ||
			(!IndexAsset.bMirrored && QueryMirrorRequest == EPoseSearchBooleanRequest::TrueValue);
		if (bMirroringMismatch)
		{
			MirrorMismatchAddend = MirrorMismatchCostBias;
		}
	}

	const FPoseSearchPoseMetadata& PoseIdxMetadata = PoseMetadata[PoseIdx];

	// cost addend associated to Schema->BaseCostBias or overriden by UAnimNotifyState_PoseSearchModifyCost
	const float NotifyAddend = PoseIdxMetadata.CostAddend;

	// cost addend associated to Schema->ContinuingPoseCostBias or overriden by UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias
	const float ContinuingPoseCostAddend = EnumHasAnyFlags(PoseComparisonFlags, UE::PoseSearch::EPoseComparisonFlags::ContinuingPose) ? PoseIdxMetadata.ContinuingPoseCostAddend : 0.f;

	return FPoseSearchCost(DissimilarityCost, NotifyAddend, MirrorMismatchAddend, ContinuingPoseCostAddend);
}

FArchive& operator<<(FArchive& Ar, FPoseSearchIndex& Index)
{
	Ar << static_cast<FPoseSearchIndexBase&>(Index);

	int32 NumPCAValues = 0;

	if (Ar.IsSaving())
	{
		NumPCAValues = Index.PCAValues.Num();
	}

	Ar << NumPCAValues;

	if (Ar.IsLoading())
	{
		Index.PCAValues.SetNumUninitialized(NumPCAValues);
	}

	if (Index.PCAValues.Num() > 0)
	{
		Ar.Serialize(&Index.PCAValues[0], Index.PCAValues.Num() * Index.PCAValues.GetTypeSize());
	}

	Ar << Index.WeightsSqrt;
	Ar << Index.Mean;
	Ar << Index.PCAProjectionMatrix;

	Serialize(Ar, Index.KDTree, Index.PCAValues.GetData());

#if WITH_EDITORONLY_DATA
	Ar << Index.PCAExplainedVariance;
	Ar << Index.Deviation;
#endif // WITH_EDITORONLY_DATA

	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// UPoseSearchDatabase

UPoseSearchDatabase::~UPoseSearchDatabase()
{
}

void UPoseSearchDatabase::SetSearchIndex(const FPoseSearchIndex& SearchIndex)
{
	check(IsInGameThread());
	SearchIndexPrivate = SearchIndex;
}

const FPoseSearchIndex& UPoseSearchDatabase::GetSearchIndex() const
{
	// making sure the search index is consistent. if it fails the calling code hasn't been protected by FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex
	check(Schema && Schema->IsValid() && !SearchIndexPrivate.IsEmpty() && SearchIndexPrivate.WeightsSqrt.Num() == Schema->SchemaCardinality && SearchIndexPrivate.KDTree.Impl);
	return SearchIndexPrivate;
}

int32 UPoseSearchDatabase::GetPoseIndexFromTime(float Time, const FPoseSearchIndexAsset& SearchIndexAsset) const
{
	const bool bIsLooping = IsSourceAssetLooping(SearchIndexAsset);
	const FFloatInterval& Range = SearchIndexAsset.SamplingInterval;
	const bool bHasPoseIndex = SearchIndexAsset.FirstPoseIdx != INDEX_NONE && SearchIndexAsset.NumPoses > 0 && (bIsLooping || Range.Contains(Time));
	if (bHasPoseIndex)
	{
		int32 PoseOffset = FMath::RoundToInt(Schema->SampleRate * (Time - Range.Min));
		
		if (PoseOffset < 0)
		{
			if (bIsLooping)
			{
				PoseOffset = (PoseOffset % SearchIndexAsset.NumPoses) + SearchIndexAsset.NumPoses;
			}
			else
			{
				PoseOffset = 0;
			}
		}
		else if (PoseOffset >= SearchIndexAsset.NumPoses)
		{
			if (bIsLooping)
			{
				PoseOffset = PoseOffset % SearchIndexAsset.NumPoses;
			}
			else
			{
				PoseOffset = SearchIndexAsset.NumPoses - 1;
			}
		}

		int32 PoseIdx = SearchIndexAsset.FirstPoseIdx + PoseOffset;
		return PoseIdx;
	}

	return INDEX_NONE;
}

bool UPoseSearchDatabase::GetPoseIndicesAndLerpValueFromTime(float Time, const FPoseSearchIndexAsset& SearchIndexAsset, int32& PrevPoseIdx, int32& PoseIdx, int32& NextPoseIdx, float& LerpValue) const
{
	PoseIdx = GetPoseIndexFromTime(Time, SearchIndexAsset);
	if (PoseIdx == INDEX_NONE)
	{
		PrevPoseIdx = INDEX_NONE;
		NextPoseIdx = INDEX_NONE;
		LerpValue = 0.f;
		return false;
	}

	const FFloatInterval& Range = SearchIndexAsset.SamplingInterval;
	const float FloatPoseOffset = Schema->SampleRate * (Time - Range.Min);
	const int32 PoseOffset = FMath::RoundToInt(FloatPoseOffset);
	LerpValue = FloatPoseOffset - float(PoseOffset);

	const float PrevTime = Time - 1.f / Schema->SampleRate;
	const float NextTime = Time + 1.f / Schema->SampleRate;

	PrevPoseIdx = GetPoseIndexFromTime(PrevTime, SearchIndexAsset);
	if (PrevPoseIdx == INDEX_NONE)
	{
		PrevPoseIdx = PoseIdx;
	}

	NextPoseIdx = GetPoseIndexFromTime(NextTime, SearchIndexAsset);
	if (NextPoseIdx == INDEX_NONE)
	{
		NextPoseIdx = PoseIdx;
	}

	check(LerpValue >= -0.5f && LerpValue <= 0.5f);

	return true;
}

const FInstancedStruct& UPoseSearchDatabase::GetAnimationAssetStruct(int32 AnimationAssetIndex) const
{
	check(AnimationAssets.IsValidIndex(AnimationAssetIndex));
	return AnimationAssets[AnimationAssetIndex];
}

const FInstancedStruct& UPoseSearchDatabase::GetAnimationAssetStruct(const FPoseSearchIndexAsset& SearchIndexAsset) const
{
	return GetAnimationAssetStruct(SearchIndexAsset.SourceAssetIdx);
}

const FPoseSearchDatabaseAnimationAssetBase* UPoseSearchDatabase::GetAnimationAssetBase(int32 AnimationAssetIndex) const
{
	if (AnimationAssets.IsValidIndex(AnimationAssetIndex))
	{
		return AnimationAssets[AnimationAssetIndex].GetMutablePtr<FPoseSearchDatabaseAnimationAssetBase>();
	}

	return nullptr;
}

const FPoseSearchDatabaseAnimationAssetBase* UPoseSearchDatabase::GetAnimationAssetBase(const FPoseSearchIndexAsset& SearchIndexAsset) const
{
	return GetAnimationAssetBase(SearchIndexAsset.SourceAssetIdx);
}

FPoseSearchDatabaseAnimationAssetBase* UPoseSearchDatabase::GetMutableAnimationAssetBase(int32 AnimationAssetIndex)
{
	if (AnimationAssets.IsValidIndex(AnimationAssetIndex))
	{
		return AnimationAssets[AnimationAssetIndex].GetMutablePtr<FPoseSearchDatabaseAnimationAssetBase>();
	}

	return nullptr;
}

FPoseSearchDatabaseAnimationAssetBase* UPoseSearchDatabase::GetMutableAnimationAssetBase(const FPoseSearchIndexAsset& SearchIndexAsset)
{
	return GetMutableAnimationAssetBase(SearchIndexAsset.SourceAssetIdx);
}

const bool UPoseSearchDatabase::IsSourceAssetLooping(const FPoseSearchIndexAsset& SearchIndexAsset) const
{
	return GetAnimationAssetBase(SearchIndexAsset.SourceAssetIdx)->IsLooping();
}

const FString UPoseSearchDatabase::GetSourceAssetName(const FPoseSearchIndexAsset& SearchIndexAsset) const
{
	return GetAnimationAssetBase(SearchIndexAsset.SourceAssetIdx)->GetName();
}

int32 UPoseSearchDatabase::GetNumberOfPrincipalComponents() const
{
	return FMath::Min<int32>(NumberOfPrincipalComponents, Schema->SchemaCardinality);
}

bool UPoseSearchDatabase::GetSkipSearchIfPossible() const
{
	if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate || PoseSearchMode == EPoseSearchMode::PCAKDTree_Compare)
	{
		return false;
	}

	return bSkipSearchIfPossible;
}

bool FPoseSearchDatabaseSequence::IsRootMotionEnabled() const
{
	return Sequence ? Sequence->HasRootMotion() : false;
}

UAnimationAsset* FPoseSearchDatabaseBlendSpace::GetAnimationAsset() const
{
	return BlendSpace.Get();
}

UClass* FPoseSearchDatabaseBlendSpace::GetAnimationAssetStaticClass() const
{
	return UBlendSpace::StaticClass();
}

bool FPoseSearchDatabaseBlendSpace::IsLooping() const
{
	return BlendSpace ? BlendSpace->bLoop : false;
}

const FString FPoseSearchDatabaseBlendSpace::GetName() const
{
	return BlendSpace ? BlendSpace->GetName() : FString();
}

bool FPoseSearchDatabaseBlendSpace::IsRootMotionEnabled() const
{
	bool bIsRootMotionUsedInBlendSpace = false;

	if (BlendSpace)
	{
		BlendSpace->ForEachImmutableSample([&bIsRootMotionUsedInBlendSpace](const FBlendSample& Sample)
			{
				const TObjectPtr<UAnimSequence> Sequence = Sample.Animation;

				if (IsValid(Sequence) && Sequence->HasRootMotion())
				{
					bIsRootMotionUsedInBlendSpace = true;
				}
			});
	}

	return bIsRootMotionUsedInBlendSpace;
}

void FPoseSearchDatabaseBlendSpace::GetBlendSpaceParameterSampleRanges(int32& HorizontalBlendNum, int32& VerticalBlendNum) const
{
	check(BlendSpace);

	HorizontalBlendNum = bUseGridForSampling ? BlendSpace->GetBlendParameter(0).GridNum + 1 : FMath::Max(NumberOfHorizontalSamples, 1);
	VerticalBlendNum = BlendSpace->IsA<UBlendSpace1D>() ? 1 : bUseGridForSampling ? BlendSpace->GetBlendParameter(1).GridNum + 1 : FMath::Max(NumberOfVerticalSamples, 1);

	check(HorizontalBlendNum >= 1 && VerticalBlendNum >= 1);
}

FVector FPoseSearchDatabaseBlendSpace::BlendParameterForSampleRanges(int32 HorizontalBlendIndex, int32 VerticalBlendIndex) const
{
	check(BlendSpace);

	const bool bWrapInputOnHorizontalAxis = BlendSpace->GetBlendParameter(0).bWrapInput;
	const bool bWrapInputOnVerticalAxis = BlendSpace->GetBlendParameter(1).bWrapInput;

	int32 HorizontalBlendNum, VerticalBlendNum;
	GetBlendSpaceParameterSampleRanges(HorizontalBlendNum, VerticalBlendNum);

	if (bWrapInputOnHorizontalAxis)
	{
		++HorizontalBlendNum;
	}

	if (bWrapInputOnVerticalAxis)
	{
		++VerticalBlendNum;
	}

	const float HorizontalBlendMin = BlendSpace->GetBlendParameter(0).Min;
	const float HorizontalBlendMax = BlendSpace->GetBlendParameter(0).Max;

	const float VerticalBlendMin = BlendSpace->GetBlendParameter(1).Min;
	const float VerticalBlendMax = BlendSpace->GetBlendParameter(1).Max;

	return FVector(
		HorizontalBlendNum > 1 ? 
			HorizontalBlendMin + (HorizontalBlendMax - HorizontalBlendMin) * 
			((float)HorizontalBlendIndex) / (HorizontalBlendNum - 1) : 
		HorizontalBlendMin,
		VerticalBlendNum > 1 ? 
			VerticalBlendMin + (VerticalBlendMax - VerticalBlendMin) * 
			((float)VerticalBlendIndex) / (VerticalBlendNum - 1) : 
		VerticalBlendMin,
		0.0f);
}

bool FPoseSearchDatabaseAnimComposite::IsRootMotionEnabled() const
{
	return AnimComposite ? AnimComposite->HasRootMotion() : false;
}

void UPoseSearchDatabase::PostLoad()
{
#if WITH_EDITORONLY_DATA
	for (const FPoseSearchDatabaseSequence& DatabaseSequence : Sequences_DEPRECATED)
	{
		AnimationAssets.Add(FInstancedStruct::Make(DatabaseSequence));
	}
	Sequences_DEPRECATED.Empty();

	for (const FPoseSearchDatabaseBlendSpace& DatabaseBlendSpace : BlendSpaces_DEPRECATED)
	{
		AnimationAssets.Add(FInstancedStruct::Make(DatabaseBlendSpace));
	}
	BlendSpaces_DEPRECATED.Empty();
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	using namespace UE::PoseSearch;
	FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(this, ERequestAsyncBuildFlag::NewRequest);
#endif

	Super::PostLoad();
}

#if WITH_EDITOR
void UPoseSearchDatabase::RegisterOnDerivedDataRebuild(const FOnDerivedDataRebuild& Delegate)
{
	OnDerivedDataRebuild.Add(Delegate);
}
void UPoseSearchDatabase::UnregisterOnDerivedDataRebuild(void* Unregister)
{
	OnDerivedDataRebuild.RemoveAll(Unregister);
}

void UPoseSearchDatabase::NotifyDerivedDataRebuild() const
{
	OnDerivedDataRebuild.Broadcast();
}

void UPoseSearchDatabase::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	using namespace UE::PoseSearch;
	Super::BeginCacheForCookedPlatformData(TargetPlatform);
	FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(this, ERequestAsyncBuildFlag::NewRequest);
}

bool UPoseSearchDatabase::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	using namespace UE::PoseSearch;
	check(IsInGameThread());
	return FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(this, ERequestAsyncBuildFlag::ContinueRequest);
}
#endif // WITH_EDITOR

void UPoseSearchDatabase::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
#if WITH_EDITOR
	using namespace UE::PoseSearch;
	if (!IsTemplate() && !ObjectSaveContext.IsProceduralSave())
	{
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(this, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
	}
#endif

	Super::PostSaveRoot(ObjectSaveContext);
}

void UPoseSearchDatabase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsFilterEditorOnly())
	{
		if (Ar.IsLoading() || Ar.IsCooking())
		{
			Ar << SearchIndexPrivate;
		}
	}
}

UE::PoseSearch::FSearchResult UPoseSearchDatabase::Search(UE::PoseSearch::FSearchContext& SearchContext) const
{
	using namespace UE::PoseSearch;

	FSearchResult Result;

#if WITH_EDITOR
	if (!FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(this, ERequestAsyncBuildFlag::ContinueRequest))
	{
		return Result;
	}
#endif

	if (PoseSearchMode == EPoseSearchMode::BruteForce || PoseSearchMode == EPoseSearchMode::PCAKDTree_Compare)
	{
		Result = SearchBruteForce(SearchContext);
	}

	if (PoseSearchMode != EPoseSearchMode::BruteForce)
	{
#if WITH_EDITORONLY_DATA
		FPoseSearchCost BruteForcePoseCost = Result.BruteForcePoseCost;
#endif

		Result = SearchPCAKDTree(SearchContext);

#if WITH_EDITORONLY_DATA
		Result.BruteForcePoseCost = BruteForcePoseCost;
		if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Compare)
		{
			check(Result.BruteForcePoseCost.GetTotalCost() <= Result.PoseCost.GetTotalCost());
		}
#endif
	}
	
	return Result;
}

UE::PoseSearch::FSearchResult UPoseSearchDatabase::SearchPCAKDTree(UE::PoseSearch::FSearchContext& SearchContext) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_PCA_KNN);
	SCOPE_CYCLE_COUNTER(STAT_PoseSearchPCAKNN);

	using namespace UE::PoseSearch;

	FSearchResult Result;

	const int32 NumDimensions = Schema->SchemaCardinality;
	const FPoseSearchIndex& SearchIndex = GetSearchIndex();

	const uint32 ClampedNumberOfPrincipalComponents = GetNumberOfPrincipalComponents();
	const uint32 ClampedKDTreeQueryNumNeighbors = FMath::Clamp<uint32>(KDTreeQueryNumNeighbors, 1, SearchIndex.NumPoses);

	//stack allocated temporaries
	TArrayView<size_t> ResultIndexes((size_t*)FMemory_Alloca((ClampedKDTreeQueryNumNeighbors + 1) * sizeof(size_t)), ClampedKDTreeQueryNumNeighbors + 1);
	TArrayView<float> ResultDistanceSqr((float*)FMemory_Alloca((ClampedKDTreeQueryNumNeighbors + 1) * sizeof(float)), ClampedKDTreeQueryNumNeighbors + 1);
	RowMajorVectorMap WeightedQueryValues((float*)FMemory_Alloca(NumDimensions * sizeof(float)), 1, NumDimensions);
	RowMajorVectorMap CenteredQueryValues((float*)FMemory_Alloca(NumDimensions * sizeof(float)), 1, NumDimensions);
	RowMajorVectorMap ProjectedQueryValues((float*)FMemory_Alloca(ClampedNumberOfPrincipalComponents * sizeof(float)), 1, ClampedNumberOfPrincipalComponents);
	
	// KDTree in PCA space search
	if (PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate)
	{
		const RowMajorVectorMapConst MapWeightsSqrt(SearchIndex.WeightsSqrt.GetData(), 1, NumDimensions);

		// testing the KDTree is returning the proper searches for all the original points transformed in pca space
		for (int32 PoseIdx = 0; PoseIdx < SearchIndex.NumPoses; ++PoseIdx)
		{
			FKDTree::KNNResultSet ResultSet(ClampedKDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr);
			TConstArrayView<float> PoseValues = SearchIndex.GetPoseValues(PoseIdx);

			const RowMajorVectorMapConst Mean(SearchIndex.Mean.GetData(), 1, NumDimensions);
			const ColMajorMatrixMapConst PCAProjectionMatrix(SearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, ClampedNumberOfPrincipalComponents);

			const RowMajorVectorMapConst QueryValues(PoseValues.GetData(), 1, NumDimensions);
			WeightedQueryValues = QueryValues.array() * MapWeightsSqrt.array();
			CenteredQueryValues.noalias() = WeightedQueryValues - Mean;
			ProjectedQueryValues.noalias() = CenteredQueryValues * PCAProjectionMatrix;

			SearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedQueryValues.data());

			size_t ResultIndex = 0;
			for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
			{
				if (PoseIdx == ResultIndexes[ResultIndex])
				{
					check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
					break;
				}
			}
			check(ResultIndex < ResultSet.Num());
		}
	}

	SearchContext.GetOrBuildQuery(this, Result.ComposedQuery);

	TConstArrayView<float> QueryValues = Result.ComposedQuery.GetValues();

	const bool IsCurrentResultFromThisDatabase = SearchContext.IsCurrentResultFromDatabase(this);

	// evaluating the continuing pose only if it hasn't already being evaluated and the related animation can advance
	if (!SearchContext.bForceInterrupt && IsCurrentResultFromThisDatabase && SearchContext.bCanAdvance && !Result.ContinuingPoseCost.IsValid())
	{
		Result.PoseIdx = SearchContext.CurrentResult.PoseIdx;
		Result.PoseCost = SearchIndex.ComparePoses(Result.PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::ContinuingPose, Schema->MirrorMismatchCostBias, QueryValues);
		Result.ContinuingPoseCost = Result.PoseCost;

		if (GetSkipSearchIfPossible())
		{
			SearchContext.UpdateCurrentBestCost(Result.PoseCost);
		}
	}

	// since any PoseCost calculated here is at least SearchIndex.MinCostAddend,
	// there's no point in performing the search if CurrentBestTotalCost is already better than that
	if (SearchContext.GetCurrentBestTotalCost() > SearchIndex.MinCostAddend)
	{
		FNonSelectableIdx NonSelectableIdx;
		PopulateNonSelectableIdx(NonSelectableIdx, SearchContext, this, QueryValues);
		FKDTree::KNNResultSet ResultSet(ClampedKDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr, NonSelectableIdx);

		check(QueryValues.Num() == NumDimensions);

		const RowMajorVectorMapConst Mean(SearchIndex.Mean.GetData(), 1, NumDimensions);
		const ColMajorMatrixMapConst PCAProjectionMatrix(SearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, ClampedNumberOfPrincipalComponents);

		// transforming query values into PCA space to query the KDTree
		const RowMajorVectorMapConst QueryValuesMap(QueryValues.GetData(), 1, NumDimensions);
		const RowMajorVectorMapConst MapWeightsSqrt(SearchIndex.WeightsSqrt.GetData(), 1, NumDimensions);
		WeightedQueryValues = QueryValuesMap.array() * MapWeightsSqrt.array();
		CenteredQueryValues.noalias() = WeightedQueryValues - Mean;
		ProjectedQueryValues.noalias() = CenteredQueryValues * PCAProjectionMatrix;

		SearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedQueryValues.data());

		// NonSelectableIdx are already filtered out inside the kdtree search
		const FPoseFilters PoseFilters(Schema, TConstArrayView<size_t>(), SearchIndex.OverallFlags);
		for (size_t ResultIndex = 0; ResultIndex < ResultSet.Num(); ++ResultIndex)
		{
			const int32 PoseIdx = ResultIndexes[ResultIndex];
			if (PoseFilters.AreFiltersValid(SearchIndex, QueryValues, PoseIdx, SearchIndex.PoseMetadata[PoseIdx]
#if UE_POSE_SEARCH_TRACE_ENABLED
				, SearchContext, this
#endif
			))
			{
				const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Schema->MirrorMismatchCostBias, QueryValues);
				if (PoseCost < Result.PoseCost)
				{
					Result.PoseCost = PoseCost;
					Result.PoseIdx = PoseIdx;
				}

#if UE_POSE_SEARCH_TRACE_ENABLED
				SearchContext.BestCandidates.Add(PoseCost, PoseIdx, this, EPoseCandidateFlags::Valid_Pose);
#endif
			}
		}

		if (GetSkipSearchIfPossible() && Result.PoseCost.IsValid())
		{
			SearchContext.UpdateCurrentBestCost(Result.PoseCost);
		}
	}
	else
	{
#if UE_POSE_SEARCH_TRACE_ENABLED
		// calling just for reporting non selectable poses
		FNonSelectableIdx NonSelectableIdx;
		PopulateNonSelectableIdx(NonSelectableIdx, SearchContext, this, QueryValues);
#endif
	}

	// finalizing Result properties
	if (Result.PoseIdx != INDEX_NONE)
	{
		Result.AssetTime = SearchIndex.GetAssetTime(Result.PoseIdx, Schema->GetSamplingInterval());
		Result.Database = this;
	}

	return Result;
}

UE::PoseSearch::FSearchResult UPoseSearchDatabase::SearchBruteForce(UE::PoseSearch::FSearchContext& SearchContext) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_Brute_Force);
	SCOPE_CYCLE_COUNTER(STAT_PoseSearchBruteForce);
	
	using namespace UE::PoseSearch;
	
	FSearchResult Result;

	const FPoseSearchIndex& SearchIndex = GetSearchIndex();

	SearchContext.GetOrBuildQuery(this, Result.ComposedQuery);
	TConstArrayView<float> QueryValues = Result.ComposedQuery.GetValues();

	const bool IsCurrentResultFromThisDatabase = SearchContext.IsCurrentResultFromDatabase(this);
	if (!SearchContext.bForceInterrupt && IsCurrentResultFromThisDatabase)
	{
		// evaluating the continuing pose only if it hasn't already being evaluated and the related animation can advance
		if (SearchContext.bCanAdvance && !Result.ContinuingPoseCost.IsValid())
		{
			Result.PoseIdx = SearchContext.CurrentResult.PoseIdx;
			Result.PoseCost = SearchIndex.ComparePoses(Result.PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::ContinuingPose, Schema->MirrorMismatchCostBias, QueryValues);
			Result.ContinuingPoseCost = Result.PoseCost;

			if (GetSkipSearchIfPossible())
			{
				SearchContext.UpdateCurrentBestCost(Result.PoseCost);
			}
		}
	}

	// since any PoseCost calculated here is at least SearchIndex.MinCostAddend,
	// there's no point in performing the search if CurrentBestTotalCost is already better than that
	if (SearchContext.GetCurrentBestTotalCost() > SearchIndex.MinCostAddend)
	{
		FNonSelectableIdx NonSelectableIdx;
		PopulateNonSelectableIdx(NonSelectableIdx, SearchContext, this, QueryValues);
		check(Algo::IsSorted(NonSelectableIdx));

		const FPoseFilters PoseFilters(Schema, NonSelectableIdx, SearchIndex.OverallFlags);
		for (int32 PoseIdx = 0; PoseIdx < SearchIndex.NumPoses; ++PoseIdx)
		{
			if (PoseFilters.AreFiltersValid(SearchIndex, QueryValues, PoseIdx, SearchIndex.PoseMetadata[PoseIdx]
#if UE_POSE_SEARCH_TRACE_ENABLED
				, SearchContext, this
#endif
			))
			{
				const FPoseSearchCost PoseCost = SearchIndex.ComparePoses(PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::None, Schema->MirrorMismatchCostBias, QueryValues);
				if (PoseCost < Result.PoseCost)
				{
					Result.PoseCost = PoseCost;
					Result.PoseIdx = PoseIdx;
				}

#if UE_POSE_SEARCH_TRACE_ENABLED
				if (PoseSearchMode == EPoseSearchMode::BruteForce)
				{
					SearchContext.BestCandidates.Add(PoseCost, PoseIdx, this, EPoseCandidateFlags::Valid_Pose);
				}
#endif
			}
		}

		if (GetSkipSearchIfPossible() && Result.PoseCost.IsValid())
		{
			SearchContext.UpdateCurrentBestCost(Result.PoseCost);
		}
	}
	else
	{
#if UE_POSE_SEARCH_TRACE_ENABLED
		// calling just for reporting non selectable poses
		FNonSelectableIdx NonSelectableIdx;
		PopulateNonSelectableIdx(NonSelectableIdx, SearchContext, this, QueryValues);
#endif
	}

	// finalizing Result properties
	if (Result.PoseIdx != INDEX_NONE)
	{
		Result.AssetTime = SearchIndex.GetAssetTime(Result.PoseIdx, Schema->GetSamplingInterval());
		Result.Database = this;
	}

#if WITH_EDITORONLY_DATA
	Result.BruteForcePoseCost = Result.PoseCost; 
#endif

	return Result;
}

void UPoseSearchDatabase::BuildQuery(UE::PoseSearch::FSearchContext& SearchContext, FPoseSearchFeatureVectorBuilder& OutQuery) const
{
	check(Schema && Schema->IsValid());
	Schema->BuildQuery(SearchContext, OutQuery);
}

UE::PoseSearch::FSearchResult UPoseSearchDatabaseSet::Search(UE::PoseSearch::FSearchContext& SearchContext) const
{
	using namespace UE::PoseSearch;

	FSearchResult Result;
	FPoseSearchCost ContinuingCost;
#if WITH_EDITOR
	FPoseSearchCost BruteForcePoseCost;
#endif

	// evaluating the continuing pose before all the active entries
	const UPoseSearchDatabase* Database = SearchContext.CurrentResult.Database.Get();
	if (bEvaluateContinuingPoseFirst &&
		SearchContext.bCanAdvance &&
		!SearchContext.bForceInterrupt &&
		SearchContext.CurrentResult.IsValid()
#if WITH_EDITOR
		&& FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest)
#endif
		)
	{
		check(Database);
		const FPoseSearchIndex& SearchIndex = Database->GetSearchIndex();
		SearchContext.GetOrBuildQuery(Database, Result.ComposedQuery);

		TConstArrayView<float> QueryValues = Result.ComposedQuery.GetValues();

		Result.PoseIdx = SearchContext.CurrentResult.PoseIdx;
		Result.PoseCost = SearchIndex.ComparePoses(Result.PoseIdx, SearchContext.QueryMirrorRequest, EPoseComparisonFlags::ContinuingPose, Database->Schema->MirrorMismatchCostBias, QueryValues);
		Result.ContinuingPoseCost = Result.PoseCost;
		ContinuingCost = Result.PoseCost;

		Result.AssetTime = SearchIndex.GetAssetTime(Result.PoseIdx, Database->Schema->GetSamplingInterval());
		Result.Database = Database;

		if (Database->GetSkipSearchIfPossible())
		{
			SearchContext.UpdateCurrentBestCost(Result.PoseCost);
		}
	}

	for (const FPoseSearchDatabaseSetEntry& Entry : AssetsToSearch)
	{
		if (!IsValid(Entry.Searchable))
		{
			UE_LOG(LogPoseSearch, Warning, TEXT("Invalid entry in Database Set %s"), *GetName());
			continue;
		}

		const bool bSearchEntry =
			!Entry.Tag.IsValid() ||
			SearchContext.ActiveTagsContainer == nullptr ||
			SearchContext.ActiveTagsContainer->IsEmpty() ||
			SearchContext.ActiveTagsContainer->HasTag(Entry.Tag);

		if (bSearchEntry)
		{
			FSearchResult EntryResult = Entry.Searchable->Search(SearchContext);

			if (EntryResult.PoseCost.GetTotalCost() < Result.PoseCost.GetTotalCost())
			{
				Result = EntryResult;
			}

			if (EntryResult.ContinuingPoseCost.GetTotalCost() < ContinuingCost.GetTotalCost())
			{
				ContinuingCost = EntryResult.ContinuingPoseCost;
			}
#if WITH_EDITOR
			if (EntryResult.BruteForcePoseCost.GetTotalCost() < BruteForcePoseCost.GetTotalCost())
			{
				BruteForcePoseCost = EntryResult.BruteForcePoseCost;
			}
#endif
			if (Entry.PostSearchStatus == EPoseSearchPostSearchStatus::Stop)
			{
				break;
			}
		}
	}

	Result.ContinuingPoseCost = ContinuingCost;

#if WITH_EDITOR
	Result.BruteForcePoseCost = BruteForcePoseCost;
#endif

	if (!Result.IsValid())
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("Invalid result searching %s"), *GetName());
	}

	return Result;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorBuilder

void FPoseSearchFeatureVectorBuilder::Init(const UPoseSearchSchema* InSchema)
{
	check(InSchema && InSchema->IsValid());
	Schema = InSchema;
	Values.Reset();
	Values.SetNumZeroed(Schema->SchemaCardinality);
}

void FPoseSearchFeatureVectorBuilder::Reset()
{
	Schema = nullptr;
	Values.Reset();
}

namespace UE::PoseSearch
{

void FPoseIndicesHistory::Update(const FSearchResult& SearchResult, float DeltaTime, float MaxTime)
{
	if (MaxTime > 0.f)
	{
		for (auto It = IndexToTime.CreateIterator(); It; ++It)
		{
			It.Value() += DeltaTime;
			if (It.Value() > MaxTime)
			{
				It.RemoveCurrent();
			}
		}

		if (SearchResult.IsValid())
		{
			FHistoricalPoseIndex HistoricalPoseIndex;
			HistoricalPoseIndex.PoseIndex = SearchResult.PoseIdx;
			HistoricalPoseIndex.DatabaseKey = FObjectKey(SearchResult.Database.Get());
			IndexToTime.Add(HistoricalPoseIndex, 0.f);
		}
	}
	else
	{
		IndexToTime.Reset();
	}
}

FTransform FSearchContext::TryGetTransformAndCacheResults(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx)
{
	check(History && Schema);

	static constexpr FBoneIndexType RootBoneIdx = 0xFFFF;
	const FBoneIndexType BoneIndexType = SchemaBoneIdx >= 0 ? Schema->BoneIndices[SchemaBoneIdx] : RootBoneIdx;

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

//////////////////////////////////////////////////////////////////////////
// FPoseHistory

/**
* Fills skeleton transforms with evaluated compact pose transforms.
* Bones that weren't evaluated are filled with the bone's reference pose.
*/
static void CopyCompactToSkeletonPose(const FCompactPose& Pose, TArray<FTransform>& OutLocalTransforms)
{
	const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
	const USkeleton* SkeletonAsset = BoneContainer.GetSkeletonAsset();
	check(SkeletonAsset);

	const FReferenceSkeleton& RefSkeleton = SkeletonAsset->GetReferenceSkeleton();
	TArrayView<const FTransform> RefSkeletonTransforms = MakeArrayView(RefSkeleton.GetRefBonePose());
	const int32 NumSkeletonBones = RefSkeleton.GetNum();

	OutLocalTransforms.SetNum(NumSkeletonBones);

	for (auto SkeletonBoneIdx = FSkeletonPoseBoneIndex(0); SkeletonBoneIdx != NumSkeletonBones; ++SkeletonBoneIdx)
	{
		FCompactPoseBoneIndex CompactBoneIdx = BoneContainer.GetCompactPoseIndexFromSkeletonPoseIndex(SkeletonBoneIdx);
		OutLocalTransforms[SkeletonBoneIdx.GetInt()] = CompactBoneIdx.IsValid() ? Pose[CompactBoneIdx] : RefSkeletonTransforms[SkeletonBoneIdx.GetInt()];
	}
}

void FPoseHistory::Init(int32 InNumPoses, float InTimeHorizon)
{
	Poses.Reserve(InNumPoses);
	TimeHorizon = InTimeHorizon;
}

void FPoseHistory::Init(const FPoseHistory& History)
{
	Poses = History.Poses;
	TimeHorizon = History.TimeHorizon;
}

bool FPoseHistory::TrySampleLocalPose(float SecondsAgo, const TArray<FBoneIndexType>* RequiredBones, TArray<FTransform>* LocalPose, FTransform* RootTransform) const
{
	const int32 NextIdx = LowerBound(Poses.begin(), Poses.end(), SecondsAgo, [](const FPose& Pose, float Value)
	{
		return Value < Pose.Time;
	});
	if (NextIdx <= 0 || NextIdx >= Poses.Num())
	{
		// We may not have accumulated enough poses yet
		return false;
	}

	const int32 PrevIdx = NextIdx - 1;

	const FPose& PrevPose = Poses[PrevIdx];
	const FPose& NextPose = Poses[NextIdx];

#if DO_CHECK
	check(PrevPose.LocalTransforms.Num() == NextPose.LocalTransforms.Num());
	FBoneIndexType MaxBoneIndexType = 0;
	if (RequiredBones)
	{
		for (FBoneIndexType BoneIndexType : *RequiredBones)
		{
			if (BoneIndexType > MaxBoneIndexType)
			{
				MaxBoneIndexType = BoneIndexType;
			}
		}
		check(MaxBoneIndexType < PrevPose.LocalTransforms.Num());
	}
#endif
	// Compute alpha between previous and next Poses
	const float Alpha = FMath::GetMappedRangeValueUnclamped(FVector2f(PrevPose.Time, NextPose.Time), FVector2f(0.0f, 1.0f), SecondsAgo);

	// Lerp between poses by alpha to produce output local pose at requested sample time
	if (LocalPose)
	{
		check(RequiredBones);
		*LocalPose = PrevPose.LocalTransforms;
		FAnimationRuntime::LerpBoneTransforms(*LocalPose, NextPose.LocalTransforms, Alpha, *RequiredBones);
	}

	if (RootTransform)
	{
		RootTransform->Blend(PrevPose.RootTransform, NextPose.RootTransform, Alpha);
	}
	return true;
}

bool FPoseHistory::Update(float SecondsElapsed, const FPoseContext& PoseContext, FTransform ComponentTransform, FText* OutError, ERootUpdateMode UpdateMode)
{
	// Age our elapsed times
	for (FPose& Pose : Poses)
	{
		Pose.Time += SecondsElapsed;
	}

	if (Poses.Num() != Poses.Max())
	{
		// Consume every pose until the queue is full
		Poses.Emplace();
	}
	else
	{
		// Exercise pose retention policy. We must guarantee there is always one additional pose
		// beyond the time horizon so we can compute derivatives at the time horizon. We also
		// want to evenly distribute poses across the entire history buffer so we only push additional
		// poses when enough time has elapsed.

		const float SampleInterval = GetSampleTimeInterval();

		bool bCanEvictOldest = Poses[1].Time >= TimeHorizon + SampleInterval;
		bool bShouldPushNewest = Poses[Poses.Num() - 2].Time >= SampleInterval;

		if (bCanEvictOldest && bShouldPushNewest)
		{
			FPose PoseTemp = MoveTemp(Poses.First());
			Poses.PopFront();
			Poses.Emplace(MoveTemp(PoseTemp));
		}
	}

	// Regardless of the retention policy, we always update the most recent pose
	FPose& CurrentPose = Poses.Last();
	CurrentPose.Time = 0.f;
	CopyCompactToSkeletonPose(PoseContext.Pose, CurrentPose.LocalTransforms);

	// Initialize with Previous Root Transform or Identity
	CurrentPose.RootTransform = Poses.Num() > 1 ? Poses[Poses.Num() - 2].RootTransform : FTransform::Identity;
	
	// Update using either AniumRootMotionProvider or Component Transform
	if (UpdateMode == ERootUpdateMode::RootMotionDelta)
	{
		const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

		if (RootMotionProvider)
		{
			if (RootMotionProvider->HasRootMotion(PoseContext.CustomAttributes))
			{
				FTransform RootMotionDelta = FTransform::Identity;
				RootMotionProvider->ExtractRootMotion(PoseContext.CustomAttributes, RootMotionDelta);

				CurrentPose.RootTransform = RootMotionDelta * CurrentPose.RootTransform;
			}
#if WITH_EDITORONLY_DATA	
			else
			{
				if (OutError)
				{
					*OutError = LOCTEXT("PoseHistoryRootMotionProviderError",
						"Input to Pose History has no Root Motion Attribute. Try disabling 'Use Root Motion'.");
				}
				return false;
			}
#endif
		}
#if WITH_EDITORONLY_DATA	
		else
		{
			if (OutError)
			{
				*OutError = LOCTEXT("PoseHistoryRootMotionAttributeError",
					"Could not get Root Motion Provider. Try disabling 'Use Root Motion'.");
			}
			return false;
		}
#endif
	}
	else if (UpdateMode == ERootUpdateMode::ComponentTransformDelta)
	{
		CurrentPose.RootTransform = ComponentTransform;
	}
	else
	{
		checkNoEntry();
	}

	return true;
}

float FPoseHistory::GetSampleTimeInterval() const
{
	// Reserve one pose for computing derivatives at the time horizon
	return TimeHorizon / (Poses.Max() - 1);
}

//////////////////////////////////////////////////////////////////////////
// FFeatureVectorHelper
void FFeatureVectorHelper::EncodeQuat(TArrayView<float> Values, int32& DataOffset, const FQuat& Quat)
{
	const FVector X = Quat.GetAxisX();
	const FVector Y = Quat.GetAxisY();

	Values[DataOffset + 0] = X.X;
	Values[DataOffset + 1] = X.Y;
	Values[DataOffset + 2] = X.Z;
	Values[DataOffset + 3] = Y.X;
	Values[DataOffset + 4] = Y.Y;
	Values[DataOffset + 5] = Y.Z;

	DataOffset += EncodeQuatCardinality;
}

void FFeatureVectorHelper::EncodeQuat(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	FQuat Quat = DecodeQuatInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Quat = FQuat::Slerp(Quat, DecodeQuatInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Quat = FQuat::Slerp(Quat, DecodeQuatInternal(NextValues, DataOffset), LerpValue);
		}
	}
	
	// @todo: do we need to add options for cubic interpolation?
	EncodeQuat(Values, DataOffset, Quat);
}

FQuat FFeatureVectorHelper::DecodeQuat(TConstArrayView<float> Values, int32& DataOffset)
{
	const FQuat Quat = DecodeQuatInternal(Values, DataOffset);
	DataOffset += EncodeQuatCardinality;
	return Quat;
}

FQuat FFeatureVectorHelper::DecodeQuatInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	const FVector X(Values[DataOffset + 0], Values[DataOffset + 1], Values[DataOffset + 2]);
	const FVector Y(Values[DataOffset + 3], Values[DataOffset + 4], Values[DataOffset + 5]);
	const FVector Z = FVector::CrossProduct(X, Y);

	FMatrix M(FMatrix::Identity);
	M.SetColumn(0, X);
	M.SetColumn(1, Y);
	M.SetColumn(2, Z);

	return FQuat(M);
}

void FFeatureVectorHelper::EncodeVector(TArrayView<float> Values, int32& DataOffset, const FVector& Vector)
{
	Values[DataOffset + 0] = Vector.X;
	Values[DataOffset + 1] = Vector.Y;
	Values[DataOffset + 2] = Vector.Z;
	DataOffset += EncodeVectorCardinality;
}

void FFeatureVectorHelper::EncodeVector(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue, bool bNormalize)
{
	FVector Vector = DecodeVectorInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Vector = FMath::Lerp(Vector, DecodeVectorInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Vector = FMath::Lerp(Vector, DecodeVectorInternal(NextValues, DataOffset), LerpValue);
		}
	}
	
	// @todo: do we need to add options for cubic interpolation?
	if (bNormalize)
	{
		Vector = Vector.GetSafeNormal(UE_SMALL_NUMBER, FVector::XAxisVector);
	}

	EncodeVector(Values, DataOffset, Vector);
}

FVector FFeatureVectorHelper::DecodeVector(TConstArrayView<float> Values, int32& DataOffset)
{
	const FVector Vector = DecodeVectorInternal(Values, DataOffset);
	DataOffset += EncodeVectorCardinality;
	return Vector;
}

FVector FFeatureVectorHelper::DecodeVectorInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return FVector(Values[DataOffset + 0], Values[DataOffset + 1], Values[DataOffset + 2]);
}

void FFeatureVectorHelper::EncodeVector2D(TArrayView<float> Values, int32& DataOffset, const FVector2D& Vector2D)
{
	Values[DataOffset + 0] = Vector2D.X;
	Values[DataOffset + 1] = Vector2D.Y;
	DataOffset += EncodeVector2DCardinality;
}

void FFeatureVectorHelper::EncodeVector2D(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	FVector2D Vector2D = DecodeVector2DInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Vector2D = FMath::Lerp(Vector2D, DecodeVector2DInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Vector2D = FMath::Lerp(Vector2D, DecodeVector2DInternal(NextValues, DataOffset), LerpValue);
		}
	}
	
	// @todo: do we need to add options for cubic interpolation?
	EncodeVector2D(Values, DataOffset, Vector2D);
}

FVector2D FFeatureVectorHelper::DecodeVector2D(TConstArrayView<float> Values, int32& DataOffset)
{
	const FVector2D Vector2D = DecodeVector2DInternal(Values, DataOffset);
	DataOffset += EncodeVector2DCardinality;
	return Vector2D;
}

FVector2D FFeatureVectorHelper::DecodeVector2DInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return FVector2D(Values[DataOffset + 0], Values[DataOffset + 1]);
}

void FFeatureVectorHelper::EncodeFloat(TArrayView<float> Values, int32& DataOffset, const float Value)
{
	Values[DataOffset + 0] = Value;
	DataOffset += EncodeFloatCardinality;
}

void FFeatureVectorHelper::EncodeFloat(TArrayView<float> Values, int32& DataOffset, TConstArrayView<float> PrevValues, TConstArrayView<float> CurValues, TConstArrayView<float> NextValues, float LerpValue)
{
	float Value = DecodeFloatInternal(CurValues, DataOffset);

	// linear interpolation
	if (!FMath::IsNearlyZero(LerpValue))
	{
		if (LerpValue < 0.f)
		{
			Value = FMath::Lerp(Value, DecodeFloatInternal(PrevValues, DataOffset), -LerpValue);
		}
		else
		{
			Value = FMath::Lerp(Value, DecodeFloatInternal(NextValues, DataOffset), LerpValue);
		}
	}

	// @todo: do we need to add options for cubic interpolation?
	EncodeFloat(Values, DataOffset, Value);
}

float FFeatureVectorHelper::DecodeFloat(TConstArrayView<float> Values, int32& DataOffset)
{
	const float Value = DecodeFloatInternal(Values, DataOffset);
	DataOffset += EncodeFloatCardinality;
	return Value;
}

float FFeatureVectorHelper::DecodeFloatInternal(TConstArrayView<float> Values, int32 DataOffset)
{
	return Values[DataOffset];
}

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

//////////////////////////////////////////////////////////////////////////
// FSearchResult
// 

void FSearchResult::Update(float NewAssetTime)
{
	if (!IsValid())
	{
		Reset();
	}
	else
	{
		const FPoseSearchIndexAsset& SearchIndexAsset = Database->GetSearchIndex().GetAssetForPose(PoseIdx);
		const FInstancedStruct& DatabaseAsset = Database->GetAnimationAssetStruct(SearchIndexAsset);
		if (DatabaseAsset.GetPtr<FPoseSearchDatabaseSequence>() || DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
		{
			if (Database->GetPoseIndicesAndLerpValueFromTime(NewAssetTime, SearchIndexAsset, PrevPoseIdx, PoseIdx, NextPoseIdx, LerpValue))
			{
				AssetTime = NewAssetTime;
			}
			else
			{
				Reset();
			}
		}
		else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
		{
			TArray<FBlendSampleData> BlendSamples;
			int32 TriangulationIndex = 0;
			DatabaseBlendSpace->BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset.BlendParameters, BlendSamples, TriangulationIndex, true);

			const float PlayLength = DatabaseBlendSpace->BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

			// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
			// to a real time before we advance it
			const float RealTime = NewAssetTime * PlayLength;
			if (Database->GetPoseIndicesAndLerpValueFromTime(RealTime, SearchIndexAsset, PrevPoseIdx, PoseIdx, NextPoseIdx, LerpValue))
			{
				AssetTime = NewAssetTime;
			}
			else
			{
				Reset();
			}
		}
		else
		{
			checkNoEntry();
		}
	}
}

bool FSearchResult::IsValid() const
{
	return PoseIdx != INDEX_NONE && Database.IsValid();
}

void FSearchResult::Reset()
{
	PoseIdx = INDEX_NONE;
	Database = nullptr;
	ComposedQuery.Reset();
	AssetTime = 0.0f;
}

const FPoseSearchIndexAsset* FSearchResult::GetSearchIndexAsset(bool bMandatory) const
{
	if (bMandatory)
	{
		check(IsValid());
	}
	else if (!IsValid())
	{
		return nullptr;
	}

	return &Database->GetSearchIndex().GetAssetForPose(PoseIdx);
}


//////////////////////////////////////////////////////////////////////////
// FAssetSamplerContext

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
	const EAxis::Type MirrorAxis = MirrorDataTable->MirrorAxis;
	FVector T = InTransform.GetTranslation();
	T = FAnimationRuntime::MirrorVector(T, MirrorAxis);
	const FQuat ReferenceRotation = ComponentSpaceRefRotations[FCompactPoseBoneIndex(0)];
	FQuat Q = InTransform.GetRotation();
	Q = FAnimationRuntime::MirrorQuat(Q, MirrorAxis);
	Q *= FAnimationRuntime::MirrorQuat(ReferenceRotation, MirrorAxis).Inverse() * ReferenceRotation;
	FTransform Result = FTransform(Q, T, InTransform.GetScale3D());
	return Result;
}

//////////////////////////////////////////////////////////////////////////
// class ICostBreakDownData

void ICostBreakDownData::AddEntireBreakDownSection(const FText& Label, const UPoseSearchSchema* Schema, int32 DataOffset, int32 Cardinality)
{
	BeginBreakDownSection(Label);

	const int32 Count = Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (IsCostVectorFromSchema(i, Schema))
		{
			const float CostBreakdown = ArraySum(GetCostVector(i, Schema), DataOffset, Cardinality);
			SetCostBreakDown(CostBreakdown, i, Schema);
		}
	}

	EndBreakDownSection(Label);
}

//////////////////////////////////////////////////////////////////////////
// Root motion extrapolation

// Uses distance delta between NextRootDistanceIndex and NextRootDistanceIndex - 1 and extrapolates it to ExtrapolationTime
static float ExtrapolateAccumulatedRootDistance(
	int32 SamplingRate,
	TConstArrayView<float> AccumulatedRootDistance,
	int32 NextRootDistanceIndex, 
	float ExtrapolationTime,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	check(NextRootDistanceIndex > 0 && NextRootDistanceIndex < AccumulatedRootDistance.Num());

	const float DistanceDelta =
		AccumulatedRootDistance[NextRootDistanceIndex] -
		AccumulatedRootDistance[NextRootDistanceIndex - 1];
	const float Speed = DistanceDelta * SamplingRate;
	const float ExtrapolationSpeed = Speed >= ExtrapolationParameters.LinearSpeedThreshold ?
		Speed : 0.0f;
	const float ExtrapolatedDistance = ExtrapolationSpeed * ExtrapolationTime;

	return ExtrapolatedDistance;
}

static float ExtractAccumulatedRootDistance(
	int32 SamplingRate,
	TConstArrayView<float> AccumulatedRootDistance,
	float PlayLength,
	float Time,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);

	// Find the distance sample that corresponds with the time and split into whole and partial parts
	float IntegralDistanceSample;
	float DistanceAlpha = FMath::Modf(ClampedTime * SamplingRate, &IntegralDistanceSample);
	float DistanceIdx = (int32)IntegralDistanceSample;

	// Verify the distance offset and any residual portion would be in bounds
	check(DistanceIdx + (DistanceAlpha > 0.0f ? 1 : 0) < AccumulatedRootDistance.Num());

	// Look up the distance and interpolate between distance samples if necessary
	float Distance = AccumulatedRootDistance[DistanceIdx];
	if (DistanceAlpha > 0.0f)
	{
		float NextDistance = AccumulatedRootDistance[DistanceIdx + 1];
		Distance = FMath::Lerp(Distance, NextDistance, DistanceAlpha);
	}

	const float ExtrapolationTime = Time - ClampedTime;

	if (ExtrapolationTime != 0.0f)
	{
		// If extrapolationTime is not zero, we extrapolate the beginning or the end of the animation to estimate
		// the root distance.
		const int32 DistIdx = (ExtrapolationTime > 0.0f) ? AccumulatedRootDistance.Num() - 1 : 1;
		const float ExtrapolatedDistance = ExtrapolateAccumulatedRootDistance(
			SamplingRate,
			AccumulatedRootDistance,
			DistIdx,
			ExtrapolationTime,
			ExtrapolationParameters);
		Distance += ExtrapolatedDistance;
	}

	return Distance;
}

static FTransform ExtrapolateRootMotion(
	FTransform SampleToExtrapolate,
	float SampleStart, 
	float SampleEnd, 
	float ExtrapolationTime,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	const float SampleDelta = SampleEnd - SampleStart;
	check(!FMath::IsNearlyZero(SampleDelta));

	const FVector LinearVelocityToExtrapolate = SampleToExtrapolate.GetTranslation() / SampleDelta;
	const float LinearSpeedToExtrapolate = LinearVelocityToExtrapolate.Size();
	const bool bCanExtrapolateTranslation =
		LinearSpeedToExtrapolate >= ExtrapolationParameters.LinearSpeedThreshold;

	const float AngularSpeedToExtrapolateRad = SampleToExtrapolate.GetRotation().GetAngle() / SampleDelta;
	const bool bCanExtrapolateRotation =
		FMath::RadiansToDegrees(AngularSpeedToExtrapolateRad) >= ExtrapolationParameters.AngularSpeedThreshold;

	if (!bCanExtrapolateTranslation && !bCanExtrapolateRotation)
	{
		return FTransform::Identity;
	}

	if (!bCanExtrapolateTranslation)
	{
		SampleToExtrapolate.SetTranslation(FVector::ZeroVector);
	}

	if (!bCanExtrapolateRotation)
	{
		SampleToExtrapolate.SetRotation(FQuat::Identity);
	}

	// converting ExtrapolationTime to a positive number to avoid dealing with the negative extrapolation and inverting
	// transforms later on.
	const float AbsExtrapolationTime = FMath::Abs(ExtrapolationTime);
	const float AbsSampleDelta = FMath::Abs(SampleDelta);
	const FTransform AbsTimeSampleToExtrapolate =
		ExtrapolationTime >= 0.0f ? SampleToExtrapolate : SampleToExtrapolate.Inverse();

	// because we're extrapolating rotation, the extrapolation must be integrated over time
	const float SampleMultiplier = AbsExtrapolationTime / AbsSampleDelta;
	float IntegralNumSamples;
	float RemainingSampleFraction = FMath::Modf(SampleMultiplier, &IntegralNumSamples);
	int32 NumSamples = (int32)IntegralNumSamples;

	// adding full samples to the extrapolated root motion
	FTransform ExtrapolatedRootMotion = FTransform::Identity;
	for (int i = 0; i < NumSamples; ++i)
	{
		ExtrapolatedRootMotion = AbsTimeSampleToExtrapolate * ExtrapolatedRootMotion;
	}

	// and a blend with identity for whatever is left
	FTransform RemainingExtrapolatedRootMotion;
	RemainingExtrapolatedRootMotion.Blend(
		FTransform::Identity,
		AbsTimeSampleToExtrapolate,
		RemainingSampleFraction);

	ExtrapolatedRootMotion = RemainingExtrapolatedRootMotion * ExtrapolatedRootMotion;
	return ExtrapolatedRootMotion;
}

//////////////////////////////////////////////////////////////////////////
// FSequenceBaseSampler
void FSequenceBaseSampler::Init(const FInput& InInput)
{
	check(InInput.SequenceBase.Get());

	Input = InInput;
}

void FSequenceBaseSampler::Process()
{
	ProcessRootDistance();
}

float FSequenceBaseSampler::GetTimeFromRootDistance(float Distance) const
{
	int32 NextSampleIdx = 1;
	int32 PrevSampleIdx = 0;
	if (Distance > 0.0f)
	{
		// Search for the distance value. Because the values will be extrapolated if necessary
		// LowerBound might go past the end of the array, in which case the last valid index is used
		int32 ClipDistanceLowerBoundIndex = Algo::LowerBound(AccumulatedRootDistance, Distance);
		NextSampleIdx = FMath::Min(
			ClipDistanceLowerBoundIndex,
			AccumulatedRootDistance.Num() - 1);

		// Compute distance interpolation amount
		PrevSampleIdx = FMath::Max(0, NextSampleIdx - 1);
	}

	float NextDistance = AccumulatedRootDistance[NextSampleIdx];
	float PrevDistance = AccumulatedRootDistance[PrevSampleIdx];
	float DistanceSampleAlpha = FMath::GetRangePct(PrevDistance, NextDistance, Distance);

	// Convert to time
	float ClipTime = (float(NextSampleIdx) - (1.0f - DistanceSampleAlpha)) / Input.RootDistanceSamplingRate;
	return ClipTime;
}

bool FSequenceBaseSampler::IsLoopable() const
{
	return Input.SequenceBase->bLoop;
}

void FSequenceBaseSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	Input.SequenceBase->GetAnimationPose(OutAnimPoseData, ExtractionCtx);
}

FTransform FSequenceBaseSampler::ExtractRootTransform(float Time) const
{
	FTransform RootTransform = FTransform::Identity;
	const UAnimSequenceBase* SequenceBase = Input.SequenceBase.Get();

	if (IsLoopable())
	{
		FTransform LoopableRootTransform = SequenceBase->ExtractRootMotion(0.0f, Time, true);
		return LoopableRootTransform;
	}

	const float ExtrapolationSampleTime = Input.ExtrapolationParameters.SampleTime;

	const float PlayLength = SequenceBase->GetPlayLength();
	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
	const float ExtrapolationTime = Time - ClampedTime;

	// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
	// animation to estimate where the root would be at Time
	if (ExtrapolationTime < -SMALL_NUMBER)
	{
		FTransform SampleToExtrapolate = SequenceBase->ExtractRootMotionFromRange(0.0f, ExtrapolationSampleTime);

		const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
			SampleToExtrapolate,
			0.0f, ExtrapolationSampleTime,
			ExtrapolationTime,
			Input.ExtrapolationParameters);
		RootTransform = ExtrapolatedRootMotion;
	}
	else
	{
		RootTransform = SequenceBase->ExtractRootMotionFromRange(0.0f, ClampedTime);

		// If Time is greater than PlayLength, ExtrapolationTIme will be a positive number. In this case, we extrapolate
		// the end of the animation to estimate where the root would be at Time
		if (ExtrapolationTime > SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = SequenceBase->ExtractRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

			const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
				SampleToExtrapolate,
				PlayLength - ExtrapolationSampleTime, PlayLength,
				ExtrapolationTime,
				Input.ExtrapolationParameters);
			RootTransform = ExtrapolatedRootMotion * RootTransform;
		}
	}

	return RootTransform;
}

float FSequenceBaseSampler::ExtractRootDistance(float Time) const
{
	return ExtractAccumulatedRootDistance(
		Input.RootDistanceSamplingRate,
		AccumulatedRootDistance,
		Input.SequenceBase->GetPlayLength(),
		Time,
		Input.ExtrapolationParameters);
}

void FSequenceBaseSampler::ExtractPoseSearchNotifyStates(
	float Time, 
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
	constexpr float ExtractionInterval = 1.0f / 120.0f;
	FAnimNotifyContext NotifyContext;
	Input.SequenceBase->GetAnimNotifies(Time - (ExtractionInterval * 0.5f), ExtractionInterval, NotifyContext);

	// check which notifies actually overlap Time and are of the right base type
	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
		if (!NotifyEvent)
		{
			continue;
		}

		if (NotifyEvent->GetTriggerTime() > Time ||
			NotifyEvent->GetEndTriggerTime() < Time)
		{
			continue;
		}

		UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify = 
			Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
		if (PoseSearchAnimNotify)
		{
			NotifyStates.Add(PoseSearchAnimNotify);
		}
	}
}

void FSequenceBaseSampler::ProcessRootDistance()
{
	const UAnimSequenceBase* SequenceBase = Input.SequenceBase.Get();
	
	// Note the distance sampling interval is independent of the schema's sampling interval
	const float DistanceSamplingInterval = 1.0f / Input.RootDistanceSamplingRate;

	const FTransform InitialRootTransform = SequenceBase->ExtractRootTrackTransform(0.0f, nullptr);

	uint32 NumDistanceSamples = FMath::CeilToInt(SequenceBase->GetPlayLength() * Input.RootDistanceSamplingRate) + 1;
	AccumulatedRootDistance.Reserve(NumDistanceSamples);

	// Build a distance lookup table by sampling root motion at a fixed rate and accumulating
	// absolute translation deltas. During indexing we'll bsearch this table and interpolate
	// between samples in order to convert distance offsets to time offsets.
	// See also FAssetIndexer::AddTrajectoryDistanceFeatures().

	double TotalAccumulatedRootDistance = 0.0;
	FTransform LastRootTransform = InitialRootTransform;
	float SampleTime = 0.0f;
	for (int32 SampleIdx = 0; SampleIdx != NumDistanceSamples; ++SampleIdx)
	{
		SampleTime = FMath::Min(SampleIdx * DistanceSamplingInterval, SequenceBase->GetPlayLength());

		FTransform RootTransform = SequenceBase->ExtractRootTrackTransform(SampleTime, nullptr);
		FTransform LocalRootMotion = RootTransform.GetRelativeTransform(LastRootTransform);
		LastRootTransform = RootTransform;

		TotalAccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		AccumulatedRootDistance.Add((float)TotalAccumulatedRootDistance);
	}

	// Verify we sampled the final frame of the clip
	check(SampleTime == SequenceBase->GetPlayLength());

	// Also emit root motion summary info to help with sample wrapping in FAssetIndexer::GetSampleInfo()
	TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	TotalRootDistance = AccumulatedRootDistance.Last();
}

const UAnimationAsset* FSequenceBaseSampler::GetAsset() const
{
	return Input.SequenceBase.Get();
}

//////////////////////////////////////////////////////////////////////////
// FBlendSpaceSampler
void FBlendSpaceSampler::Init(const FInput& InInput)
{
	check(InInput.BlendSpace.Get());

	Input = InInput;
}

void FBlendSpaceSampler::Process()
{
	FMemMark Mark(FMemStack::Get());

	ProcessPlayLength();
	ProcessRootTransform();
	ProcessRootDistance();
}

float FBlendSpaceSampler::GetTimeFromRootDistance(float Distance) const
{
	int32 NextSampleIdx = 1;
	int32 PrevSampleIdx = 0;
	if (Distance > 0.0f)
	{
		// Search for the distance value. Because the values will be extrapolated if necessary
		// LowerBound might go past the end of the array, in which case the last valid index is used
		int32 ClipDistanceLowerBoundIndex = Algo::LowerBound(AccumulatedRootDistance, Distance);
		NextSampleIdx = FMath::Min(
			ClipDistanceLowerBoundIndex,
			AccumulatedRootDistance.Num() - 1);

		// Compute distance interpolation amount
		PrevSampleIdx = FMath::Max(0, NextSampleIdx - 1);
	}

	float NextDistance = AccumulatedRootDistance[NextSampleIdx];
	float PrevDistance = AccumulatedRootDistance[PrevSampleIdx];
	float DistanceSampleAlpha = FMath::GetRangePct(PrevDistance, NextDistance, Distance);

	// Convert to time
	float ClipTime = (float(NextSampleIdx) - (1.0f - DistanceSampleAlpha)) / Input.RootDistanceSamplingRate;
	return ClipTime;
}

bool FBlendSpaceSampler::IsLoopable() const
{
	return Input.BlendSpace->bLoop;
}

void FBlendSpaceSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
	{
		float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

		FDeltaTimeRecord BlendSampleDeltaTimeRecord;
		BlendSampleDeltaTimeRecord.Set(ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale, ExtractionCtx.DeltaTimeRecord.Delta * Scale);

		BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
		BlendSamples[BlendSampleIdex].PreviousTime = ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale;
		BlendSamples[BlendSampleIdex].Time = ExtractionCtx.CurrentTime * Scale;
	}

	Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, OutAnimPoseData);
}

FTransform FBlendSpaceSampler::ExtractRootTransform(float Time) const
{
	if (IsLoopable())
	{
		FTransform LoopableRootTransform = ExtractBlendSpaceRootMotion(0.0f, Time, true);
		return LoopableRootTransform;
	}

	const float ExtrapolationSampleTime = Input.ExtrapolationParameters.SampleTime;

	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
	const float ExtrapolationTime = Time - ClampedTime;

	FTransform RootTransform = FTransform::Identity;

	// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
	// animation to estimate where the root would be at Time
	if (ExtrapolationTime < -SMALL_NUMBER)
	{
		FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(0.0f, ExtrapolationSampleTime);

		const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
			SampleToExtrapolate,
			0.0f, ExtrapolationSampleTime,
			ExtrapolationTime,
			Input.ExtrapolationParameters);
		RootTransform = ExtrapolatedRootMotion;
	}
	else
	{
		RootTransform = ExtractBlendSpaceRootMotionFromRange(0.0f, ClampedTime);

		// If Time is greater than PlayLength, ExtrapolationTIme will be a positive number. In this case, we extrapolate
		// the end of the animation to estimate where the root would be at Time
		if (ExtrapolationTime > SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

			const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
				SampleToExtrapolate,
				PlayLength - ExtrapolationSampleTime, PlayLength,
				ExtrapolationTime,
				Input.ExtrapolationParameters);
			RootTransform = ExtrapolatedRootMotion * RootTransform;
		}
	}

	return RootTransform;
}

float FBlendSpaceSampler::ExtractRootDistance(float Time) const
{
	return ExtractAccumulatedRootDistance(
		Input.RootDistanceSamplingRate,
		AccumulatedRootDistance,
		PlayLength,
		Time,
		Input.ExtrapolationParameters);
}

static int32 GetHighestWeightSample(const TArray<struct FBlendSampleData>& SampleDataList)
{
	int32 HighestWeightIndex = 0;
	float HighestWeight = SampleDataList[HighestWeightIndex].GetClampedWeight();
	for (int32 I = 1; I < SampleDataList.Num(); I++)
	{
		if (SampleDataList[I].GetClampedWeight() > HighestWeight)
		{
			HighestWeightIndex = I;
			HighestWeight = SampleDataList[I].GetClampedWeight();
		}
	}
	return HighestWeightIndex;
}

void FBlendSpaceSampler::ExtractPoseSearchNotifyStates(
	float Time,
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	if (Input.BlendSpace->NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation)
	{
		// Set up blend samples
		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

		// Find highest weighted
		const int32 HighestWeightIndex = GetHighestWeightSample(BlendSamples);

		check(HighestWeightIndex != -1);

		// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
		constexpr float ExtractionInterval = 1.0f / 120.0f;

		float SampleTime = Time * (BlendSamples[HighestWeightIndex].Animation->GetPlayLength() / PlayLength);

		// Get notifies for highest weighted
		FAnimNotifyContext NotifyContext;
		BlendSamples[HighestWeightIndex].Animation->GetAnimNotifies(
			(SampleTime - (ExtractionInterval * 0.5f)),
			ExtractionInterval, 
			NotifyContext);

		// check which notifies actually overlap Time and are of the right base type
		for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
		{
			const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
			if (!NotifyEvent)
			{
				continue;
			}

			if (NotifyEvent->GetTriggerTime() > SampleTime ||
				NotifyEvent->GetEndTriggerTime() < SampleTime)
			{
				continue;
			}

			UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify =
				Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
			if (PoseSearchAnimNotify)
			{
				NotifyStates.Add(PoseSearchAnimNotify);
			}
		}
	}
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootTrackTransform(float Time) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	int32 Index = Time * Input.RootTransformSamplingRate;
	int32 FirstIndexClamped = FMath::Clamp(Index + 0, 0, AccumulatedRootTransform.Num() - 1);
	int32 SecondIndexClamped = FMath::Clamp(Index + 1, 0, AccumulatedRootTransform.Num() - 1);
	float Alpha = FMath::Fmod(Time * Input.RootTransformSamplingRate, 1.0f);
	FTransform OutputTransform;
	OutputTransform.Blend(
		AccumulatedRootTransform[FirstIndexClamped],
		AccumulatedRootTransform[SecondIndexClamped],
		Alpha);

	return OutputTransform;
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	FTransform RootTransformRefPose = ExtractBlendSpaceRootTrackTransform(0.0f);

	FTransform StartTransform = ExtractBlendSpaceRootTrackTransform(StartTrackPosition);
	FTransform EndTransform = ExtractBlendSpaceRootTrackTransform(EndTrackPosition);

	// Transform to Component Space
	const FTransform RootToComponent = RootTransformRefPose.Inverse();
	StartTransform = RootToComponent * StartTransform;
	EndTransform = RootToComponent * EndTransform;

	return EndTransform.GetRelativeTransform(StartTransform);
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		bool const bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// Disable looping here. Advance to desired position, or beginning / end of animation 
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, PlayLength);

			// Verify position assumptions
			//ensureMsgf(bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition), TEXT("in Animation %s(Skeleton %s) : bPlayingBackwards(%d), PreviousPosition(%0.2f), Current Position(%0.2f)"),
			//	*GetName(), *GetNameSafe(GetSkeleton()), bPlayingBackwards, PreviousPosition, CurrentPosition);

			RootMotionParams.Accumulate(ExtractBlendSpaceRootMotionFromRange(PreviousPosition, CurrentPosition));

			// If we've hit the end of the animation, and we're allowed to loop, keep going.
			if ((AdvanceType == ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? PlayLength : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

void FBlendSpaceSampler::ProcessPlayLength()
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	PlayLength = Input.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);
}

void FBlendSpaceSampler::ProcessRootTransform()
{
	// Pre-compute root motion

	int32 NumRootSamples = FMath::Max(PlayLength * Input.RootTransformSamplingRate + 1, 1);
	AccumulatedRootTransform.SetNumUninitialized(NumRootSamples);

	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	FTransform RootMotionAccumulation = FTransform::Identity;

	AccumulatedRootTransform[0] = RootMotionAccumulation;

	for (int32 SampleIdx = 1; SampleIdx < NumRootSamples; ++SampleIdx)
	{
		float PreviousTime = float(SampleIdx - 1) / Input.RootTransformSamplingRate;
		float CurrentTime = float(SampleIdx - 0) / Input.RootTransformSamplingRate;

		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(static_cast<double>(CurrentTime), true, DeltaTimeRecord, IsLoopable());

		for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
		{
			float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

			FDeltaTimeRecord BlendSampleDeltaTimeRecord;
			BlendSampleDeltaTimeRecord.Set(DeltaTimeRecord.GetPrevious() * Scale, DeltaTimeRecord.Delta * Scale);

			BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
			BlendSamples[BlendSampleIdex].PreviousTime = PreviousTime * Scale;
			BlendSamples[BlendSampleIdex].Time = CurrentTime * Scale;
		}

		FCompactPose Pose;
		FBlendedCurve BlendedCurve;
		Anim::FStackAttributeContainer StackAttributeContainer;
		FAnimationPoseData AnimPoseData(Pose, BlendedCurve, StackAttributeContainer);

		Pose.SetBoneContainer(&Input.BoneContainer);
		BlendedCurve.InitFrom(Input.BoneContainer);

		Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, AnimPoseData);

		const Anim::IAnimRootMotionProvider* RootMotionProvider = Anim::IAnimRootMotionProvider::Get();

		if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
		{
			if (ensureMsgf(RootMotionProvider->HasRootMotion(StackAttributeContainer), TEXT("Blend Space had no Root Motion Attribute.")))
			{
				FTransform RootMotionDelta;
				RootMotionProvider->ExtractRootMotion(StackAttributeContainer, RootMotionDelta);

				RootMotionAccumulation = RootMotionDelta * RootMotionAccumulation;
			}
		}

		AccumulatedRootTransform[SampleIdx] = RootMotionAccumulation;
	}
}

void FBlendSpaceSampler::ProcessRootDistance()
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	// Note the distance sampling interval is independent of the schema's sampling interval
	const float DistanceSamplingInterval = 1.0f / Input.RootDistanceSamplingRate;

	const FTransform InitialRootTransform = FTransform::Identity;

	uint32 NumDistanceSamples = FMath::CeilToInt(PlayLength * Input.RootDistanceSamplingRate) + 1;
	AccumulatedRootDistance.Reserve(NumDistanceSamples);

	// Build a distance lookup table by sampling root motion at a fixed rate and accumulating
	// absolute translation deltas. During indexing we'll bsearch this table and interpolate
	// between samples in order to convert distance offsets to time offsets.
	// See also FAssetIndexer::AddTrajectoryDistanceFeatures().
	double TotalAccumulatedRootDistance = 0.0;
	FTransform LastRootTransform = InitialRootTransform;
	float SampleTime = 0.0f;
	for (int32 SampleIdx = 0; SampleIdx != NumDistanceSamples; ++SampleIdx)
	{
		SampleTime = FMath::Min(SampleIdx * DistanceSamplingInterval, PlayLength);

		FTransform RootTransform = ExtractBlendSpaceRootTrackTransform(SampleTime);
		FTransform LocalRootMotion = RootTransform.GetRelativeTransform(LastRootTransform);
		LastRootTransform = RootTransform;

		TotalAccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		AccumulatedRootDistance.Add((float)TotalAccumulatedRootDistance);
	}

	// Verify we sampled the final frame of the clip
	check(SampleTime == PlayLength);

	// Also emit root motion summary info to help with sample wrapping in FAssetIndexer::GetSampleInfo()
	TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	TotalRootDistance = AccumulatedRootDistance.Last(); 
}

const UAnimationAsset* FBlendSpaceSampler::GetAsset() const
{
	return Input.BlendSpace.Get();
}

//////////////////////////////////////////////////////////////////////////
// PoseSearch API

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

void DrawSearchIndex(const FDebugDrawParams& DrawParams)
{
#if ENABLE_DRAW_DEBUG
	if (DrawParams.CanDraw())
	{
		const FPoseSearchIndex* SearchIndex = DrawParams.GetSearchIndex();
		const int32 LastPoseIdx = SearchIndex->NumPoses;
		for (int32 PoseIdx = 0; PoseIdx != LastPoseIdx; ++PoseIdx)
		{
			DrawFeatureVector(DrawParams, PoseIdx);
		}
	}
#endif // ENABLE_DRAW_DEBUG
}

} // namespace UE::PoseSearch

#undef LOCTEXT_NAMESPACE