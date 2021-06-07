// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearch.h"
#include "PoseSearchEigenHelper.h"

#include "Async/ParallelFor.h"
#include "Templates/IdentityFunctor.h"
#include "Templates/Invoke.h"
#include "Templates/Less.h"
#include "Features/IModularFeatures.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimPoseSearchProvider.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "UObject/ObjectSaveContext.h"

IMPLEMENT_ANIMGRAPH_MESSAGE(UE::PoseSearch::IPoseHistoryProvider);

namespace UE { namespace PoseSearch {

DEFINE_LOG_CATEGORY(LogPoseSearch);

//////////////////////////////////////////////////////////////////////////
// Constants and utilities

constexpr float DrawDebugLineThickness = 2.0f;
constexpr float DrawDebugPointSize = 3.0f;
constexpr float DrawDebugVelocityScale = 0.1f;
constexpr float DrawDebugArrowSize = 5.0f;
constexpr float DrawDebugSphereSize = 3.0f;
constexpr int32 DrawDebugSphereSegments = 8;
constexpr float DrawDebugSphereLineThickness = 0.5f;

static bool IsSamplingRangeValid(FFloatInterval Range)
{
	return Range.IsValid() && (Range.Min >= 0.0f);
}

static FFloatInterval GetEffectiveSamplingRange(const UAnimSequenceBase* Sequence, FFloatInterval SamplingRange)
{
	const bool bSampleAll = (SamplingRange.Min == 0.0f) && (SamplingRange.Max == 0.0f);
	const float SequencePlayLength = Sequence->GetPlayLength();

	FFloatInterval Range;
	Range.Min = bSampleAll ? 0.0f : SamplingRange.Min;
	Range.Max = bSampleAll ? SequencePlayLength : FMath::Min(SequencePlayLength, SamplingRange.Max);
	return Range;
}

static inline float CompareFeatureVectors(int32 NumValues, const float* A, const float* B, const float* Weights)
{
	float Dissimilarity = 0.f;

	for (int32 ValueIdx = 0; ValueIdx != NumValues; ++ValueIdx)
	{
		const float Diff = Weights[ValueIdx] * (A[ValueIdx] - B[ValueIdx]);
		Dissimilarity += Diff * Diff;
	}

	return Dissimilarity;
}

FLinearColor GetColorForFeature(FPoseSearchFeatureDesc Feature, const FPoseSearchFeatureVectorLayout* Layout)
{
	int32 FeatureIdx = Layout->Features.IndexOfByKey(Feature);
	check(FeatureIdx != INDEX_NONE);
	float Lerp = (float)(FeatureIdx) / (Layout->Features.Num() - 1);
	FLinearColor ColorHSV(Lerp * 360.0f, 0.8f, 0.5f, 1.0f);
	return ColorHSV.HSVToLinearRGB();
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

template <typename IteratorType, typename ValueType, typename SortPredicateType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value, SortPredicateType SortPredicate) -> decltype(First.GetIndex())
{
	return LowerBound(First, Last, Value, FIdentityFunctor(), SortPredicate);
}

template <typename IteratorType, typename ValueType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value) -> decltype(First.GetIndex())
{
	return LowerBound(First, Last, Value, FIdentityFunctor(), TLess<>());
}


//////////////////////////////////////////////////////////////////////////
// FFeatureTypeTraits

struct FFeatureTypeTraits
{
	EPoseSearchFeatureType Type = EPoseSearchFeatureType::Invalid;
	uint32 NumFloats = 0;
};

// Could upgrade to class objects in the future with value reader/writer functions
static constexpr FFeatureTypeTraits FeatureTypeTraits[] =
{
	{ EPoseSearchFeatureType::Position, 3 },
	{ EPoseSearchFeatureType::Rotation, 6 },
	{ EPoseSearchFeatureType::LinearVelocity, 3 },
	{ EPoseSearchFeatureType::AngularVelocity, 3 },
};

FFeatureTypeTraits GetFeatureTypeTraits(EPoseSearchFeatureType Type)
{
	// Could allow external registration to a TSet of traits in the future
	// For now just use a simple local array
	for (const FFeatureTypeTraits& Traits : FeatureTypeTraits)
	{
		if (Traits.Type == Type)
		{
			return Traits;
		}
	}

	return FFeatureTypeTraits();
}

}} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureDesc

bool FPoseSearchFeatureDesc::operator==(const FPoseSearchFeatureDesc& Other) const
{
	return
		(SchemaBoneIdx == Other.SchemaBoneIdx) &&
		(SubsampleIdx == Other.SubsampleIdx) &&
		(Type == Other.Type) &&
		(Domain == Other.Domain);
}

bool FPoseSearchFeatureDesc::IsSubsampleOfSameFeature(const FPoseSearchFeatureDesc& Other) const
{
	return
		(SchemaBoneIdx == Other.SchemaBoneIdx) &&
		(Type == Other.Type) &&
		(Domain == Other.Domain);
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorLayout

void FPoseSearchFeatureVectorLayout::Init()
{
	uint32 FloatCount = 0;

	for (FPoseSearchFeatureDesc& Feature : Features)
	{
		Feature.ValueOffset = FloatCount;

		uint32 FeatureNumFloats = UE::PoseSearch::GetFeatureTypeTraits(Feature.Type).NumFloats;
		FloatCount += FeatureNumFloats;

		if (Feature.SchemaBoneIdx == FPoseSearchFeatureDesc::TrajectoryBoneIndex)
		{
			if (FirstTrajectoryValueOffset == -1)
			{
				FirstTrajectoryValueOffset = Feature.ValueOffset;
			}

			NumTrajectoryValues += FeatureNumFloats;
		}
		else
		{
			if (FirstPoseValueOffset == -1)
			{
				FirstPoseValueOffset = Feature.ValueOffset;
			}

			NumPoseValues += FeatureNumFloats;
		}
	}

	NumFloats = FloatCount;
}

void FPoseSearchFeatureVectorLayout::Reset()
{
	Features.Reset();
	NumFloats = 0;
	NumTrajectoryValues = 0;
	NumPoseValues = 0;
	FirstTrajectoryValueOffset = -1;
	FirstPoseValueOffset = -1;
}

bool FPoseSearchFeatureVectorLayout::IsValid(int32 MaxNumBones) const
{
	if (NumFloats == 0)
	{
		return false;
	}

	for (const FPoseSearchFeatureDesc& Feature : Features)
	{
		if (Feature.SchemaBoneIdx >= MaxNumBones)
		{
			return false;
		}
	}

	return true;
}

bool FPoseSearchFeatureVectorLayout::EnumerateFeature(EPoseSearchFeatureType FeatureType, bool bTrajectory, int32& OutFeatureIdx) const
{
	// This function behaves similar to a generator
	// OutFeatureIdx represents the 'next' Pose Feature Description that matches the inner criteria
	// OutFeatureIdx can then be used again as a starting index to begin a subsequent search
	for (int32 Idx = OutFeatureIdx + 1, Size = Features.Num(); Idx < Size; ++Idx)
	{
		// A trajectory feature match will result when bTrajectory = True and SchemaBoneIdx = -1
		// A pose feature match will result when bTrajectory = False and SchemaBoneIdx != -1
		if (Features[Idx].Type == FeatureType && (bTrajectory == (Features[Idx].SchemaBoneIdx == FPoseSearchFeatureDesc::TrajectoryBoneIndex)))
		{
			OutFeatureIdx = Idx;
			return true;
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// UPoseSearchSchema

void UPoseSearchSchema::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SampleRate = FMath::Clamp(SampleRate, 1, 60);
	SamplingInterval = 1.0f / SampleRate;

	PoseSampleTimes.Sort(TLess<>());
	TrajectorySampleTimes.Sort(TLess<>());
	TrajectorySampleDistances.Sort(TLess<>());

	GenerateLayout();
	ResolveBoneReferences();

	EffectiveDataPreprocessor = DataPreprocessor;
	if (EffectiveDataPreprocessor == EPoseSearchDataPreprocessor::Automatic)
	{
		EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Normalize;
	}

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchSchema::PostLoad()
{
	Super::PostLoad();

	ResolveBoneReferences();
}

bool UPoseSearchSchema::IsValid() const
{
	bool bValid = Skeleton != nullptr;

	for (const FBoneReference& BoneRef : Bones)
	{
		bValid &= BoneRef.HasValidSetup();
	}

	bValid &= (Bones.Num() == BoneIndices.Num());

	bValid &= Layout.IsValid(BoneIndices.Num());
	
	return bValid;
}

void UPoseSearchSchema::GenerateLayout()
{
	Layout.Reset();

	// Time domain trajectory positions
	if (bUseTrajectoryPositions && TrajectorySampleTimes.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
		Feature.Domain = EPoseSearchFeatureDomain::Time;
		Feature.Type = EPoseSearchFeatureType::Position;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != TrajectorySampleTimes.Num(); ++Feature.SubsampleIdx)
		{
			Layout.Features.Add(Feature);
		}
	}

	// Time domain trajectory linear velocities
	if (bUseTrajectoryVelocities && TrajectorySampleTimes.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
		Feature.Domain = EPoseSearchFeatureDomain::Time;
		Feature.Type = EPoseSearchFeatureType::LinearVelocity;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != TrajectorySampleTimes.Num(); ++Feature.SubsampleIdx)
		{
			Layout.Features.Add(Feature);
		}
	}

	// Distance domain trajectory positions
	if (bUseTrajectoryPositions && TrajectorySampleDistances.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
		Feature.Domain = EPoseSearchFeatureDomain::Distance;
		Feature.Type = EPoseSearchFeatureType::Position;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != TrajectorySampleDistances.Num(); ++Feature.SubsampleIdx)
		{
			Layout.Features.Add(Feature);
		}
	}

	// Distance domain trajectory linear velocities
	if (bUseTrajectoryVelocities && TrajectorySampleDistances.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
		Feature.Domain = EPoseSearchFeatureDomain::Distance;
		Feature.Type = EPoseSearchFeatureType::LinearVelocity;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != TrajectorySampleDistances.Num(); ++Feature.SubsampleIdx)
		{
			Layout.Features.Add(Feature);
		}
	}

	// Time domain bone positions
	if (bUseBonePositions && PoseSampleTimes.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.Domain = EPoseSearchFeatureDomain::Time;
		Feature.Type = EPoseSearchFeatureType::Position;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != PoseSampleTimes.Num(); ++Feature.SubsampleIdx)
		{
			for (Feature.SchemaBoneIdx = 0; Feature.SchemaBoneIdx != Bones.Num(); ++Feature.SchemaBoneIdx)
			{
				Layout.Features.Add(Feature);
			}
		}
	}

	// Time domain bone linear velocities
	if (bUseBoneVelocities && PoseSampleTimes.Num())
	{
		FPoseSearchFeatureDesc Feature;
		Feature.Domain = EPoseSearchFeatureDomain::Time;
		Feature.Type = EPoseSearchFeatureType::LinearVelocity;
		for (Feature.SubsampleIdx = 0; Feature.SubsampleIdx != PoseSampleTimes.Num(); ++Feature.SubsampleIdx)
		{
			for (Feature.SchemaBoneIdx = 0; Feature.SchemaBoneIdx != Bones.Num(); ++Feature.SchemaBoneIdx)
			{
				Layout.Features.Add(Feature);
			}
		}
	}

	Layout.Init();
}

void UPoseSearchSchema::ResolveBoneReferences()
{
	// Initialize references to obtain bone indices
	for (FBoneReference& BoneRef : Bones)
	{
		BoneRef.Initialize(Skeleton);
	}

	// Fill out bone index array and sort by bone index
	BoneIndices.SetNum(Bones.Num());
	for (int32 Index = 0; Index != Bones.Num(); ++Index)
	{
		BoneIndices[Index] = Bones[Index].BoneIndex;
	}
	BoneIndices.Sort();

	// Build separate index array with parent indices guaranteed to be present
	BoneIndicesWithParents = BoneIndices;
	if (Skeleton)
	{
		FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, Skeleton->GetReferenceSkeleton());
	}
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchBiasWeights

void FPoseSearchBiasWeights::Init(const FPoseSearchBiasWeightParams& WeightParams, const FPoseSearchFeatureVectorLayout& Layout)
{
	// Initialize all weights to a default value of 1, and subsequently override all bound weights to their assigned value
	Weights.Init(1.f, Layout.NumFloats);
	BindSemanticWeight(WeightParams.TrajectoryPositionWeight, Layout, EPoseSearchFeatureType::Position, true);
	BindSemanticWeight(WeightParams.TrajectoryLinearVelocityWeight, Layout, EPoseSearchFeatureType::LinearVelocity, true);
	BindSemanticWeight(WeightParams.PosePositionWeight, Layout, EPoseSearchFeatureType::Position, false);
	BindSemanticWeight(WeightParams.PoseLinearVelocityWeight, Layout, EPoseSearchFeatureType::LinearVelocity, false);
}

