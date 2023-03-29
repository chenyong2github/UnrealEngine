// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearchEigenHelper.h"

namespace UE::PoseSearch
{

float CompareFeatureVectors(TConstArrayView<float> A, TConstArrayView<float> B, TConstArrayView<float> WeightsSqrt)
{
	check(A.Num() == B.Num() && A.Num() == WeightsSqrt.Num());

	Eigen::Map<const Eigen::ArrayXf> VA(A.GetData(), A.Num());
	Eigen::Map<const Eigen::ArrayXf> VB(B.GetData(), B.Num());
	Eigen::Map<const Eigen::ArrayXf> VW(WeightsSqrt.GetData(), WeightsSqrt.Num());

	return ((VA - VB) * VW).square().sum();
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

} // namespace UE::PoseSearch

//////////////////////////////////////////////////////////////////////////
// FPoseSearchPoseMetadata
FArchive& operator<<(FArchive& Ar, FPoseSearchPoseMetadata& Metadata)
{
	Ar << Metadata.Flags;
	Ar << Metadata.CostAddend;
	Ar << Metadata.ContinuingPoseCostAddend;
	Ar << Metadata.AssetIndex;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndexAsset
FArchive& operator<<(FArchive& Ar, FPoseSearchIndexAsset& IndexAsset)
{
	Ar << IndexAsset.SourceAssetIdx;
	Ar << IndexAsset.bMirrored;
	Ar << IndexAsset.SamplingInterval;
	Ar << IndexAsset.PermutationIdx;
	Ar << IndexAsset.BlendParameters;
	Ar << IndexAsset.FirstPoseIdx;
	Ar << IndexAsset.NumPoses;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchStats
FArchive& operator<<(FArchive& Ar, FPoseSearchStats& Stats)
{
	Ar << Stats.AverageSpeed;
	Ar << Stats.MaxSpeed;
	Ar << Stats.AverageAcceleration;
	Ar << Stats.MaxAcceleration;
	return Ar;
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

bool FPoseSearchIndexBase::IsEmpty() const
{
	return Assets.IsEmpty() || PoseMetadata.IsEmpty();
}

void FPoseSearchIndexBase::Reset()
{
	*this = FPoseSearchIndexBase();
}

FArchive& operator<<(FArchive& Ar, FPoseSearchIndexBase& Index)
{
	Ar << Index.Values;
	Ar << Index.PoseMetadata;
	Ar << Index.OverallFlags;
	Ar << Index.Assets;
	Ar << Index.MinCostAddend;
	Ar << Index.Stats;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndex
FPoseSearchIndex::FPoseSearchIndex(const FPoseSearchIndex& Other)
	: FPoseSearchIndexBase(Other)
	, WeightsSqrt(Other.WeightsSqrt)
	, PCAValues(Other.PCAValues)
	, PCAProjectionMatrix(Other.PCAProjectionMatrix)
	, Mean(Other.Mean)
	, KDTree(Other.KDTree)
	, PCAExplainedVariance(Other.PCAExplainedVariance)
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
	check(!Values.IsEmpty() && PoseIdx >= 0 && PoseIdx < GetNumPoses() && SchemaCardinality > 0);
	const int32 ValueOffset = PoseIdx * SchemaCardinality;
	return MakeArrayView(&Values[ValueOffset], SchemaCardinality);
}

TConstArrayView<float> FPoseSearchIndex::GetReconstructedPoseValues(int32 PoseIdx, TArrayView<float> BufferUsedForReconstruction) const
{
	using namespace UE::PoseSearch;

	const int32 NumDimensions = WeightsSqrt.Num();
	const int32 NumPoses = GetNumPoses();
	check(PoseIdx >= 0 && PoseIdx < NumPoses&& NumDimensions > 0);
	check(BufferUsedForReconstruction.Num() == NumDimensions);

	const int32 NumberOfPrincipalComponents = PCAValues.Num() / NumPoses;
	check(NumPoses * NumberOfPrincipalComponents == PCAValues.Num());

	const RowMajorVectorMapConst MapWeightsSqrt(WeightsSqrt.GetData(), 1, NumDimensions);
	const ColMajorMatrixMapConst MapPCAProjectionMatrix(PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);
	const RowMajorVectorMapConst MapMean(Mean.GetData(), 1, NumDimensions);
	const RowMajorMatrixMapConst MapPCAValues(PCAValues.GetData(), NumPoses, NumberOfPrincipalComponents);

	const RowMajorVector ReciprocalWeightsSqrt = MapWeightsSqrt.cwiseInverse();
	const RowMajorVector WeightedReconstructedValues = MapPCAValues.row(PoseIdx) * MapPCAProjectionMatrix.transpose() + MapMean;

	RowMajorVectorMap ReconstructedPoseValues(BufferUsedForReconstruction.GetData(), 1, NumDimensions);
	ReconstructedPoseValues = WeightedReconstructedValues.array() * ReciprocalWeightsSqrt.array();

	return BufferUsedForReconstruction;
}

#if WITH_EDITOR || ENABLE_DRAW_DEBUG
TArray<float> FPoseSearchIndex::GetPoseValuesSafe(int32 PoseIdx) const
{
	TArray<float> PoseValues;
	if (PoseIdx >= 0 && PoseIdx < GetNumPoses())
	{
		if (Values.IsEmpty())
		{
			const int32 NumDimensions = WeightsSqrt.Num();
			PoseValues.SetNumUninitialized(NumDimensions);
			GetReconstructedPoseValues(PoseIdx, PoseValues);
		}
		else
		{
			PoseValues = GetPoseValues(PoseIdx);
		}
	}
	return PoseValues;
}
#endif // WITH_EDITOR || ENABLE_DRAW_DEBUG

FPoseSearchCost FPoseSearchIndex::ComparePoses(int32 PoseIdx, EPoseSearchBooleanRequest QueryMirrorRequest, UE::PoseSearch::EPoseComparisonFlags PoseComparisonFlags, float MirrorMismatchCostBias, TConstArrayView<float> PoseValues, TConstArrayView<float> QueryValues) const
{
	// base dissimilarity cost representing how the associated PoseIdx differ, in a weighted way, from the query pose (QueryValues)
	const float DissimilarityCost = UE::PoseSearch::CompareFeatureVectors(PoseValues, QueryValues, WeightsSqrt);

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

	Ar << Index.WeightsSqrt;
	Ar << Index.PCAValues;
	Ar << Index.PCAProjectionMatrix;
	Ar << Index.Mean;
	Ar << Index.PCAExplainedVariance;

	Serialize(Ar, Index.KDTree, Index.PCAValues.GetData());
	return Ar;
}