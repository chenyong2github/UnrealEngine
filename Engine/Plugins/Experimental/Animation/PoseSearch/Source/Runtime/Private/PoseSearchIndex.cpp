// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearchEigenHelper.h"

namespace UE::PoseSearch
{

static FORCEINLINE float CompareFeatureVectors(TConstArrayView<float> A, TConstArrayView<float> B, TConstArrayView<float> WeightsSqrt)
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

//////////////////////////////////////////////////////////////////////////
// FPoseMetadata
FArchive& operator<<(FArchive& Ar, FPoseMetadata& Metadata)
{
	Ar << Metadata.Data;
	Ar << Metadata.CostAddend;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FSearchIndexAsset
FArchive& operator<<(FArchive& Ar, FSearchIndexAsset& IndexAsset)
{
	Ar << IndexAsset.SourceAssetIdx;
	Ar << IndexAsset.bMirrored;
	Ar << IndexAsset.PermutationIdx;
	Ar << IndexAsset.BlendParameters;
	Ar << IndexAsset.FirstPoseIdx;
	Ar << IndexAsset.FirstSampleIdx;
	Ar << IndexAsset.LastSampleIdx;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FSearchStats
FArchive& operator<<(FArchive& Ar, FSearchStats& Stats)
{
	Ar << Stats.AverageSpeed;
	Ar << Stats.MaxSpeed;
	Ar << Stats.AverageAcceleration;
	Ar << Stats.MaxAcceleration;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchBaseIndex
const FSearchIndexAsset& FSearchIndexBase::GetAssetForPose(int32 PoseIdx) const
{
	const uint32 AssetIndex = PoseMetadata[PoseIdx].GetAssetIndex();
	return Assets[AssetIndex];
}

const FSearchIndexAsset* FSearchIndexBase::GetAssetForPoseSafe(int32 PoseIdx) const
{
	if (PoseMetadata.IsValidIndex(PoseIdx))
	{
		const uint32 AssetIndex = PoseMetadata[PoseIdx].GetAssetIndex();
		if (Assets.IsValidIndex(AssetIndex))
		{
			return &Assets[AssetIndex];
		}
	}
	return nullptr;
}

bool FSearchIndexBase::IsEmpty() const
{
	return Assets.IsEmpty() || PoseMetadata.IsEmpty();
}

void FSearchIndexBase::Reset()
{
	*this = FSearchIndexBase();
}

FArchive& operator<<(FArchive& Ar, FSearchIndexBase& Index)
{
	Ar << Index.Values;
	Ar << Index.PoseMetadata;
	Ar << Index.bAnyBlockTransition;
	Ar << Index.Assets;
	Ar << Index.MinCostAddend;
	Ar << Index.Stats;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
// FSearchIndex
FSearchIndex::FSearchIndex(const FSearchIndex& Other)
	: FSearchIndexBase(Other)
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

FSearchIndex& FSearchIndex::operator=(const FSearchIndex& Other)
{
	if (this != &Other)
	{
		this->~FSearchIndex();
		new(this)FSearchIndex(Other);
	}
	return *this;
}

void FSearchIndex::Reset()
{
	FSearchIndex Default;
	*this = Default;
}

TConstArrayView<float> FSearchIndex::GetPoseValues(int32 PoseIdx) const
{
	const int32 SchemaCardinality = WeightsSqrt.Num();
	check(!Values.IsEmpty() && PoseIdx >= 0 && PoseIdx < GetNumPoses() && SchemaCardinality > 0);
	const int32 ValueOffset = PoseIdx * SchemaCardinality;
	return MakeArrayView(&Values[ValueOffset], SchemaCardinality);
}

TConstArrayView<float> FSearchIndex::GetReconstructedPoseValues(int32 PoseIdx, TArrayView<float> BufferUsedForReconstruction) const
{
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

TArray<float> FSearchIndex::GetPoseValuesSafe(int32 PoseIdx) const
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

FPoseSearchCost FSearchIndex::ComparePoses(int32 PoseIdx, float ContinuingPoseCostBias, TConstArrayView<float> PoseValues, TConstArrayView<float> QueryValues) const
{
	// base dissimilarity cost representing how the associated PoseIdx differ, in a weighted way, from the query pose (QueryValues)
	const float DissimilarityCost = CompareFeatureVectors(PoseValues, QueryValues, WeightsSqrt);

	// cost addend associated to Schema->BaseCostBias or overriden by UAnimNotifyState_PoseSearchModifyCost
	const float NotifyAddend = PoseMetadata[PoseIdx].GetCostAddend();
	return FPoseSearchCost(DissimilarityCost, NotifyAddend, ContinuingPoseCostBias);
}

FArchive& operator<<(FArchive& Ar, FSearchIndex& Index)
{
	Ar << static_cast<FSearchIndexBase&>(Index);

	Ar << Index.WeightsSqrt;
	Ar << Index.PCAValues;
	Ar << Index.PCAProjectionMatrix;
	Ar << Index.Mean;
	Ar << Index.PCAExplainedVariance;

	Serialize(Ar, Index.KDTree, Index.PCAValues.GetData());
	return Ar;
}

} // namespace UE::PoseSearch