void FPoseSearchBiasWeights::BindSemanticWeight(float Weight, const FPoseSearchFeatureVectorLayout& Layout, EPoseSearchFeatureType FeatureType, bool bTrajectory)
{
	// The Weight parameter will be bound to a specific feature described by the FPoseSearchFeatureVectorLayout
	int32 FeatureIdx = INDEX_NONE;
	while (Layout.EnumerateFeature(FeatureType, bTrajectory, FeatureIdx))
	{
		const FPoseSearchFeatureDesc& Feature = Layout.Features[FeatureIdx];
		const int32 FirstValueIdx = Feature.ValueOffset;
		const int32 NumValues = UE::PoseSearch::GetFeatureTypeTraits(FeatureType).NumFloats;

		for (int32 Idx = 0; Idx < NumValues; ++Idx)
		{
			Weights[FirstValueIdx + Idx] = Weight;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndex

bool FPoseSearchIndex::IsValid() const
{
	bool bSchemaValid = Schema && Schema->IsValid();
	bool bSearchIndexValid = bSchemaValid && (NumPoses * Schema->Layout.NumFloats == Values.Num());

	return bSearchIndexValid;
}

TArrayView<const float> FPoseSearchIndex::GetPoseValues(int32 PoseIdx) const
{
	check(PoseIdx < NumPoses);
	int32 ValueOffset = PoseIdx * Schema->Layout.NumFloats;
	return MakeArrayView(&Values[ValueOffset], Schema->Layout.NumFloats);
}

void FPoseSearchIndex::Reset()
{
	NumPoses = 0;
	Values.Reset();
	Schema = nullptr;
}

void FPoseSearchIndex::Normalize(TArrayView<float> InOutPoseVector) const
{
	using namespace Eigen;

	auto TransformationMtx = Map<const Matrix<float, Dynamic, Dynamic, ColMajor>>
	(
		PreprocessInfo.TransformationMatrix.GetData(),
		PreprocessInfo.NumDimensions,
		PreprocessInfo.NumDimensions
	);
	auto SampleMean = Map<const Matrix<float, Dynamic, 1, ColMajor>>
	(
		PreprocessInfo.SampleMean.GetData(),
		PreprocessInfo.NumDimensions
	);

	checkSlow(InOutPoseVector.Num() == PreprocessInfo.NumDimensions);

	auto PoseVector = Map<Matrix<float, Dynamic, 1, ColMajor>>
	(
		InOutPoseVector.GetData(),
		InOutPoseVector.Num()
	);

	PoseVector = TransformationMtx * (PoseVector - SampleMean);
}

void FPoseSearchIndex::InverseNormalize(TArrayView<float> InOutNormalizedPoseVector) const
{
	using namespace Eigen;

	auto InverseTransformationMtx = Map<const Matrix<float, Dynamic, Dynamic, ColMajor>>
	(
		PreprocessInfo.InverseTransformationMatrix.GetData(),
		PreprocessInfo.NumDimensions,
		PreprocessInfo.NumDimensions
	);
	auto SampleMean = Map<const Matrix<float, Dynamic, 1, ColMajor>>
	(
		PreprocessInfo.SampleMean.GetData(),
		PreprocessInfo.NumDimensions
	);

	checkSlow(InOutNormalizedPoseVector.Num() == PreprocessInfo.NumDimensions);

	auto NormalizedPoseVector = Map<Matrix<float, Dynamic, 1, ColMajor>>
	(
		InOutNormalizedPoseVector.GetData(),
		InOutNormalizedPoseVector.Num()
	);

	NormalizedPoseVector = (InverseTransformationMtx * NormalizedPoseVector) + SampleMean;
}


//////////////////////////////////////////////////////////////////////////
// UPoseSearchSequenceMetaData

void UPoseSearchSequenceMetaData::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SearchIndex.Reset();

#if WITH_EDITOR
	if (!IsTemplate())
	{
		if (IsValidForIndexing())
		{
			UObject* Outer = GetOuter();
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Outer))
			{
				UE::PoseSearch::BuildIndex(Sequence, this);
			}
		}
	}
#endif

	Super::PreSave(ObjectSaveContext);
}

bool UPoseSearchSequenceMetaData::IsValidForIndexing() const
{
	return Schema && Schema->IsValid() && UE::PoseSearch::IsSamplingRangeValid(SamplingRange);
}

bool UPoseSearchSequenceMetaData::IsValidForSearch() const
{
	return IsValidForIndexing() && SearchIndex.IsValid();
}


//////////////////////////////////////////////////////////////////////////
// UPoseSearchDatabase

int32 UPoseSearchDatabase::FindSequenceForPose(int32 PoseIdx) const
{
	auto Predicate = [PoseIdx](const FPoseSearchDatabaseSequence& DbSequence)
	{
		return (PoseIdx >= DbSequence.FirstPoseIdx) && (PoseIdx < DbSequence.FirstPoseIdx + DbSequence.NumPoses);
	};

	return Sequences.IndexOfByPredicate(Predicate);
}

int32 UPoseSearchDatabase::GetPoseIndexFromAssetTime(int32 DbSequenceIdx, float AssetTime) const
{
	const FPoseSearchDatabaseSequence& DbSequence = Sequences[DbSequenceIdx];
	FFloatInterval Range = UE::PoseSearch::GetEffectiveSamplingRange(DbSequence.Sequence, DbSequence.SamplingRange);
	if (Range.Contains(AssetTime))
	{
		int32 PoseOffset = FMath::RoundToInt(Schema->SampleRate * (AssetTime - Range.Min));		
		if (PoseOffset >= DbSequence.NumPoses)
		{
			if (DbSequence.bLoopAnimation)
			{
				PoseOffset -= DbSequence.NumPoses;
			}
			else
			{
				PoseOffset = DbSequence.NumPoses - 1;
			}
		}
		
		int32 PoseIdx = DbSequence.FirstPoseIdx + PoseOffset;
		return PoseIdx;
	}
	
	return INDEX_NONE;
}

bool UPoseSearchDatabase::IsValidForIndexing() const
{
	bool bValid = Schema && Schema->IsValid() && !Sequences.IsEmpty();

	if (bValid)
	{
		bool bSequencesValid = true;
		for (const FPoseSearchDatabaseSequence& DbSequence : Sequences)
		{
			if (!DbSequence.Sequence)
			{
				bSequencesValid = false;
				break;
			}

			USkeleton* SeqSkeleton = DbSequence.Sequence->GetSkeleton();
			if (!SeqSkeleton || !SeqSkeleton->IsCompatible(Schema->Skeleton))
			{
				bSequencesValid = false;
				break;
			}
		}

		bValid = bSequencesValid;
	}

	return bValid;
}

bool UPoseSearchDatabase::IsValidForSearch() const
{
	return IsValidForIndexing() && SearchIndex.IsValid();
}

void UPoseSearchDatabase::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SearchIndex.Reset();

#if WITH_EDITOR
	if (!IsTemplate())
	{
		if (IsValidForIndexing())
		{
			UE::PoseSearch::BuildIndex(this);
		}
	}
#endif

	Super::PreSave(ObjectSaveContext);
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorBuilder

void FPoseSearchFeatureVectorBuilder::Init(const UPoseSearchSchema* InSchema)
{
	check(InSchema && InSchema->IsValid());
	Schema = InSchema;
	ResetFeatures();
}

void FPoseSearchFeatureVectorBuilder::ResetFeatures()
{
	Values.Reset(0);
	Values.SetNumZeroed(Schema->Layout.NumFloats);
	ValuesNormalized.Reset(0);
	ValuesNormalized.SetNumZeroed(Schema->Layout.NumFloats);
	NumFeaturesAdded = 0;
	FeaturesAdded.Init(false, Schema->Layout.Features.Num());
}

void FPoseSearchFeatureVectorBuilder::SetTransform(FPoseSearchFeatureDesc Element, const FTransform& Transform)
{
	SetPosition(Element, Transform.GetTranslation());
	SetRotation(Element, Transform.GetRotation());
}

void FPoseSearchFeatureVectorBuilder::SetTransformVelocity(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	SetLinearVelocity(Element, Transform, PrevTransform, DeltaTime);
	SetAngularVelocity(Element, Transform, PrevTransform, DeltaTime);
}

void FPoseSearchFeatureVectorBuilder::SetPosition(FPoseSearchFeatureDesc Element, const FVector& Position)
{
	Element.Type = EPoseSearchFeatureType::Position;
	SetVector(Element, Position);
}

void FPoseSearchFeatureVectorBuilder::SetRotation(FPoseSearchFeatureDesc Element, const FQuat& Rotation)
{
	Element.Type = EPoseSearchFeatureType::Rotation;
	int32 ElementIndex = Schema->Layout.Features.Find(Element);
	if (ElementIndex >= 0)
	{
		FVector X = Rotation.GetAxisX();
		FVector Y = Rotation.GetAxisY();

		const FPoseSearchFeatureDesc& FoundElement = Schema->Layout.Features[ElementIndex];

		Values[FoundElement.ValueOffset + 0] = X.X;
		Values[FoundElement.ValueOffset + 1] = X.Y;
		Values[FoundElement.ValueOffset + 2] = X.Z;
		Values[FoundElement.ValueOffset + 3] = Y.X;
		Values[FoundElement.ValueOffset + 4] = Y.Y;
		Values[FoundElement.ValueOffset + 5] = Y.Z;

		if (!FeaturesAdded[ElementIndex])
		{
			FeaturesAdded[ElementIndex] = true;
			++NumFeaturesAdded;
		}
	}
}

void FPoseSearchFeatureVectorBuilder::SetLinearVelocity(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Element.Type = EPoseSearchFeatureType::LinearVelocity;
	FVector LinearVelocity = (Transform.GetTranslation() - PrevTransform.GetTranslation()) / DeltaTime;
	SetVector(Element, LinearVelocity);
}

void FPoseSearchFeatureVectorBuilder::SetAngularVelocity(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Element.Type = EPoseSearchFeatureType::AngularVelocity;
	int32 ElementIndex = Schema->Layout.Features.Find(Element);
	if (ElementIndex >= 0)
	{
		FQuat Q0 = PrevTransform.GetRotation();
		FQuat Q1 = Transform.GetRotation();
		Q1.EnforceShortestArcWith(Q0);

		// Given angular velocity vector w, quaternion differentiation can be represented as
		//   dq/dt = (w * q)/2
		// Solve for w
		//   w = 2 * dq/dt * q^-1
		// And let dq/dt be expressed as the finite difference
		//   dq/dt = (q(t+h) - q(t)) / h
		FQuat DQDt = (Q1 - Q0) / DeltaTime;
		FQuat QInv = Q0.Inverse();
		FQuat W = (DQDt * QInv) * 2.0f;

		FVector AngularVelocity(W.X, W.Y, W.Z);

		const FPoseSearchFeatureDesc& FoundElement = Schema->Layout.Features[ElementIndex];

		Values[FoundElement.ValueOffset + 0] = AngularVelocity[0];
		Values[FoundElement.ValueOffset + 1] = AngularVelocity[1];
		Values[FoundElement.ValueOffset + 2] = AngularVelocity[2];

		if (!FeaturesAdded[ElementIndex])
		{
			FeaturesAdded[ElementIndex] = true;
			++NumFeaturesAdded;
		}
	}
}

void FPoseSearchFeatureVectorBuilder::SetVector(FPoseSearchFeatureDesc Element, const FVector& Vector)
{
	int32 ElementIndex = Schema->Layout.Features.Find(Element);
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Schema->Layout.Features[ElementIndex];

		Values[FoundElement.ValueOffset + 0] = Vector[0];
		Values[FoundElement.ValueOffset + 1] = Vector[1];
		Values[FoundElement.ValueOffset + 2] = Vector[2];

		if (!FeaturesAdded[ElementIndex])
		{
			FeaturesAdded[ElementIndex] = true;
			++NumFeaturesAdded;
		}
	}
}

bool FPoseSearchFeatureVectorBuilder::TrySetPoseFeatures(UE::PoseSearch::FPoseHistory* History)
{
	check(Schema && Schema->IsValid());
	check(History);

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->PoseSampleTimes.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		// Stop when we've reached future samples
		float SampleTime = Schema->PoseSampleTimes[SchemaSubsampleIdx];
		if (SampleTime > 0.0f)
		{
			break;
		}

		float SecondsAgo = -SampleTime;
		if (!History->TrySamplePose(SecondsAgo, Schema->Skeleton->GetReferenceSkeleton(), Schema->BoneIndicesWithParents))
		{
			return false;
		}

		TArrayView<const FTransform> ComponentPose = History->GetComponentPoseSample();
		TArrayView<const FTransform> ComponentPrevPose = History->GetPrevComponentPoseSample();
		for (int32 SchemaBoneIdx = 0; SchemaBoneIdx != Schema->BoneIndices.Num(); ++SchemaBoneIdx)
		{
			Feature.SchemaBoneIdx = SchemaBoneIdx;

			int32 SkeletonBoneIndex = Schema->BoneIndices[SchemaBoneIdx];
			const FTransform& Transform = ComponentPose[SkeletonBoneIndex];
			const FTransform& PrevTransform = ComponentPrevPose[SkeletonBoneIndex];
			SetTransform(Feature, Transform);
			SetTransformVelocity(Feature, Transform, PrevTransform, History->GetSampleTimeInterval());
		}
	}

	return true;
}

bool FPoseSearchFeatureVectorBuilder::TrySetPastTrajectoryFeatures(UE::PoseSearch::FPoseHistory* History)
{
	check(Schema && Schema->IsValid());
	check(History);

	bool bSuccess = TrySetPastTrajectoryTimeFeatures(History);
	bSuccess &= TrySetPastTrajectoryDistanceFeatures(History);

	return bSuccess;
}

