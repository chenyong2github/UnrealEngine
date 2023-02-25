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
	check(PoseIdx >= 0 && PoseIdx < GetNumPoses() && SchemaCardinality > 0);
	const int32 ValueOffset = PoseIdx * SchemaCardinality;
	return MakeArrayView(&Values[ValueOffset], SchemaCardinality);
}

TConstArrayView<float> FPoseSearchIndex::GetPoseValuesSafe(int32 PoseIdx) const
{
	if (PoseIdx >= 0 && PoseIdx < GetNumPoses())
	{
		return GetPoseValues(PoseIdx);
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

	Ar << Index.WeightsSqrt;
	Ar << Index.PCAValues;
	Ar << Index.PCAProjectionMatrix;
	Ar << Index.Mean;
	Ar << Index.PCAExplainedVariance;

	Serialize(Ar, Index.KDTree, Index.PCAValues.GetData());
	return Ar;
}