bool FPoseSearchFeatureVectorBuilder::TrySetPastTrajectoryTimeFeatures(UE::PoseSearch::FPoseHistory* History)
{
	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->TrajectorySampleTimes.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;
		
		// Stop when we've reached future samples
		float SampleTime = Schema->TrajectorySampleTimes[SchemaSubsampleIdx];
		if (SampleTime > 0.0f)
		{
			break;
		}

		float SecondsAgo = -SampleTime;
		FTransform WorldComponentTransform;
		if (!History->TrySampleRootTimeBased(SecondsAgo, &WorldComponentTransform))
		{
			return false;
		}

		FTransform WorldPrevComponentTransform;
		if (!History->TrySampleRootTimeBased(SecondsAgo + History->GetSampleTimeInterval(), &WorldPrevComponentTransform))
		{
			return false;
		}

		SetTransform(Feature, WorldComponentTransform);
		SetTransformVelocity(Feature, WorldComponentTransform, WorldPrevComponentTransform, History->GetSampleTimeInterval());
	}

	return true;
}

bool FPoseSearchFeatureVectorBuilder::TrySetPastTrajectoryDistanceFeatures(UE::PoseSearch::FPoseHistory* History)
{
	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Distance;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->TrajectorySampleDistances.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;
		
		// Stop when we've reached future samples
		float SampleDistance = Schema->TrajectorySampleDistances[SchemaSubsampleIdx];
		if (SampleDistance > 0.0f)
		{
			break;
		}

		float SampleTime = 0.0f;
		float PastDistance = -SampleDistance;
		FTransform WorldComponentTransform;
		if (!History->TrySampleRootDistanceBased(PastDistance, &WorldComponentTransform, &SampleTime))
		{
			return false;
		}

		FTransform WorldPrevComponentTransform;
		if (!History->TrySampleRootTimeBased(SampleTime + History->GetSampleTimeInterval(), &WorldPrevComponentTransform))
		{
			return false;
		}

		SetTransform(Feature, WorldComponentTransform);
		SetTransformVelocity(Feature, WorldComponentTransform, WorldPrevComponentTransform, History->GetSampleTimeInterval());
	}

	return true;
}

void FPoseSearchFeatureVectorBuilder::CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx)
{
	check(Schema == SearchIndex.Schema);

	TArrayView<const float> FeatureVector = SearchIndex.GetPoseValues(PoseIdx);

	ValuesNormalized = FeatureVector;
	Values = FeatureVector;
	SearchIndex.InverseNormalize(Values);

	NumFeaturesAdded = Schema->Layout.Features.Num();
	FeaturesAdded.SetRange(0, FeaturesAdded.Num(), true);
}

void FPoseSearchFeatureVectorBuilder::CopyFeature(const FPoseSearchFeatureVectorBuilder& OtherBuilder, int32 FeatureIdx)
{
	check(IsCompatible(OtherBuilder));
	check(OtherBuilder.FeaturesAdded[FeatureIdx]);

	const FPoseSearchFeatureDesc& FeatureDesc = Schema->Layout.Features[FeatureIdx];
	const int32 FeatureNumFloats = UE::PoseSearch::GetFeatureTypeTraits(FeatureDesc.Type).NumFloats;
	const int32 FeatureValueOffset = FeatureDesc.ValueOffset;

	for(int32 FeatureValueIdx = FeatureValueOffset; FeatureValueIdx != FeatureValueOffset + FeatureNumFloats; ++FeatureValueIdx)
	{
		Values[FeatureValueIdx] = OtherBuilder.Values[FeatureValueIdx];
	}

	if (!FeaturesAdded[FeatureIdx])
	{
		FeaturesAdded[FeatureIdx] = true;
		++NumFeaturesAdded;
	}
}

void FPoseSearchFeatureVectorBuilder::MergeReplace(const FPoseSearchFeatureVectorBuilder& OtherBuilder)
{
	check(IsCompatible(OtherBuilder));

	for (TConstSetBitIterator<> Iter(OtherBuilder.FeaturesAdded); Iter; ++Iter)
	{
		CopyFeature(OtherBuilder, Iter.GetIndex());
	}
}

bool FPoseSearchFeatureVectorBuilder::IsInitialized() const
{
	return (Schema != nullptr) && (Values.Num() == Schema->Layout.NumFloats);
}

bool FPoseSearchFeatureVectorBuilder::IsInitializedForSchema(const UPoseSearchSchema* InSchema) const
{
	return (Schema == InSchema) && IsInitialized();
}

bool FPoseSearchFeatureVectorBuilder::IsComplete() const
{
	return NumFeaturesAdded == Schema->Layout.Features.Num();
}

bool FPoseSearchFeatureVectorBuilder::IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const
{
	return IsInitialized() && (Schema == OtherBuilder.Schema);
}

void FPoseSearchFeatureVectorBuilder::Normalize(const FPoseSearchIndex& ForSearchIndex)
{
	ValuesNormalized = Values;
	ForSearchIndex.Normalize(ValuesNormalized);
}

namespace UE { namespace PoseSearch {

//////////////////////////////////////////////////////////////////////////
// FPoseHistory

/**
* Fills skeleton transforms with evaluated compact pose transforms.
* Bones that weren't evaluated are filled with the bone's reference pose.
*/
static void CopyCompactToSkeletonPose(const FCompactPose& Pose, TArray<FTransform>& OutLocalTransforms)
{
	const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
	TArrayView<const FTransform> RefSkeletonTransforms = MakeArrayView(RefSkeleton.GetRefBonePose());

	const int32 NumSkeletonBones = BoneContainer.GetNumBones();
	OutLocalTransforms.SetNum(NumSkeletonBones);

	for (auto SkeletonBoneIdx = FSkeletonPoseBoneIndex(0); SkeletonBoneIdx != NumSkeletonBones; ++SkeletonBoneIdx)
	{
		FCompactPoseBoneIndex CompactBoneIdx = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(SkeletonBoneIdx.GetInt());
		OutLocalTransforms[SkeletonBoneIdx.GetInt()] = CompactBoneIdx.IsValid() ? Pose[CompactBoneIdx] : RefSkeletonTransforms[SkeletonBoneIdx.GetInt()];
	}
}

void FPoseHistory::Init(int32 InNumPoses, float InTimeHorizon)
{
	Poses.Reserve(InNumPoses);
	Knots.Reserve(InNumPoses);
	TimeHorizon = InTimeHorizon;
}

void FPoseHistory::Init(const FPoseHistory& History)
{
	Poses = History.Poses;
	Knots = History.Knots;
	TimeHorizon = History.TimeHorizon;
}

bool FPoseHistory::TrySampleLocalPose(float SecondsAgo, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose)
{
	int32 NextIdx = LowerBound(Knots.begin(), Knots.end(), SecondsAgo, TGreater<>());
	if (NextIdx <= 0 || NextIdx >= Knots.Num())
	{
		return false;
	}

	int32 PrevIdx = NextIdx - 1;

	const FPose& PrevPose = Poses[PrevIdx];
	const FPose& NextPose = Poses[NextIdx];

	// Compute alpha between previous and next knots
	float Alpha = FMath::GetMappedRangeValueUnclamped(
		FVector2D(Knots[PrevIdx], Knots[NextIdx]),
		FVector2D(0.0f, 1.0f),
		SecondsAgo);

	// We may not have accumulated enough poses yet
	if (PrevPose.LocalTransforms.Num() != NextPose.LocalTransforms.Num())
	{
		return false;
	}

	if (RequiredBones.Num() > PrevPose.LocalTransforms.Num())
	{
		return false;
	}

	// Lerp between poses by alpha to produce output local pose at requested sample time
	LocalPose = PrevPose.LocalTransforms;
	FAnimationRuntime::LerpBoneTransforms(
		LocalPose,
		NextPose.LocalTransforms,
		Alpha,
		RequiredBones);

	return true;
}

bool FPoseHistory::TrySamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones)
{
	// Compute local space pose at requested time
	bool bSampled = TrySampleLocalPose(SecondsAgo, RequiredBones, SampledLocalPose);

	// Compute local space pose one sample interval in the past
	bSampled = bSampled && TrySampleLocalPose(SecondsAgo + GetSampleTimeInterval(), RequiredBones, SampledPrevLocalPose);

	// Convert local to component space
	if (bSampled)
	{
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledLocalPose, SampledComponentPose);
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledPrevLocalPose, SampledPrevComponentPose);
	}

	return bSampled;
}

bool FPoseHistory::TrySampleRootTimeBased(float SecondsAgo, FTransform* OutTransform) const
{
	int32 NextIdx = LowerBound(Knots.begin(), Knots.end(), SecondsAgo, TGreater<>());
	if (NextIdx <= 0 || NextIdx >= Knots.Num())
	{
		return false;
	}

	int32 PrevIdx = NextIdx - 1;

	const FPose& PrevPose = Poses[PrevIdx];
	const FPose& NextPose = Poses[NextIdx];

	// Compute alpha between previous and next knots
	float Alpha = FMath::GetMappedRangeValueUnclamped(
		FVector2D(Knots[PrevIdx], Knots[NextIdx]),
		FVector2D(0.0f, 1.0f),
		SecondsAgo);

	FTransform RootTransform;
	RootTransform.Blend(PrevPose.WorldComponentTransform, NextPose.WorldComponentTransform, Alpha);
	RootTransform.SetToRelativeTransform(Poses.Last().WorldComponentTransform);

	*OutTransform = RootTransform;
	return true;
}

bool FPoseHistory::TrySampleRootDistanceBased(float SampleDistance, FTransform* OutTransform, float* OutSampleTime) const
{
	float SampleTime = 0.0f;

	// Find the relevant distance samples and convert to time
	double NextDistance = 0.0;
	double PrevDistance = 0.0;
	for (int32 NextIdx = Poses.Num() - 2; NextIdx >= 0; --NextIdx)
	{
		FTransform PrevTransform = Poses[NextIdx+1].WorldComponentTransform;
		FTransform NextTransform = Poses[NextIdx].WorldComponentTransform;

		NextDistance += FVector::Distance(PrevTransform.GetTranslation(), NextTransform.GetTranslation());
		if (NextDistance >= SampleDistance)
		{
			float DistanceSampleAlpha = (float)FMath::GetRangePct(PrevDistance, NextDistance, (double)SampleDistance);
			SampleTime = FMath::Lerp(Knots[NextIdx+1], Knots[NextIdx], DistanceSampleAlpha);
			break;
		}

		PrevDistance = NextDistance;
	}

	bool bResult = TrySampleRootTimeBased(SampleTime, OutTransform);
	*OutSampleTime = SampleTime;
	return bResult;
}

void FPoseHistory::Update(float SecondsElapsed, const FPoseContext& PoseContext)
{
	// Age our elapsed times
	for (float& Knot : Knots)
	{
		Knot += SecondsElapsed;
	}

	if (Knots.Num() != Knots.Max())
	{
		// Consume every pose until the queue is full
		Knots.AddUninitialized();
		Poses.Emplace();
	}
	else
	{
		// Exercise pose retention policy. We must guarantee there is always one additional knot
		// beyond the time horizon so we can compute derivatives at the time horizon. We also
		// want to evenly distribute knots across the entire history buffer so we only push additional
		// poses when enough time has elapsed.

		const float SampleInterval = GetSampleTimeInterval();

		bool bCanEvictOldest = Knots[1] >= TimeHorizon + SampleInterval;
		bool bShouldPushNewest = Knots[Knots.Num() - 2] >= SampleInterval;

		if (bCanEvictOldest && bShouldPushNewest)
		{
			FPose PoseTemp = MoveTemp(Poses.First());
			Poses.PopFront();
			Poses.Emplace(MoveTemp(PoseTemp));

			Knots.PopFront();
			Knots.AddUninitialized();
		}
	}

	// Regardless of the retention policy, we always update the most recent pose
	Knots.Last() = 0.0f;
	FPose& CurrentPose = Poses.Last();
	CopyCompactToSkeletonPose(PoseContext.Pose, CurrentPose.LocalTransforms);
	CurrentPose.WorldComponentTransform = PoseContext.AnimInstanceProxy->GetComponentTransform();
}

float FPoseHistory::GetSampleTimeInterval() const
{
	// Reserve one knot for computing derivatives at the time horizon
	return TimeHorizon / (Knots.Max() - 1);
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorReader

void FFeatureVectorReader::Init(const FPoseSearchFeatureVectorLayout* InLayout)
{
	check(InLayout);
	Layout = InLayout;
}

void FFeatureVectorReader::SetValues(TArrayView<const float> InValues)
{
	check(Layout);
	check(Layout->NumFloats == InValues.Num());
	Values = InValues;
}

bool FFeatureVectorReader::IsValid() const
{
	return Layout && (Layout->NumFloats == Values.Num());
}

bool FFeatureVectorReader::GetTransform(FPoseSearchFeatureDesc Element, FTransform* OutTransform) const
{
	FVector Position;
	bool bResult = GetPosition(Element, &Position);

	FQuat Rotation;
	bResult |= GetRotation(Element, &Rotation);

	OutTransform->SetComponents(Rotation, Position, FVector::OneVector);
	return bResult;
}

bool FFeatureVectorReader::GetPosition(FPoseSearchFeatureDesc Element, FVector* OutPosition) const
{
	Element.Type = EPoseSearchFeatureType::Position;
	return GetVector(Element, OutPosition);
}

bool FFeatureVectorReader::GetRotation(FPoseSearchFeatureDesc Element, FQuat* OutRotation) const
{
	Element.Type = EPoseSearchFeatureType::Rotation;
	int32 ElementIndex = IsValid() ? Layout->Features.Find(Element) : -1;
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

		FVector X;
		FVector Y;

		X.X = Values[FoundElement.ValueOffset + 0];
		X.Y = Values[FoundElement.ValueOffset + 1];
		X.Z = Values[FoundElement.ValueOffset + 2];
		Y.X = Values[FoundElement.ValueOffset + 3];
		Y.Y = Values[FoundElement.ValueOffset + 4];
		Y.Z = Values[FoundElement.ValueOffset + 5];

		FVector Z = FVector::CrossProduct(X, Y);

		FMatrix M(FMatrix::Identity);
		M.SetColumn(0, X);
		M.SetColumn(1, Y);
		M.SetColumn(2, Z);

		*OutRotation = FQuat(M);
		return true;
	}

	*OutRotation = FQuat::Identity;
	return false;
}

bool FFeatureVectorReader::GetLinearVelocity(FPoseSearchFeatureDesc Element, FVector* OutLinearVelocity) const
{
	Element.Type = EPoseSearchFeatureType::LinearVelocity;
	return GetVector(Element, OutLinearVelocity);
}

bool FFeatureVectorReader::GetAngularVelocity(FPoseSearchFeatureDesc Element, FVector* OutAngularVelocity) const
{
	Element.Type = EPoseSearchFeatureType::AngularVelocity;
	return GetVector(Element, OutAngularVelocity);
}

bool FFeatureVectorReader::GetVector(FPoseSearchFeatureDesc Element, FVector* OutVector) const
{
	int32 ElementIndex = IsValid() ? Layout->Features.Find(Element) : -1;
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

		FVector V;
		V.X = Values[FoundElement.ValueOffset + 0];
		V.Y = Values[FoundElement.ValueOffset + 1];
		V.Z = Values[FoundElement.ValueOffset + 2];
		*OutVector = V;
		return true;
	}

	*OutVector = FVector::ZeroVector;
	return false;
}


//////////////////////////////////////////////////////////////////////////
// FDebugDrawParams

bool FDebugDrawParams::CanDraw() const
{
	if (!World || !EnumHasAnyFlags(Flags, EDebugDrawFlags::DrawAll))
	{
		return false;
	}

	const FPoseSearchIndex* SearchIndex = GetSearchIndex();
	if (!SearchIndex)
	{
		return false;
	}

	return SearchIndex->IsValid();
}

const FPoseSearchIndex* FDebugDrawParams::GetSearchIndex() const
{
	if (Database)
	{
		return &Database->SearchIndex;
	}

	if (SequenceMetaData)
	{
		return &SequenceMetaData->SearchIndex;
	}

	return nullptr;
}

const UPoseSearchSchema* FDebugDrawParams::GetSchema() const
{
	if (Database)
	{
		return Database->Schema;
	}

	if (SequenceMetaData)
	{
		return SequenceMetaData->Schema;
	}

	return nullptr;
}


//////////////////////////////////////////////////////////////////////////
// FSequenceSampler

struct FSequenceSampler
{
public:
	struct FInput
	{
		const UPoseSearchSchema* Schema = nullptr;
		const UAnimSequence* Sequence = nullptr;
		bool bLoopable = false;
		int32 DistanceSamplingRate = 60;
	} Input;

	struct FOutput
	{
		TArray<float> AccumulatedRootDistance;

		int32 NumDistanceSamples = 0;
		float PlayLength = 0.0f;
		float TotalRootDistance = 0.0f;
		FTransform TotalRootMotion = FTransform::Identity;
	} Output;

	void Reset();
	void Init(const FInput& Input);
	void Process();

	FTransform ExtractRootTransform(float Time) const;
	float ExtractRootDistance(float Time) const;

private:
	void Reserve();
	void ProcessRootMotion();
};

void FSequenceSampler::Init(const FInput& InInput)
{
	check(InInput.Schema);
	check(InInput.Schema->IsValid());
	check(InInput.Sequence);

	Reset();

	Input = InInput;

	Output.PlayLength = Input.Sequence->GetPlayLength();
	Output.NumDistanceSamples = FMath::CeilToInt(Output.PlayLength * Input.DistanceSamplingRate) + 1;

	Reserve();
}

void FSequenceSampler::Reset()
{
	Input = FInput();

	Output.NumDistanceSamples = 0;
	Output.PlayLength = 0.0f;
	Output.TotalRootDistance = 0.0f;
	Output.AccumulatedRootDistance.Reset(0);
}

void FSequenceSampler::Reserve()
{
	Output.AccumulatedRootDistance.Reserve(Output.NumDistanceSamples);
}

void FSequenceSampler::Process()
{
	ProcessRootMotion();
}

FTransform FSequenceSampler::ExtractRootTransform(float Time) const
{
	return Input.Sequence->ExtractRootMotionFromRange(0.0f, Time);
}

float FSequenceSampler::ExtractRootDistance(float Time) const
{
	check(Time <= Output.PlayLength);

	// Find the distance sample that corresponds with the time and split into whole and partial parts
	float IntegralDistanceSample;
	float DistanceAlpha = FMath::Modf(Time * Input.DistanceSamplingRate, &IntegralDistanceSample);
	float DistanceIdx = (int32)IntegralDistanceSample;

	// Verify the distance offset and any residual portion would be in bounds
	check(DistanceIdx + (DistanceAlpha > 0.0f ? 1 : 0) < Output.AccumulatedRootDistance.Num());
	
	// Look up the distance and interpolate between distance samples if necessary
	float Distance = Output.AccumulatedRootDistance[DistanceIdx];
	if (DistanceAlpha > 0.0f)
	{
		float NextDistance = Output.AccumulatedRootDistance[DistanceIdx + 1];
		Distance = FMath::Lerp(Distance, NextDistance, DistanceAlpha);
	}

	return Distance;
}

void FSequenceSampler::ProcessRootMotion()
{
	// Note the distance sampling interval is independent of the schema's sampling interval
	const float DistanceSamplingInterval = 1.0f / Input.DistanceSamplingRate;

	const FTransform InitialRootTransform = Input.Sequence->ExtractRootTrackTransform(0.0f, nullptr);

	// Build a distance lookup table by sampling root motion at a fixed rate and accumulating
	// absolute translation deltas. During indexing we'll bsearch this table and interpolate
	// between samples in order to convert distance offsets to time offsets.
	// See also FSequenceIndexer::AddTrajectoryDistanceFeatures().
	double AccumulatedRootDistance = 0.0;
	FTransform LastRootTransform = InitialRootTransform;
	float SampleTime = 0.0f;
	for (int32 SampleIdx = 0; SampleIdx != Output.NumDistanceSamples; ++SampleIdx)
	{
		SampleTime = FMath::Min(SampleIdx * DistanceSamplingInterval, Output.PlayLength);

		FTransform RootTransform = Input.Sequence->ExtractRootTrackTransform(SampleTime, nullptr);
		FTransform LocalRootMotion = RootTransform.GetRelativeTransform(LastRootTransform);
		LastRootTransform = RootTransform;

		AccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		Output.AccumulatedRootDistance.Add((float)AccumulatedRootDistance);
	}

	// Verify we sampled the final frame of the clip
	check(SampleTime == Input.Sequence->GetPlayLength());

	// Also emit root motion summary info to help with sample wrapping in 
	// FSequenceIndexer::GetSampleTimeFromDistance() and FSequenceIndexer::GetSampleInfo()
	Output.TotalRootMotion = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	Output.TotalRootDistance = Output.AccumulatedRootDistance.Last();
}

struct FSamplingParam
{
	float WrappedParam = 0.0f;
	int32 NumCycles = 0;
	bool bClamped = false;
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

	if (bCanWrap)
	{
		if (SamplingParam < 0.0f)
		{
			// Note a full reverse cycle isn't completed merely when SamplingParam < 0.0f,
			// rather when SamplingParam < -SamplingParamExtent
			Result.WrappedParam += SamplingParamExtent;
			while (Result.WrappedParam < 0.0f)
			{
				Result.WrappedParam += SamplingParamExtent;
				++Result.NumCycles;
			}
		}

		else
		{
			// On the other hand a full forward cycle is completed each time we exceed SamplingParamExtent
			while (Result.WrappedParam > SamplingParamExtent)
			{
				Result.WrappedParam -= SamplingParamExtent;
				++Result.NumCycles;
			}
		}
	}

	float ParamClamped = FMath::Clamp(Result.WrappedParam, 0.0f, SamplingParamExtent);
	if (ParamClamped != Result.WrappedParam)
	{
		check(!bCanWrap);
		Result.WrappedParam = ParamClamped;
		Result.bClamped = true;
	}
	
	return Result;
}


//////////////////////////////////////////////////////////////////////////
// FSequenceIndexer

class FSequenceIndexer
{
public:

	struct FInput
	{
		const FBoneContainer* BoneContainer = nullptr;
		const UPoseSearchSchema* Schema = nullptr;
		const FSequenceSampler* MainSequence = nullptr;
		const FSequenceSampler* LeadInSequence = nullptr;
		const FSequenceSampler* FollowUpSequence = nullptr;
		FFloatInterval RequestedSamplingRange = FFloatInterval(0.0f, 0.0f);
	} Input;

	struct FOutput
	{
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedPoses = 0;
		TArray<float> FeatureVectorTable;
	} Output;

	void Reset();
	void Init(const FInput& Input);
	void Process();

private:
	FPoseSearchFeatureVectorBuilder FeatureVector;

	struct FSampleInfo
	{
		const FSequenceSampler* Clip = nullptr;
		FTransform RootTransform;
		float ClipTime = 0.0f;
		float RootDistance = 0.0f;

		bool IsValid() const { return Clip != nullptr; }
	};

	FSampleInfo GetSampleInfo(float SampleTime) const;
	FSampleInfo GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const;
	const float GetSampleTimeFromDistance(float Distance) const;

	void Reserve();
	void SampleBegin(int32 SampleIdx);
	void SampleEnd(int32 SampleIdx);
	void AddPoseFeatures(int32 SampleIdx);
	void AddTrajectoryTimeFeatures(int32 SampleIdx);
	void AddTrajectoryDistanceFeatures(int32 SampleIdx);
};

void FSequenceIndexer::Reset()
{
	Output.FirstIndexedSample = 0;
	Output.LastIndexedSample = 0;
	Output.NumIndexedPoses = 0;

	Output.FeatureVectorTable.Reset(0);
}

void FSequenceIndexer::Reserve()
{
	Output.FeatureVectorTable.SetNumZeroed(Input.Schema->Layout.NumFloats * Output.NumIndexedPoses);
}

void FSequenceIndexer::Init(const FInput& InSettings)
{
	check(InSettings.Schema);
	check(InSettings.Schema->IsValid());
	check(InSettings.MainSequence);

	Input = InSettings;

	const FFloatInterval SamplingRange = GetEffectiveSamplingRange(Input.MainSequence->Input.Sequence, Input.RequestedSamplingRange);

	Reset();
	Output.FirstIndexedSample = FMath::FloorToInt(SamplingRange.Min * Input.Schema->SampleRate);
	Output.LastIndexedSample = FMath::Max(0, FMath::CeilToInt(SamplingRange.Max * Input.Schema->SampleRate));
	Output.NumIndexedPoses = Output.LastIndexedSample - Output.FirstIndexedSample + 1;
	Reserve();
}

void FSequenceIndexer::Process()
{
	for (int32 SampleIdx = Output.FirstIndexedSample; SampleIdx <= Output.LastIndexedSample; ++SampleIdx)
	{
		FMemMark Mark(FMemStack::Get());

		SampleBegin(SampleIdx);

		AddPoseFeatures(SampleIdx);
		AddTrajectoryTimeFeatures(SampleIdx);
		AddTrajectoryDistanceFeatures(SampleIdx);

		SampleEnd(SampleIdx);
	}
}

void FSequenceIndexer::SampleBegin(int32 SampleIdx)
{
	FeatureVector.Init(Input.Schema);
}

void FSequenceIndexer::SampleEnd(int32 SampleIdx)
{
	check(FeatureVector.IsComplete());

	int32 FirstValueIdx = (SampleIdx - Output.FirstIndexedSample) * Input.Schema->Layout.NumFloats;
	TArrayView<float> WriteValues = MakeArrayView(&Output.FeatureVectorTable[FirstValueIdx], Input.Schema->Layout.NumFloats);

	TArrayView<const float> ReadValues = FeatureVector.GetValues();
	
	check(WriteValues.Num() == ReadValues.Num());
	FMemory::Memcpy(WriteValues.GetData(), ReadValues.GetData(), WriteValues.Num() * WriteValues.GetTypeSize());
}

const float FSequenceIndexer::GetSampleTimeFromDistance(float SampleDistance) const
{
	auto CanWrapDistanceSamples = [](const FSequenceSampler* Sampler) -> bool
	{
		constexpr float SMALL_ROOT_DISTANCE = 1.0f;
		return Sampler->Input.bLoopable && Sampler->Output.TotalRootDistance > SMALL_ROOT_DISTANCE;
	};

	auto ClipTimeFromDistance = [](const FSequenceSampler* Sampler, float ClipDistance) -> float
	{
		// Search for the distance value
		int32 NextSampleIdx = Algo::LowerBound(Sampler->Output.AccumulatedRootDistance, ClipDistance);
		check(NextSampleIdx < Sampler->Output.AccumulatedRootDistance.Num());
	
		// Compute distance interpolation amount
		int32 PrevSampleIdx = FMath::Max(0, NextSampleIdx - 1);
		float NextDistance = Sampler->Output.AccumulatedRootDistance[NextSampleIdx];
		float PrevDistance = PrevSampleIdx < NextSampleIdx ? Sampler->Output.AccumulatedRootDistance[PrevSampleIdx] : 0.0f;
		float DistanceSampleAlpha = FMath::GetRangePct(PrevDistance, NextDistance, ClipDistance);

		// Convert to time
		float ClipTime = (float(NextSampleIdx) - (1.0f - DistanceSampleAlpha)) / Sampler->Input.DistanceSamplingRate;
		float ClipTimeClamped = FMath::Clamp(ClipTime, 0.0f, Sampler->Output.PlayLength);

		return ClipTimeClamped;
	};

	float MainTotalDistance = Input.MainSequence->Output.TotalRootDistance;
	bool bMainCanWrap = CanWrapDistanceSamples(Input.MainSequence);

	float SampleTime = MAX_flt;

	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (Input.LeadInSequence && (SampleDistance < 0.0f))
		{
			const FSequenceSampler::FOutput& ClipData = Input.LeadInSequence->Output;

			bool bLeadInCanWrap = CanWrapDistanceSamples(Input.LeadInSequence);
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipData.TotalRootDistance, SampleDistance);

			float ClipTime = ClipTimeFromDistance(Input.LeadInSequence, SamplingParam.WrappedParam);

			// Make the lead in clip time relative to the main sequence again and unwrap
			SampleTime = -((SamplingParam.NumCycles * ClipData.PlayLength) + (ClipData.PlayLength - ClipTime));
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (Input.FollowUpSequence && (SampleDistance > MainTotalDistance))
		{
			const FSequenceSampler::FOutput& ClipData = Input.FollowUpSequence->Output;

			bool bFollowUpCanWrap = CanWrapDistanceSamples(Input.FollowUpSequence);
			float FollowRelativeDistance = SampleDistance - MainTotalDistance;
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipData.TotalRootDistance, FollowRelativeDistance);

			float ClipTime = ClipTimeFromDistance(Input.FollowUpSequence, SamplingParam.WrappedParam);

			// Make the follow up clip time relative to the main sequence again and unwrap
			SampleTime = Input.MainSequence->Output.PlayLength + SamplingParam.NumCycles * ClipData.PlayLength + ClipTime;
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (SampleTime == MAX_flt)
	{
		const FSequenceSampler::FOutput& ClipData = Input.MainSequence->Output;

		FSamplingParam SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainTotalDistance, SampleDistance);
		float ClipTime = ClipTimeFromDistance(Input.MainSequence, SamplingParam.WrappedParam);

		// Unwrap the main clip time
		if (SampleDistance < 0.0f)
		{
			SampleTime = -((SamplingParam.NumCycles * ClipData.PlayLength) + (ClipData.PlayLength - ClipTime));
		}
		else
		{
			SampleTime = SamplingParam.NumCycles * ClipData.PlayLength + ClipTime;
		}
	}

	return SampleTime;
}

FSequenceIndexer::FSampleInfo FSequenceIndexer::GetSampleInfo(float SampleTime) const
{
	FSampleInfo Sample;

	FTransform RootMotionLast = FTransform::Identity;
	FTransform RootMotionInitial = FTransform::Identity;

	float RootDistanceLast = 0.0f;
	float RootDistanceInitial = 0.0f;

	auto CanWrapTimeSamples = [](const FSequenceSampler* Sampler) -> bool
	{
		return Sampler->Input.bLoopable;
	};

	float MainPlayLength = Input.MainSequence->Output.PlayLength;
	bool bMainCanWrap = CanWrapTimeSamples(Input.MainSequence);

	FSamplingParam SamplingParam;
	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (Input.LeadInSequence && (SampleTime < 0.0f))
		{
			const FSequenceSampler::FOutput& ClipData = Input.LeadInSequence->Output;

			bool bLeadInCanWrap = CanWrapTimeSamples(Input.LeadInSequence);
			SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipData.PlayLength, SampleTime);

			Sample.Clip = Input.LeadInSequence;

			RootMotionInitial = FTransform::Identity;
			RootDistanceInitial = 0.0f;

			RootMotionLast = Input.LeadInSequence->Output.TotalRootMotion;
			RootDistanceLast = Input.LeadInSequence->Output.TotalRootDistance;
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (Input.FollowUpSequence && (SampleTime > MainPlayLength))
		{
			const FSequenceSampler::FOutput& ClipData = Input.FollowUpSequence->Output;

			bool bFollowUpCanWrap = CanWrapTimeSamples(Input.FollowUpSequence);
			float FollowRelativeTime = SampleTime - MainPlayLength;
			SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipData.PlayLength, FollowRelativeTime);

			Sample.Clip = Input.FollowUpSequence;

			RootMotionInitial = Input.MainSequence->Output.TotalRootMotion;
			RootDistanceInitial = Input.MainSequence->Output.TotalRootDistance;

			RootMotionLast = Input.FollowUpSequence->Output.TotalRootMotion;
			RootDistanceLast = Input.FollowUpSequence->Output.TotalRootDistance;
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (!Sample.IsValid())
	{
		SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainPlayLength, SampleTime);

		Sample.Clip = Input.MainSequence;

		RootMotionInitial = FTransform::Identity;
		RootDistanceInitial = 0.0f;

		RootMotionLast = Input.MainSequence->Output.TotalRootMotion;
		RootDistanceLast = Input.MainSequence->Output.TotalRootDistance;
	}

	Sample.ClipTime = SamplingParam.WrappedParam;

	// Determine how to accumulate motion for every cycle of the anim. If the sample
	// had to be clamped, this motion will end up not getting applied below.
	// Also invert the accumulation direction if the requested sample was wrapped backwards.
	FTransform RootMotionPerCycle = RootMotionLast;
	float RootDistancePerCycle = RootDistanceLast;
	if (SampleTime < 0.0f)
	{
		RootMotionPerCycle = RootMotionPerCycle.Inverse();
		RootDistancePerCycle *= -1;
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
		Sample.RootTransform *= RootMotionPerCycle;
		Sample.RootDistance += RootDistancePerCycle;
	}

	Sample.RootTransform *= RootMotionRemainder;
	Sample.RootDistance += RootDistanceRemainder;

	return Sample;
}

FSequenceIndexer::FSampleInfo FSequenceIndexer::GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const
{
	FSampleInfo Sample = GetSampleInfo(SampleTime);
	Sample.RootTransform.SetToRelativeTransform(Origin.RootTransform);
	Sample.RootDistance = Origin.RootDistance - Sample.RootDistance;
	return Sample;
}

void FSequenceIndexer::AddPoseFeatures(int32 SampleIdx)
{
	// This function samples the instantaneous pose at time t as well as the pose's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three pose extractions are taken at time t-h, t, and t+h
	constexpr float FiniteDelta = 1 / 60.0f;
	constexpr int32 NumFiniteDiffTerms = 3;

	if (Input.Schema->Bones.IsEmpty() || Input.Schema->PoseSampleTimes.IsEmpty())
	{
		return;
	}

	FCompactPose Poses[NumFiniteDiffTerms];
	FCSPose<FCompactPose> ComponentSpacePoses[NumFiniteDiffTerms];
	FBlendedCurve Curves[NumFiniteDiffTerms];
	UE::Anim::FStackAttributeContainer Atrributes[NumFiniteDiffTerms];

	for(FCompactPose& Pose: Poses)
	{
		Pose.SetBoneContainer(Input.BoneContainer);
	}

	FAnimationPoseData AnimPoseData[NumFiniteDiffTerms] = 
	{
		{Poses[0], Curves[0], Atrributes[0]},
		{Poses[1], Curves[1], Atrributes[1]},
		{Poses[2], Curves[2], Atrributes[2]},
	};

	FAnimExtractContext ExtractionCtx;
	ExtractionCtx.bExtractRootMotion = true;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	float SampleTime = FMath::Min(SampleIdx * Input.Schema->SamplingInterval, Input.MainSequence->Output.PlayLength);
	FSampleInfo Origin = GetSampleInfo(SampleTime);
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Input.Schema->PoseSampleTimes.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		float SubsampleTime = SampleTime + Input.Schema->PoseSampleTimes[SchemaSubsampleIdx];

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[NumFiniteDiffTerms];
		Samples[0] = GetSampleInfoRelative(SubsampleTime - FiniteDelta, Origin);
		Samples[1] = GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = GetSampleInfoRelative(SubsampleTime + FiniteDelta, Origin);

		// Get pose samples
		for (int32 Term = 0; Term != NumFiniteDiffTerms; ++Term)
		{
			ExtractionCtx.CurrentTime = Samples[Term].ClipTime;
			Samples[Term].Clip->Input.Sequence->GetAnimationPose(AnimPoseData[Term], ExtractionCtx);
			ComponentSpacePoses[Term].InitPose(Poses[Term]);
		}

		// Get each bone's component transform, velocity, and acceleration and add accumulated root motion at this time offset
		// Think of this process as freezing the character in place (at SampleTime) and then tracing the paths of their joints
		// as they move through space from past to present to future (at times indicated by PoseSampleTimes).
		for (int32 SchemaBoneIndex = 0; SchemaBoneIndex != Input.Schema->NumBones(); ++SchemaBoneIndex)
		{
			Feature.SchemaBoneIdx = SchemaBoneIndex;

			FCompactPoseBoneIndex CompactBoneIndex = Input.BoneContainer->MakeCompactPoseIndex(FMeshPoseBoneIndex(Input.Schema->BoneIndices[SchemaBoneIndex]));

			FTransform BoneTransforms[NumFiniteDiffTerms];
			for (int32 Term = 0; Term != NumFiniteDiffTerms; ++Term)
			{
				BoneTransforms[Term] = ComponentSpacePoses[Term].GetComponentSpaceTransform(CompactBoneIndex);
				BoneTransforms[Term] = BoneTransforms[Term] * Samples[Term].RootTransform;
			}

			// Add properties to the feature vector for the pose at SampleIdx
			FeatureVector.SetTransform(Feature, BoneTransforms[1]);
			FeatureVector.SetTransformVelocity(Feature, BoneTransforms[2], BoneTransforms[0], 2.0f * FiniteDelta);
			//FeatureVector.SetTransformAccleration(Feature, BoneTransforms[2], BoneTransforms[1], BoneTransforms[0], FiniteDelta * FiniteDelta);
		}
	}
}

void FSequenceIndexer::AddTrajectoryTimeFeatures(int32 SampleIdx)
{
	// This function samples the instantaneous trajectory at time t as well as the trajectory's velocity and acceleration at time t.
	// Symmetric finite differences are used to approximate derivatives:
	//	First symmetric derivative:   f'(t) ~ (f(t+h) - f(t-h)) / 2h
	//	Second symmetric derivative: f''(t) ~ (f(t+h) - 2f(t) + f(t-h)) / h^2
	// Where h is a constant time delta
	// So this means three root motion extractions are taken at time t-h, t, and t+h
	constexpr float FiniteDelta = 1 / 60.0f;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	float SampleTime = FMath::Min(SampleIdx * Input.Schema->SamplingInterval, Input.MainSequence->Output.PlayLength);
	FSampleInfo Origin = GetSampleInfo(SampleTime);
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Input.Schema->TrajectorySampleTimes.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		float SubsampleTime = SampleTime + Input.Schema->TrajectorySampleTimes[SchemaSubsampleIdx];

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[3];
		Samples[0] = GetSampleInfoRelative(SubsampleTime - FiniteDelta, Origin);
		Samples[1] = GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = GetSampleInfoRelative(SubsampleTime + FiniteDelta, Origin);

		// Add properties to the feature vector for the pose at SampleIdx
		FeatureVector.SetTransform(Feature, Samples[1].RootTransform);
		FeatureVector.SetTransformVelocity(Feature, Samples[2].RootTransform, Samples[0].RootTransform, 2.0f * FiniteDelta);
		//FeatureVector.SetTransformAcceleration(Feature, Samples[2].RootTransform, Samples[1].RootTransform, Samples[0].RootTransform, FiniteDelta * FiniteDelta);
	}
}

void FSequenceIndexer::AddTrajectoryDistanceFeatures(int32 SampleIdx)
{
	// This function is very similar to AddTrajectoryTimeFeatures, but samples are taken in the distance domain
	// instead of time domain.
	constexpr float FiniteDelta = 1 / 60.0f;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Distance;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	float SampleTime = FMath::Min(SampleIdx * Input.Schema->SamplingInterval, Input.MainSequence->Output.PlayLength);
	FSampleInfo Origin = GetSampleInfo(SampleTime);
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Input.Schema->TrajectorySampleDistances.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		// For distance based sampling of the trajectory, we first have to look up the time value
		// we're sampling given the desired travel distance of the root for this distance offset.
		// Once we know the time, we can then carry on just like time-based sampling.
		const float SubsampleDistance = Origin.RootDistance + Input.Schema->TrajectorySampleDistances[SchemaSubsampleIdx];
		float SubsampleTime = GetSampleTimeFromDistance(SubsampleDistance);

		// For each pose subsample term, get the corresponding clip, accumulated root motion,
		// and wrap the time parameter based on the clip's length.
		FSampleInfo Samples[3];
		Samples[0] = GetSampleInfoRelative(SubsampleTime - FiniteDelta, Origin);
		Samples[1] = GetSampleInfoRelative(SubsampleTime, Origin);
		Samples[2] = GetSampleInfoRelative(SubsampleTime + FiniteDelta, Origin);

		// Add properties to the feature vector for the pose at SampleIdx
		FeatureVector.SetTransform(Feature, Samples[1].RootTransform);
		FeatureVector.SetTransformVelocity(Feature, Samples[2].RootTransform, Samples[0].RootTransform, 2.0f * FiniteDelta);
		//FeatureVector.SetTransformAcceleration(Feature, Samples[0].RootTransform, Samples[1].RootTransform, Samples[2].RootTransform, FiniteDelta * FiniteDelta);
	}
}


//////////////////////////////////////////////////////////////////////////
// PoseSearch API

static void DrawTrajectoryFeatures(const FDebugDrawParams& DrawParams, const FFeatureVectorReader& Reader, EPoseSearchFeatureDomain Domain)
{
	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = Domain;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	const int32 NumSubsamples = Domain == EPoseSearchFeatureDomain::Time ?
		DrawParams.GetSchema()->TrajectorySampleTimes.Num() :
		DrawParams.GetSchema()->TrajectorySampleDistances.Num();

	if (NumSubsamples == 0)
	{
		return;
	}

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != NumSubsamples; ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		FVector TrajectoryPos;
		if (Reader.GetPosition(Feature, &TrajectoryPos))
		{	
			Feature.Type = EPoseSearchFeatureType::Position;
			FLinearColor LinearColor = GetColorForFeature(Feature, Reader.GetLayout());
			FColor Color = LinearColor.ToFColor(true);

			TrajectoryPos = DrawParams.RootTransform.TransformPosition(TrajectoryPos);
			DrawDebugSphere(DrawParams.World, TrajectoryPos, DrawDebugSphereSize, DrawDebugSphereSegments, Color, false, LifeTime, DepthPriority,  DrawDebugSphereLineThickness);
		}
		else
		{
			TrajectoryPos = DrawParams.RootTransform.GetTranslation();
		}

		FVector TrajectoryVel;
		if (Reader.GetLinearVelocity(Feature, &TrajectoryVel))
		{
			Feature.Type = EPoseSearchFeatureType::LinearVelocity;
			FLinearColor LinearColor = GetColorForFeature(Feature, Reader.GetLayout());
			FColor Color = LinearColor.ToFColor(true);

			TrajectoryVel *= DrawDebugVelocityScale;
			TrajectoryVel = DrawParams.RootTransform.TransformVector(TrajectoryVel);
			FVector TrajectoryVelDirection = TrajectoryVel.GetSafeNormal();
			DrawDebugDirectionalArrow(DrawParams.World, TrajectoryPos + TrajectoryVelDirection * DrawDebugSphereSize, TrajectoryPos + TrajectoryVel, DrawDebugArrowSize, Color, false, LifeTime, DepthPriority, DrawDebugLineThickness);
		}
	}
}

static void DrawPoseFeatures(const FDebugDrawParams& DrawParams, const FFeatureVectorReader& Reader)
{
	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema && Schema->IsValid());

	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	const int32 NumSubsamples = Schema->PoseSampleTimes.Num();
	const int32 NumBones = Schema->Bones.Num();

	if ((NumSubsamples * NumBones) == 0)
	{
		return;
	}

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != NumSubsamples; ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;
		
		for (int32 SchemaBoneIdx = 0; SchemaBoneIdx != NumBones; ++SchemaBoneIdx)
		{
			Feature.SchemaBoneIdx = SchemaBoneIdx;

			FVector BonePos;
			bool bHaveBonePos = Reader.GetPosition(Feature, &BonePos);
			if (bHaveBonePos)
			{
				Feature.Type = EPoseSearchFeatureType::Position;
				FLinearColor Color = GetColorForFeature(Feature, Reader.GetLayout());

				BonePos = DrawParams.RootTransform.TransformPosition(BonePos);
				DrawDebugSphere(DrawParams.World, BonePos, DrawDebugSphereSize, DrawDebugSphereSegments, Color.ToFColor(true), false, LifeTime, DepthPriority, DrawDebugSphereLineThickness);
			}

			FVector BoneVel;
			if (bHaveBonePos && Reader.GetLinearVelocity(Feature, &BoneVel))
			{
				Feature.Type = EPoseSearchFeatureType::LinearVelocity;
				FLinearColor Color = GetColorForFeature(Feature, Reader.GetLayout());

				BoneVel *= DrawDebugVelocityScale;
				BoneVel = DrawParams.RootTransform.TransformVector(BoneVel);
				FVector BoneVelDirection = BoneVel.GetSafeNormal();
				DrawDebugDirectionalArrow(DrawParams.World, BonePos + BoneVelDirection * DrawDebugSphereSize, BonePos + BoneVel, DrawDebugArrowSize, Color.ToFColor(true), false, LifeTime, DepthPriority, DrawDebugLineThickness);
			}
		}
	}
}

static void DrawFeatureVector(const FDebugDrawParams& DrawParams, const FFeatureVectorReader& Reader)
{
	DrawPoseFeatures(DrawParams, Reader);
	DrawTrajectoryFeatures(DrawParams, Reader, EPoseSearchFeatureDomain::Time);
	DrawTrajectoryFeatures(DrawParams, Reader, EPoseSearchFeatureDomain::Distance);
}

static void DrawSearchIndex(const FDebugDrawParams& DrawParams)
{
	if (!DrawParams.CanDraw())
	{
		return;
	}

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	const FPoseSearchIndex* SearchIndex = DrawParams.GetSearchIndex();
	check(Schema);
	check(SearchIndex);

	FFeatureVectorReader Reader;
	Reader.Init(&Schema->Layout);

	int32 LastPoseIdx = SearchIndex->NumPoses;
	int32 StartPoseIdx = 0;
	if (!(DrawParams.Flags & EDebugDrawFlags::DrawSearchIndex))
	{
		StartPoseIdx = DrawParams.HighlightPoseIdx;
		LastPoseIdx = StartPoseIdx + 1;
	}

	if (StartPoseIdx < 0)
	{
		return;
	}

	TArray<float> PoseVector;
	for (int32 PoseIdx = StartPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
	{
		PoseVector = SearchIndex->GetPoseValues(PoseIdx);
		SearchIndex->InverseNormalize(PoseVector);
		Reader.SetValues(PoseVector);

		DrawFeatureVector(DrawParams, Reader);
	}
}

static void DrawQuery(const FDebugDrawParams& DrawParams)
{
	if (!DrawParams.CanDraw())
	{
		return;
	}

	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema);

	if (DrawParams.Query.Num() != Schema->Layout.NumFloats)
	{
		return;
	}

	FFeatureVectorReader Reader;
	Reader.Init(&Schema->Layout);
	Reader.SetValues(DrawParams.Query);
	DrawFeatureVector(DrawParams, Reader);
}

void Draw(const FDebugDrawParams& DebugDrawParams)
{
	if (DebugDrawParams.CanDraw())
	{
		if (EnumHasAnyFlags(DebugDrawParams.Flags, EDebugDrawFlags::DrawQuery))
		{
			DrawQuery(DebugDrawParams);
		}

		if (EnumHasAnyFlags(DebugDrawParams.Flags, EDebugDrawFlags::DrawSearchIndex | EDebugDrawFlags::DrawBest))
		{
			DrawSearchIndex(DebugDrawParams);
		}
	}
}

static void PreprocessSearchIndexNone(FPoseSearchIndex* SearchIndex)
{
	// This function leaves the data unmodified and simply outputs the transformation
	// and inverse transformation matrices as the identity matrix and the sample mean
	// as the zero vector.

	using namespace Eigen;

	check(SearchIndex->IsValid());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(NumDimensions * NumPoses);
	Info.InverseTransformationMatrix.SetNumZeroed(NumDimensions * NumPoses);
	Info.SampleMean.SetNumZeroed(NumDimensions);

	// Map output transformation matrix
	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		NumDimensions, NumPoses
	);

	// Map output inverse transformation matrix
	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		NumDimensions, NumPoses
	);

	// Map output sample mean vector
	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), NumDimensions);

	// Write the transformation matrices and sample mean
	TransformMap = MatrixXf::Identity(NumDimensions, NumPoses);
	InverseTransformMap = MatrixXf::Identity(NumDimensions, NumPoses);
	SampleMeanMap = VectorXf::Zero(NumDimensions);
}

inline Eigen::VectorXd ComputeFeatureMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, const FPoseSearchFeatureVectorLayout& Layout) {
	using namespace Eigen;

	const int32 NumPoses = CenteredPoseMatrix.cols();
	const int32 NumDimensions = CenteredPoseMatrix.rows();

	VectorXd MeanDeviations(NumDimensions);
	MeanDeviations.setConstant(1.0);
	for (const FPoseSearchFeatureDesc& Feature : Layout.Features)
	{
		int32 FeatureDims = GetFeatureTypeTraits(Feature.Type).NumFloats;

		// Construct a submatrix for the feature and find the average distance to the feature's centroid.
		// Since we've already mean centered the data, the average distance to the centroid is simply the average norm.
		double FeatureMeanDeviation = CenteredPoseMatrix.block(Feature.ValueOffset, 0, FeatureDims, NumPoses).colwise().norm().mean();

		// Fill the feature's corresponding scaling axes with the average distance
		// Avoid scaling by zero by leaving near-zero deviations as 1.0
		if (FeatureMeanDeviation > KINDA_SMALL_NUMBER)
		{
			MeanDeviations.segment(Feature.ValueOffset, FeatureDims).setConstant(FeatureMeanDeviation);
		}
	}

	return MeanDeviations;
}

static void PreprocessSearchIndexNormalize(FPoseSearchIndex* SearchIndex)
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
	// The pose matrix is transformed in place and the tranformation matrix, its inverse,
	// and data mean vector are computed and stored along with it.
	//
	// N:	number of dimensions for input column vectors
	// P:	number of input column vectors
	// X:	NxP input matrix
	// x_p:	pth column vector of input matrix
	// u:   mean column vector of X
	//
	// S:	mean absolute deviations of X, as diagonal NxN matrix with average distances replicated for each feature's axes
	// s_n:	nth deviation
	//
	// Normalization by mean absolute deviation algorithm:
	//
	// 1) mean-center X
	//    x_p := x_p - u
	// 2) rescale X by inverse mean absolute deviation
	//    x_p := x_p * s_n^(-1)
	// 
	// Let S^(-1) be the inverse of S where the nth diagonal element is s_n^(-1)
	// then step 2 can be expressed as matrix multiplication:
	// X := S^(-1) * X
	//
	// By persisting the mean vector u and linear transform S, we can bring an input vector q
	// into the same space as the mean centered and scaled data matrix X:
	// q := S^(-1) * (q - u)
	//
	// This operation is invertible, a normalized data vector x can be unscaled via:
	// x := (S * x) + u
	//
	// References:
	// [1] Gorard, S. (2005), "Revisiting a 90-Year-Old Debate: The Advantages of the Mean Deviation."
	//     British Journal of Educational Studies, 53: 417-430.

	using namespace Eigen;

	check(SearchIndex->IsValid());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	// Map input buffer
	auto PoseMatrixSourceMap = Map<Matrix<float, Dynamic, Dynamic, RowMajor>>(
		SearchIndex->Values.GetData(),
		NumPoses,		// rows
		NumDimensions	// cols
	);

	// Copy row major float matrix to column major double matrix
	MatrixXd PoseMatrix = PoseMatrixSourceMap.transpose().cast<double>();
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

#if UE_POSE_SEARCH_EIGEN_DEBUG
	MatrixXd PoseMatrixOriginal = PoseMatrix;
#endif

	// Mean center
	VectorXd SampleMean = PoseMatrix.rowwise().mean();
	PoseMatrix = PoseMatrix.colwise() - SampleMean;

	// Compute per-feature average distances
	VectorXd MeanDeviations = ComputeFeatureMeanDeviations(PoseMatrix, Layout);

	// Construct a scaling matrix that uniformly scales each feature by its average distance from the mean
	MatrixXd ScalingMatrix = MeanDeviations.cwiseInverse().asDiagonal();

	// Construct the inverse scaling matrix
	MatrixXd InverseScalingMatrix = MeanDeviations.asDiagonal();

	// Rescale data by transforming it with the scaling matrix
	// Now each feature has an average Euclidean length = 1.
	PoseMatrix = ScalingMatrix * PoseMatrix;

	// Write normalized data back to source buffer, converting from column data back to row data
	PoseMatrixSourceMap = PoseMatrix.transpose().cast<float>();

	// Output preprocessing info
	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(ScalingMatrix.size());
	Info.InverseTransformationMatrix.SetNumZeroed(InverseScalingMatrix.size());
	Info.SampleMean.SetNumZeroed(SampleMean.size());

	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		ScalingMatrix.rows(), ScalingMatrix.cols()
	);

	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		InverseScalingMatrix.rows(), InverseScalingMatrix.cols()
	);

	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), SampleMean.size());

	// Output scaling matrix, inverse scaling matrix, and mean vector
	TransformMap = ScalingMatrix.cast<float>();
	InverseTransformMap = InverseScalingMatrix.cast<float>();
	SampleMeanMap = SampleMean.cast<float>();

#if UE_POSE_SEARCH_EIGEN_DEBUG
	FString PoseMtxOriginalStr = EigenMatrixToString(PoseMatrixOriginal);
	FString PoseMtxStr = EigenMatrixToString(PoseMatrix);
	FString TransformationStr = EigenMatrixToString(TransformMap);
	FString InverseTransformationStr = EigenMatrixToString(InverseTransformMap);
	FString SampleMeanStr = EigenMatrixToString(SampleMeanMap);
#endif // UE_POSE_SEARCH_EIGEN_DEBUG
}

static void PreprocessSearchIndexSphere(FPoseSearchIndex* SearchIndex)
{
	// This function performs correlation based zero-phase component analysis sphering (ZCA-cor sphering)
	// The pose matrix is transformed in place and the tranformation matrix, its inverse,
	// and data mean vector are computed and stored along with it.
	//
	// N:	number of dimensions for input column vectors
	// P:	number of input column vectors
	// X:	NxP input matrix
	// x_p:	pth column vector of input matrix
	// u:   mean column vector of X
	//
	// Eigendecomposition of correlation matrix of X:
	// cor(X) = (1/P) * X * X^T = V * D * V^T
	//
	// V:	eigenvectors of cor(X), stacked as columns in an orthogonal NxN matrix
	// D:	eigenvalues of cor(X), as diagonal NxN matrix
	// d_n:	nth eigenvalue
	// s_n: nth standard deviation
	// s_n^2 = d_n, the variance along the nth eigenvector
	// s_n   = d_n^(1/2)
	//
	// ZCA sphering algorithm:
	//
	// 1) mean-center X
	//    x_p := x_p - u
	// 2) align largest orthogonal directions of variance in X to coordinate axes (PCA rotate)
	//    x_p := V^T * x_p
	// 3) rescale X by inverse standard deviation
	//    x_p := x_p * d_n^(-1/2)
	// 4) return now rescaled X back to original rotation (inverse PCA rotate)
	//    x_p := V * x_p
	// 
	// Let D^(-1/2) be the inverse square root of D where the nth diagonal element is d_n^(-1/2)
	// then steps 2-4 can be expressed as a series of matrix multiplications:
	// Z = V * D^(-1/2) * V^T
	// X := Z * X
	//
	// By persisting the mean vector u and linear transform Z, we can bring an input vector q
	// into the same space as the sphered data matrix X:
	// q := Z * (q - u)
	//
	// This operation is invertible, a sphere standardized data vector x can be unscaled via:
	// Z^(-1) = V * D^(1/2) * V^T
	// x := (Z^(-1) * x) + u
	//
	// The sphering processs allows nearest neighbor queries to use the Mahalonobis metric
	// which is unitless, scale-invariant, and accounts for feature correlation.
	// The Mahalanobis distance between two random vectors x and y in data matrix X is:
	// d(x,y) = ((x-y)^T * cov(X)^(-1) * (x-y))^(1/2)
	//
	// Because sphering transforms X into a new matrix with identity covariance, the Mahalonobis
	// distance equation above reduces to Eucledean distance since cov(X)^(-1) = I:
	// d(x,y) = ((x-y)^T * (x-y))^(1/2)
	// 
	// References:
	// Watt, Jeremy, et al. Machine Learning Refined: Foundations, Algorithms, and Applications.
	// 2nd ed., Cambridge University Press, 2020.
	// 
	// Kessy, Agnan, Alex Lewin, and Korbinian Strimmer. "Optimal whitening and decorrelation."
	// The American Statistician 72.4 (2018): 309-314.
	// 
	// https://en.wikipedia.org/wiki/Whitening_transformation
	// 
	// https://en.wikipedia.org/wiki/Mahalanobis_distance
	//
	// Note this sphering preprocessor needs more work and isn't yet exposed in the editor as an option.
	// Todo:
	// - Try singular value decomposition in place of eigendecomposition
	// - Remove zero variance feature axes from data and search queries
	// - Support weighted Mahalanobis metric. User supplied weights need to be transformed to data's new basis.

#if UE_POSE_SEARCH_EIGEN_DEBUG
	double StartTime = FPlatformTime::Seconds();
#endif

	using namespace Eigen;

	check(SearchIndex->IsValid());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	// Map input buffer
	auto PoseMatrixSourceMap = Map<Matrix<float, Dynamic, Dynamic, RowMajor>>(
		SearchIndex->Values.GetData(),
		NumPoses,		// rows
		NumDimensions	// cols
	);

	// Copy row major float matrix to column major double matrix
	MatrixXd PoseMatrix = PoseMatrixSourceMap.transpose().cast<double>();
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

#if UE_POSE_SEARCH_EIGEN_DEBUG
	MatrixXd PoseMatrixOriginal = PoseMatrix;
#endif

	// Mean center
	VectorXd SampleMean = PoseMatrix.rowwise().mean();
	PoseMatrix = PoseMatrix.colwise() - SampleMean;

	// Compute per-feature average distances
	VectorXd MeanDeviations = ComputeFeatureMeanDeviations(PoseMatrix, Layout);

	// Rescale data by transforming it with the scaling matrix
	// Now each feature has an average Euclidean length = 1.
	MatrixXd PoseMatrixNormalized = MeanDeviations.cwiseInverse().asDiagonal() * PoseMatrix;

	// Compute sample covariance
	MatrixXd Covariance = ((1.0 / NumPoses) * (PoseMatrixNormalized * PoseMatrixNormalized.transpose())) + 1e-7 * MatrixXd::Identity(NumDimensions, NumDimensions);

	VectorXd StdDev = Covariance.diagonal().cwiseSqrt();
	VectorXd InvStdDev = StdDev.cwiseInverse();
	MatrixXd Correlation = InvStdDev.asDiagonal() * Covariance * InvStdDev.asDiagonal();

	// Compute eigenvalues and eigenvectors of correlation matrix
	SelfAdjointEigenSolver<MatrixXd> EigenDecomposition(Correlation, ComputeEigenvectors);

	VectorXd EigenValues = EigenDecomposition.eigenvalues();
	MatrixXd EigenVectors = EigenDecomposition.eigenvectors();

	// Sort eigenpairs by descending eigenvalue
	{
		const Eigen::Index n = EigenValues.size();
		for (Eigen::Index i = 0; i < n-1; ++i)
		{
			Index k;
			EigenValues.segment(i,n-i).maxCoeff(&k);
			if (k > 0)
			{
				std::swap(EigenValues[i], EigenValues[k+i]);
				EigenVectors.col(i).swap(EigenVectors.col(k+i));
			}
		}
	}

	// Regularize eigenvalues
	EigenValues = EigenValues.array() + 1e-7;

	// Compute ZCA-cor and ZCA-cor^(-1)
	MatrixXd ZCA = EigenVectors * EigenValues.cwiseInverse().cwiseSqrt().asDiagonal() * EigenVectors.transpose() * MeanDeviations.cwiseInverse().asDiagonal();
	MatrixXd ZCAInverse = MeanDeviations.asDiagonal() * EigenVectors * EigenValues.cwiseSqrt().asDiagonal() * EigenVectors.transpose();

	// Apply sphering transform to the data matrix
	PoseMatrix = ZCA * PoseMatrix;
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

	// Write data back to source buffer, converting from column data back to row data
	PoseMatrixSourceMap = PoseMatrix.transpose().cast<float>();

	// Output preprocessing info
	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(ZCA.size());
	Info.InverseTransformationMatrix.SetNumZeroed(ZCAInverse.size());
	Info.SampleMean.SetNumZeroed(SampleMean.size());

	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		ZCA.rows(), ZCA.cols()
	);

	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		ZCAInverse.rows(), ZCAInverse.cols()
	);

	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), SampleMean.size());

	// Output sphering matrix, inverse sphering matrix, and mean vector
	TransformMap = ZCA.cast<float>();
	InverseTransformMap = ZCAInverse.cast<float>();
	SampleMeanMap = SampleMean.cast<float>();

#if UE_POSE_SEARCH_EIGEN_DEBUG
	double ElapsedTime = FPlatformTime::Seconds() - StartTime;

	FString EigenValuesStr = EigenMatrixToString(EigenValues);
	FString EigenVectorsStr = EigenMatrixToString(EigenVectors);

	FString CovarianceStr = EigenMatrixToString(Covariance);
	FString CorrelationStr = EigenMatrixToString(Correlation);

	FString ZCAStr = EigenMatrixToString(ZCA);
	FString ZCAInverseStr = EigenMatrixToString(ZCAInverse);

	FString PoseMatrixSphereStr = EigenMatrixToString(PoseMatrix);
	MatrixXd PoseMatrixUnsphered = ZCAInverse * PoseMatrix;
	PoseMatrixUnsphered = PoseMatrixUnsphered.colwise() + SampleMean;
	FString PoseMatrixUnspheredStr = EigenMatrixToString(PoseMatrixUnsphered);
	FString PoseMatrixOriginalStr = EigenMatrixToString(PoseMatrixOriginal);

	FString OutputPoseMatrixStr = EigenMatrixToString(PoseMatrixSourceMap);

	FString TransformStr = EigenMatrixToString(TransformMap);
	FString InverseTransformStr = EigenMatrixToString(InverseTransformMap);
	FString SampleMeanStr = EigenMatrixToString(SampleMeanMap);
#endif // UE_POSE_SEARCH_EIGEN_DEBUG
}

static void PreprocessSearchIndex(FPoseSearchIndex* SearchIndex)
{
	switch (SearchIndex->Schema->EffectiveDataPreprocessor)
	{
		case EPoseSearchDataPreprocessor::Normalize:
			PreprocessSearchIndexNormalize(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::Sphere:
			PreprocessSearchIndexSphere(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::None:
			PreprocessSearchIndexNone(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::Invalid:
			checkNoEntry();
		break;
	}
}

bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData)
{
	check(Sequence);
	check(SequenceMetaData);

	if (!SequenceMetaData->IsValidForIndexing())
	{
		return false;
	}

	USkeleton* SeqSkeleton = Sequence->GetSkeleton();
	if (!SeqSkeleton || !SeqSkeleton->IsCompatible(SequenceMetaData->Schema->Skeleton))
	{
		return false;
	}

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(SequenceMetaData->Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *SeqSkeleton);

	FSequenceSampler Sampler;
	FSequenceSampler::FInput SamplerInput;
	SamplerInput.Schema = SequenceMetaData->Schema;
	SamplerInput.Sequence = Sequence;
	SamplerInput.bLoopable = false;
	Sampler.Init(SamplerInput);
	Sampler.Process();

	FSequenceIndexer Indexer;
	FSequenceIndexer::FInput IndexerInput;
	IndexerInput.MainSequence = &Sampler;
	IndexerInput.Schema = SequenceMetaData->Schema;
	IndexerInput.RequestedSamplingRange = SequenceMetaData->SamplingRange;
	IndexerInput.BoneContainer = &BoneContainer;
	Indexer.Init(IndexerInput);
	Indexer.Process();

	SequenceMetaData->SearchIndex.Values = Indexer.Output.FeatureVectorTable;
	SequenceMetaData->SearchIndex.NumPoses = Indexer.Output.NumIndexedPoses;
	SequenceMetaData->SearchIndex.Schema = SequenceMetaData->Schema;

	PreprocessSearchIndex(&SequenceMetaData->SearchIndex);

	return true;
}

bool BuildIndex(UPoseSearchDatabase* Database)
{
	check(Database);

	if (!Database->IsValidForIndexing())
	{
		return false;
	}

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Database->Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Database->Schema->Skeleton);

	// Prepare animation preprocessing tasks
	TArray<FSequenceSampler> SequenceSamplers;
	TMap<const UAnimSequence*, int32> SequenceSamplerMap;

	auto AddSampler = [&](const UAnimSequence* Sequence, bool bLoopable)
	{
		if (!SequenceSamplerMap.Contains(Sequence))
		{
			int32 SequenceSamplerIdx = SequenceSamplers.AddDefaulted();
			SequenceSamplerMap.Add(Sequence, SequenceSamplerIdx);

			FSequenceSampler::FInput Input;
			Input.Schema = Database->Schema;
			Input.Sequence = Sequence;
			Input.bLoopable = bLoopable;
			SequenceSamplers[SequenceSamplerIdx].Init(Input);
		}
	};

	for (const FPoseSearchDatabaseSequence& DbSequence : Database->Sequences)
	{
		if (DbSequence.Sequence)
		{
			AddSampler(DbSequence.Sequence, DbSequence.bLoopAnimation);
		}

		if (DbSequence.LeadInSequence)
		{
			AddSampler(DbSequence.LeadInSequence, DbSequence.bLoopLeadInAnimation);
		}

		if (DbSequence.FollowUpSequence)
		{
			AddSampler(DbSequence.FollowUpSequence, DbSequence.bLoopFollowUpAnimation);
		}
	}

	// Preprocess animations independently
	ParallelFor(SequenceSamplers.Num(), [&SequenceSamplers](int32 SamplerIdx){ SequenceSamplers[SamplerIdx].Process(); });


	auto GetSampler = [&](const UAnimSequence* Sequence) -> const FSequenceSampler*
	{
		return Sequence ? &SequenceSamplers[SequenceSamplerMap[Sequence]] : nullptr;
	};

	// Prepare animation indexing tasks
	TArray<FSequenceIndexer> Indexers;
	Indexers.SetNum(Database->Sequences.Num());
	for (int32 SequenceIdx = 0; SequenceIdx != Database->Sequences.Num(); ++SequenceIdx)
	{
		const FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[SequenceIdx];
		FSequenceIndexer& Indexer = Indexers[SequenceIdx];

		FSequenceIndexer::FInput Input;
		Input.BoneContainer = &BoneContainer;
		Input.MainSequence = GetSampler(DbSequence.Sequence);
		Input.LeadInSequence = GetSampler(DbSequence.LeadInSequence);
		Input.FollowUpSequence = GetSampler(DbSequence.FollowUpSequence);
		Input.Schema = Database->Schema;
		Input.RequestedSamplingRange = DbSequence.SamplingRange;
		Indexer.Init(Input);
	}

	// Index animations independently
	ParallelFor(Indexers.Num(), [&Indexers](int32 SequenceIdx){ Indexers[SequenceIdx].Process(); });


	// Write index info to sequence and count up total poses and storage required
	int32 TotalPoses = 0;
	int32 TotalFloats = 0;
	for (int32 SequenceIdx = 0; SequenceIdx != Database->Sequences.Num(); ++SequenceIdx)
	{
		FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[SequenceIdx];
		const FSequenceIndexer::FOutput& Output = Indexers[SequenceIdx].Output;
		DbSequence.NumPoses = Output.NumIndexedPoses;
		DbSequence.FirstPoseIdx = TotalPoses;
		TotalPoses += Output.NumIndexedPoses;
		TotalFloats += Output.FeatureVectorTable.Num();
	}

	// Establish per-sequence pose search bias weights if metadata is present
	for (int32 SequenceIdx = 0; SequenceIdx != Database->Sequences.Num(); ++SequenceIdx)
	{
		FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[SequenceIdx];
		if (DbSequence.Sequence)
		{
			const UPoseSearchSequenceBiasWeightMetaData* BiasWeightMetadata = DbSequence.Sequence->FindMetaDataByClass<UPoseSearchSequenceBiasWeightMetaData>();
			if (BiasWeightMetadata)
			{
				DbSequence.BiasWeights.Init(BiasWeightMetadata->BiasWeights, Database->Schema->Layout);
			}
		}
	}

	// Join animation data into a single search index
	Database->SearchIndex.Values.Reset(TotalFloats);
	for (const FSequenceIndexer& Indexer : Indexers)
	{
		const FSequenceIndexer::FOutput& Output = Indexer.Output;
		Database->SearchIndex.Values.Append(Output.FeatureVectorTable.GetData(), Output.FeatureVectorTable.Num());
	}

	Database->SearchIndex.NumPoses = TotalPoses;
	Database->SearchIndex.Schema = Database->Schema;

	PreprocessSearchIndex(&Database->SearchIndex);

	return true;
}

static inline void DefaultInitializeWeights(const FPoseSearchBiasWeightsContext* BiasWeightsContext, int32 Size, TArray<float>& Weights, bool& bBiasWeightContextAvailable)
{
	bBiasWeightContextAvailable = BiasWeightsContext && BiasWeightsContext->HasBiasWeights();

	if (bBiasWeightContextAvailable)
	{
		Weights = BiasWeightsContext->BiasWeights->Weights;
	}
	else
	{
		Weights.Init(1.f, Size);
	}
}

static FSearchResult Search(const FPoseSearchIndex& SearchIndex, TArrayView<const float> Query, const FPoseSearchBiasWeightsContext* BiasWeightsContext = nullptr)
{
	FSearchResult Result;
	if (!ensure(SearchIndex.IsValid()))
	{
		return Result;
	}

	if(!ensure(Query.Num() == SearchIndex.Schema->Layout.NumFloats))
	{
		return Result;
	}

	bool bBiasWeightContextAvailable = false;

	// Initial weights by default are set to 1, but may be independently set by an external system such as motion matching
	TArray<float> InitialWeights;
	DefaultInitializeWeights(BiasWeightsContext, Query.Num(), InitialWeights, bBiasWeightContextAvailable);
	const auto InitialWeightsMap = Eigen::Map<const Eigen::ArrayXf>(InitialWeights.GetData(), InitialWeights.Num());

	// Accumulated weights will contain the per-pose final weights, optionally including per-sequence and/or other external values
	TArray<float> AccumulatedWeights(InitialWeights);
	auto AccumulatedWeightsMap = Eigen::Map<Eigen::ArrayXf>(AccumulatedWeights.GetData(), AccumulatedWeights.Num());

	float BestPoseDissimilarity = MAX_flt;
	int32 BestPoseIdx = INDEX_NONE;

	for (int32 PoseIdx = 0, PrevSequenceIdx = INDEX_NONE; PoseIdx != SearchIndex.NumPoses; ++PoseIdx)
	{
		// Sequence index and metadata tracking are done within this loop in order to optimize
		// and elide unnecessary recomputing of the weight buffer.
		bool bSequenceWeightsAvailable = false;
		int32_t SequenceIdx = INDEX_NONE;

		if (bBiasWeightContextAvailable)
		{
			// Apply the per-sequence bias weights if they are present within the sequence metadata
			SequenceIdx = BiasWeightsContext->Database->FindSequenceForPose(PoseIdx);
			if (SequenceIdx != PrevSequenceIdx)
			{
				const FPoseSearchDatabaseSequence& SequenceEntry = BiasWeightsContext->Database->Sequences[SequenceIdx];
				bSequenceWeightsAvailable = SequenceEntry.BiasWeights.IsInitialized();

				if (bSequenceWeightsAvailable)
				{
					const auto& SequenceBiasWeights = SequenceEntry.BiasWeights.Weights;
					AccumulatedWeightsMap *= Eigen::Map<const Eigen::ArrayXf>(SequenceBiasWeights.GetData(), SequenceBiasWeights.Num());
				}
			}
		}

		const int32 FeatureValueOffset = PoseIdx * SearchIndex.Schema->Layout.NumFloats;

		const float PoseDissimilarity = CompareFeatureVectors(
			SearchIndex.Schema->Layout.NumFloats,
			Query.GetData(),
			&SearchIndex.Values[FeatureValueOffset],
			AccumulatedWeights.GetData()
		);

		if (bSequenceWeightsAvailable && SequenceIdx != PrevSequenceIdx)
		{
			// Reset pose weights to remove any extraneous sequence contributions for the next iteration
			AccumulatedWeightsMap = InitialWeightsMap;
			PrevSequenceIdx = SequenceIdx;
		}
		
		if (PoseDissimilarity < BestPoseDissimilarity)
		{
			BestPoseDissimilarity = PoseDissimilarity;
			BestPoseIdx = PoseIdx;
		}
	}

	ensure(BestPoseIdx != INDEX_NONE);

	Result.Dissimilarity = BestPoseDissimilarity;
	Result.PoseIdx = BestPoseIdx;
	// Result.TimeOffsetSeconds is set by caller

	return Result;
}

FSearchResult Search(const UAnimSequenceBase* Sequence, TArrayView<const float> Query, FDebugDrawParams DebugDrawParams)
{
	const UPoseSearchSequenceMetaData* MetaData = Sequence ? Sequence->FindMetaDataByClass<UPoseSearchSequenceMetaData>() : nullptr;
	if (!MetaData || !MetaData->IsValidForSearch())
	{
		return FSearchResult();
	}

	const FPoseSearchIndex& SearchIndex = MetaData->SearchIndex;

	FSearchResult Result = Search(SearchIndex, Query);
	if (!Result.IsValid())
	{
		return Result;
	}

	const FFloatInterval SamplingRange = GetEffectiveSamplingRange(Sequence, MetaData->SamplingRange);
	Result.TimeOffsetSeconds = SamplingRange.Min + (SearchIndex.Schema->SamplingInterval * Result.PoseIdx);

	// Do debug visualization
	DebugDrawParams.SequenceMetaData = MetaData;
	DebugDrawParams.Query = Query;
	DebugDrawParams.HighlightPoseIdx = Result.PoseIdx;
	Draw(DebugDrawParams);

	return Result;
}

FDbSearchResult Search(const UPoseSearchDatabase* Database, TArrayView<const float> Query, const FPoseSearchBiasWeightsContext* BiasWeightsContext, FDebugDrawParams DebugDrawParams)
{
	if (!ensure(Database && Database->IsValidForSearch()))
	{
		return FDbSearchResult();
	}

	const FPoseSearchIndex& SearchIndex = Database->SearchIndex;

	FDbSearchResult Result = Search(SearchIndex, Query, BiasWeightsContext);
	if (!Result.IsValid())
	{
		return FDbSearchResult();
	}

	int32 DbSequenceIdx = Database->FindSequenceForPose(Result.PoseIdx);
	if (DbSequenceIdx == INDEX_NONE)
	{
		return FDbSearchResult();
	}
	
	const FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[DbSequenceIdx];
	const FFloatInterval SamplingRange = GetEffectiveSamplingRange(DbSequence.Sequence, DbSequence.SamplingRange);

	Result.DbSequenceIdx = DbSequenceIdx;
	Result.TimeOffsetSeconds = SamplingRange.Min + SearchIndex.Schema->SamplingInterval * (Result.PoseIdx - DbSequence.FirstPoseIdx);

	// Do debug visualization
	DebugDrawParams.Database = Database;
	DebugDrawParams.Query = Query;
	DebugDrawParams.HighlightPoseIdx = Result.PoseIdx;
	Draw(DebugDrawParams);

	return Result;
}

float ComparePoses(const FPoseSearchIndex& SearchIndex, int32 PoseIdx, TArrayView<const float> Query, const FPoseSearchBiasWeightsContext* BiasWeightsContext)
{
	TArrayView<const float> PoseValues = SearchIndex.GetPoseValues(PoseIdx);
	check(PoseValues.Num() == Query.Num());

	bool bBiasWeightContextAvailable = false;

	TArray<float> Weights;
	DefaultInitializeWeights(BiasWeightsContext, Query.Num(), Weights, bBiasWeightContextAvailable);

	if (bBiasWeightContextAvailable)
	{
		// Apply the per-sequence bias weights if present on the sequence metadata
		const int32 SequenceIdx = BiasWeightsContext->Database->FindSequenceForPose(PoseIdx);
		const FPoseSearchDatabaseSequence& SequenceEntry = BiasWeightsContext->Database->Sequences[SequenceIdx];

		if (SequenceEntry.BiasWeights.IsInitialized())
		{
			const auto& SequenceWeights = SequenceEntry.BiasWeights.Weights;
			Eigen::Map<Eigen::ArrayXf>(Weights.GetData(), Weights.Num()) *= Eigen::Map<const Eigen::ArrayXf>(SequenceWeights.GetData(), SequenceWeights.Num());
		}
	}

	return CompareFeatureVectors(PoseValues.Num(), PoseValues.GetData(), Query.GetData(), Weights.GetData());
}


//////////////////////////////////////////////////////////////////////////
// FModule

class FModule : public IModuleInterface, public UE::Anim::IPoseSearchProvider
{
public: // IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public: // IPoseSearchProvider
	virtual UE::Anim::IPoseSearchProvider::FSearchResult Search(const FAnimationBaseContext& GraphContext, const UAnimSequenceBase* Sequence) override;
};

void FModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(UE::Anim::IPoseSearchProvider::ModularFeatureName, this);
}

void FModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(UE::Anim::IPoseSearchProvider::ModularFeatureName, this);
}

UE::Anim::IPoseSearchProvider::FSearchResult FModule::Search(const FAnimationBaseContext& GraphContext, const UAnimSequenceBase* Sequence)
{
	UE::Anim::IPoseSearchProvider::FSearchResult ProviderResult;

	const UPoseSearchSequenceMetaData* MetaData = Sequence ? Sequence->FindMetaDataByClass<UPoseSearchSequenceMetaData>() : nullptr;
	if (!MetaData || !MetaData->IsValidForSearch())
	{
		return ProviderResult;
	}

	IPoseHistoryProvider* PoseHistoryProvider = GraphContext.GetMessage<IPoseHistoryProvider>();
	if (!PoseHistoryProvider)
	{
		return ProviderResult;
	}

	FPoseHistory& PoseHistory = PoseHistoryProvider->GetPoseHistory();
	FPoseSearchFeatureVectorBuilder& QueryBuilder = PoseHistory.GetQueryBuilder();

	QueryBuilder.Init(MetaData->Schema);
	if (!QueryBuilder.TrySetPoseFeatures(&PoseHistory))
	{
		return ProviderResult;
	}

	QueryBuilder.Normalize(MetaData->SearchIndex);

	::UE::PoseSearch::FSearchResult Result = ::UE::PoseSearch::Search(Sequence, QueryBuilder.GetNormalizedValues());

	ProviderResult.Dissimilarity = Result.Dissimilarity;
	ProviderResult.PoseIdx = Result.PoseIdx;
	ProviderResult.TimeOffsetSeconds = Result.TimeOffsetSeconds;
	return ProviderResult;
}

}} // namespace UE::PoseSearch

IMPLEMENT_MODULE(UE::PoseSearch::FModule, PoseSearch)