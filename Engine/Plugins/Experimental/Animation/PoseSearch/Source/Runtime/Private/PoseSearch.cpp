// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearch.h"

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

IMPLEMENT_ANIMGRAPH_MESSAGE(UE::PoseSearch::IPoseHistoryProvider);

namespace UE { namespace PoseSearch {

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

static float CompareFeatureVectors(int32 NumValues, const float* A, const float* B)
{
	float Dissimilarity = 0.0f;

	for (int32 ValueIdx = 0; ValueIdx != NumValues; ++ValueIdx)
	{
		float Diff = A[ValueIdx] - B[ValueIdx];
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


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorLayout

void FPoseSearchFeatureVectorLayout::Init()
{
	uint32 FloatCount = 0;

	for (FPoseSearchFeatureDesc& Feature : Features)
	{
		Feature.ValueOffset = FloatCount;
		FloatCount += UE::PoseSearch::GetFeatureTypeTraits(Feature.Type).NumFloats;
	}

	NumFloats = FloatCount;
}

void FPoseSearchFeatureVectorLayout::Reset()
{
	Features.Reset();
	NumFloats = 0;
}

bool FPoseSearchFeatureVectorLayout::IsValid(int32 MaxNumBones) const
{
	if (NumFloats == 0.0f)
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


//////////////////////////////////////////////////////////////////////////
// UPoseSearchSchema

void UPoseSearchSchema::PreSave(const class ITargetPlatform* TargetPlatform)
{
	SampleRate = FMath::Clamp(SampleRate, 1, 60);
	SamplingInterval = 1.0f / SampleRate;

	PoseSampleTimes.Sort(TLess<>());
	TrajectorySampleTimes.Sort(TLess<>());
	TrajectorySampleDistances.Sort(TLess<>());

	ConvertTimesToOffsets(PoseSampleTimes, PoseSampleOffsets);
	ConvertTimesToOffsets(TrajectorySampleTimes, TrajectorySampleOffsets);

	GenerateLayout();
	ResolveBoneReferences();

	Super::PreSave(TargetPlatform);
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

	for (int32 TrajectoryTimeSubsampleIdx = 0; TrajectoryTimeSubsampleIdx != TrajectorySampleOffsets.Num(); ++TrajectoryTimeSubsampleIdx)
	{
		FPoseSearchFeatureDesc Element;
		Element.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
		Element.SubsampleIdx = TrajectoryTimeSubsampleIdx;
		Element.Domain = EPoseSearchFeatureDomain::Time;

		if (bUseTrajectoryPositions)
		{
			Element.Type = EPoseSearchFeatureType::Position;
			Layout.Features.Add(Element);
		}

		if (bUseTrajectoryVelocities)
		{
			Element.Type = EPoseSearchFeatureType::LinearVelocity;
			Layout.Features.Add(Element);
		}
	}

 	for (int32 TrajectoryDistSubsampleIdx = 0; TrajectoryDistSubsampleIdx != TrajectorySampleDistances.Num(); ++TrajectoryDistSubsampleIdx)
 	{
 		FPoseSearchFeatureDesc Element;
 		Element.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
 		Element.SubsampleIdx = TrajectoryDistSubsampleIdx;
 		Element.Domain = EPoseSearchFeatureDomain::Distance;

		if (bUseTrajectoryPositions)
		{
			Element.Type = EPoseSearchFeatureType::Position;
			Layout.Features.Add(Element);
		}

		if (bUseTrajectoryVelocities)
		{
			Element.Type = EPoseSearchFeatureType::LinearVelocity;
			Layout.Features.Add(Element);
		}
 	}

	for (int32 PoseSubsampleIdx = 0; PoseSubsampleIdx != PoseSampleOffsets.Num(); ++PoseSubsampleIdx)
	{
		FPoseSearchFeatureDesc Element;
		Element.SubsampleIdx = PoseSubsampleIdx;
		Element.Domain = EPoseSearchFeatureDomain::Time;

		for (int32 SchemaBoneIdx = 0; SchemaBoneIdx != Bones.Num(); ++SchemaBoneIdx)
		{
			Element.SchemaBoneIdx = SchemaBoneIdx;
			if (bUseBonePositions)
			{
				Element.Type = EPoseSearchFeatureType::Position;
				Layout.Features.Add(Element);
			}

			if (bUseBoneVelocities)
			{
				Element.Type = EPoseSearchFeatureType::LinearVelocity;
				Layout.Features.Add(Element);
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

void UPoseSearchSchema::ConvertTimesToOffsets(TArrayView<const float> SampleTimes, TArray<int32>& OutSampleOffsets)
{
	OutSampleOffsets.SetNum(SampleTimes.Num());

	for (int32 Idx = 0; Idx != SampleTimes.Num(); ++Idx)
	{
		OutSampleOffsets[Idx] = FMath::RoundToInt(SampleTimes[Idx] * SampleRate);
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


//////////////////////////////////////////////////////////////////////////
// UPoseSearchSequenceMetaData

void UPoseSearchSequenceMetaData::PreSave(const class ITargetPlatform* TargetPlatform)
{
	SearchIndex.Reset();

	if (IsValidForIndexing())
	{
		UObject* Outer = GetOuter();
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Outer))
		{
			UE::PoseSearch::BuildIndex(Sequence, this);
		}
	}

	Super::PreSave(TargetPlatform);
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
	return Schema && Schema->IsValid() && !Sequences.IsEmpty();
}

bool UPoseSearchDatabase::IsValidForSearch() const
{
	return IsValidForIndexing() && SearchIndex.IsValid();
}

void UPoseSearchDatabase::PreSave(const class ITargetPlatform* TargetPlatform)
{
	SearchIndex.Reset();

	if (IsValidForIndexing())
	{
		UE::PoseSearch::BuildIndex(this);
	}

	Super::PreSave(TargetPlatform);
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
	NumFeaturesAdded = 0;
	FeaturesAdded.Init(false, Schema->Layout.Features.Num());
}

void FPoseSearchFeatureVectorBuilder::SetTransform(FPoseSearchFeatureDesc Element, const FTransform& Transform)
{
	SetPosition(Element, Transform.GetTranslation());
	SetRotation(Element, Transform.GetRotation());
}

void FPoseSearchFeatureVectorBuilder::SetTransformDerivative(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
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

bool FPoseSearchFeatureVectorBuilder::SetPoseFeatures(UE::PoseSearch::FPoseHistory* History)
{
	check(Schema && Schema->IsValid());
	check(History);

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->PoseSampleOffsets.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		int32 Offset = Schema->PoseSampleOffsets[SchemaSubsampleIdx];
		float TimeDelta = -Offset * Schema->SamplingInterval;

		if (!History->SamplePose(TimeDelta, Schema->Skeleton->GetReferenceSkeleton(), Schema->BoneIndicesWithParents))
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
			SetTransformDerivative(Feature, Transform, PrevTransform, History->GetSampleInterval());
		}
	}

	return true;
}

bool FPoseSearchFeatureVectorBuilder::SetPastTrajectoryFeatures(UE::PoseSearch::FPoseHistory* History)
{
	check(Schema && Schema->IsValid());
	check(History);

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->TrajectorySampleOffsets.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		int32 SubsampleIndex = Schema->TrajectorySampleOffsets[SchemaSubsampleIdx];
		if (SubsampleIndex >= 0)
		{
			break;
		}

		float SecondsAgo = -SubsampleIndex * Schema->SamplingInterval;
		FTransform WorldComponentTransform;
		if (!History->SampleRoot(SecondsAgo, &WorldComponentTransform))
		{
			return false;
		}

		FTransform WorldPrevComponentTransform;
		if (!History->SampleRoot(SecondsAgo + History->GetSampleInterval(), &WorldPrevComponentTransform))
		{
			return false;
		}

		SetTransform(Feature, WorldComponentTransform);
		SetTransformDerivative(Feature, WorldComponentTransform, WorldPrevComponentTransform, History->GetSampleInterval());
	}

	return true;
}

void FPoseSearchFeatureVectorBuilder::Copy(TArrayView<const float> FeatureVector)
{
	check(FeatureVector.Num() == Values.Num());
	FMemory::Memcpy(Values.GetData(), FeatureVector.GetData(), FeatureVector.GetTypeSize() * FeatureVector.Num());
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

bool FPoseSearchFeatureVectorBuilder::IsComplete() const
{
	return NumFeaturesAdded == Schema->Layout.Features.Num();
}

bool FPoseSearchFeatureVectorBuilder::IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const
{
	return IsInitialized() && (Schema == OtherBuilder.Schema);
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

bool FPoseHistory::SampleLocalPose(float SecondsAgo, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose)
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

bool FPoseHistory::SamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones)
{
	// Compute local space pose at requested time
	bool bSampled = SampleLocalPose(SecondsAgo, RequiredBones, SampledLocalPose);

	// Compute local space pose one sample interval in the past
	bSampled = bSampled && SampleLocalPose(SecondsAgo + GetSampleInterval(), RequiredBones, SampledPrevLocalPose);

	// Convert local to component space
	if (bSampled)
	{
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledLocalPose, SampledComponentPose);
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledPrevLocalPose, SampledPrevComponentPose);
	}

	return bSampled;
}

bool FPoseHistory::SampleRoot(float SecondsAgo, FTransform* OutTransform) const
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

		const float SampleInterval = GetSampleInterval();

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

float FPoseHistory::GetSampleInterval() const
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

bool FDebugDrawParams::CanDraw () const
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
	} Input;

	struct FOutput
	{
		TArray<FTransform> ComponentSpacePose;		// Indexed by SampleIdx * NumBones + SchemaBoneIdx
		TArray<FTransform> LocalRootMotion;			// Indexed by SampleIdx
		TArray<FTransform> AccumulatedRootMotion;	// Indexed by SampleIdx
		TArray<float> AccumulatedRootDistance;		// Indexed by SampleIdx

		int32 TotalSamples = 0;
	} Output;

	void Reset();
	void Init(const FInput& Input);
	void Process();

	struct FWrappedSampleIndex
	{
		int32 Idx = INDEX_NONE;
		int32 NumCycles = 0;
		bool bClamped = false;
	};
	FWrappedSampleIndex WrapOrClampSubsampleIndex (int32 SampleIdx) const;

private:
	void Reserve();
	void ExtractPoses();
	void ExtractRootMotion();
};

void FSequenceSampler::Init(const FInput& InInput)
{
	check(InInput.Schema);
	check(InInput.Schema->IsValid());
	check(InInput.Sequence);

	Reset();

	Input = InInput;

	const float SequencePlayLength = Input.Sequence->GetPlayLength();
	Output.TotalSamples = FMath::FloorToInt(SequencePlayLength * Input.Schema->SampleRate);

	Reserve();
}

void FSequenceSampler::Reset()
{
	Input = FInput();

	Output.TotalSamples = 0;
	Output.ComponentSpacePose.Reset(0);
	Output.LocalRootMotion.Reset(0);
	Output.AccumulatedRootMotion.Reset(0);
	Output.AccumulatedRootDistance.Reset(0);
}

void FSequenceSampler::Reserve()
{
	Output.ComponentSpacePose.Reserve(Input.Schema->NumBones() * Output.TotalSamples);
	Output.LocalRootMotion.Reserve(Output.TotalSamples);
	Output.AccumulatedRootMotion.Reserve(Output.TotalSamples);
	Output.AccumulatedRootDistance.Reserve(Output.TotalSamples);
}

void FSequenceSampler::Process()
{
	ExtractPoses();
	ExtractRootMotion();
}


FSequenceSampler::FWrappedSampleIndex FSequenceSampler::WrapOrClampSubsampleIndex (int32 SampleIdx) const
{
	FWrappedSampleIndex Result;
	Result.Idx = SampleIdx;
	Result.NumCycles = 0;
	Result.bClamped = false;

	// Wrap the index if this is a loopable sequence
	if (Input.bLoopable)
	{
		if (Result.Idx < 0)
		{
			Result.Idx += Output.TotalSamples;

			while (Result.Idx < 0)
			{
				Result.Idx += Output.TotalSamples;
				++Result.NumCycles;
			}
		}

		while (Result.Idx >= Output.TotalSamples)
		{
			Result.Idx -= Output.TotalSamples;
			++Result.NumCycles;
		}
	}

	// Clamp if we can't loop
	else if (SampleIdx < 0 || SampleIdx >= Output.TotalSamples)
	{
		Result.Idx = FMath::Clamp(SampleIdx, 0, Output.TotalSamples - 1);
		Result.bClamped = true;
	}

	return Result;
}

void FSequenceSampler::ExtractPoses()
{
	if (Input.Schema->Bones.IsEmpty())
	{
		return;
	}

	USkeleton* Skeleton = Input.Sequence->GetSkeleton();
	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Input.Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Skeleton);

	FCompactPose Pose;
	Pose.SetBoneContainer(&BoneContainer);
	FCSPose<FCompactPose> ComponentSpacePose;

	FBlendedCurve UnusedCurve;
	FStackCustomAttributes UnusedAttributes;

	FAnimExtractContext ExtractionCtx;
	// ExtractionCtx.PoseCurves is intentionally left empty
	// ExtractionCtx.BonesRequired is unused by UAnimSequence::GetAnimationPose
	ExtractionCtx.bExtractRootMotion = true;

	FAnimationPoseData AnimPoseData(Pose, UnusedCurve, UnusedAttributes);
	for (int32 SampleIdx = 0; SampleIdx != Output.TotalSamples; ++SampleIdx)
	{
		const float CurrentTime = SampleIdx * Input.Schema->SamplingInterval;

		ExtractionCtx.CurrentTime = CurrentTime;
		Input.Sequence->GetAnimationPose(AnimPoseData, ExtractionCtx);
		ComponentSpacePose.InitPose(Pose);

		for (int32 BoneIndex : Input.Schema->BoneIndices)
		{
			FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
			const FTransform& Transform = ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex);
			Output.ComponentSpacePose.Add(Transform);
		}
	}
}

void FSequenceSampler::ExtractRootMotion()
{
	double AccumulatedRootDistance = 0.0;
	FTransform AccumulatedRootMotion = FTransform::Identity;
	for (int32 SampleIdx = 0; SampleIdx != Output.TotalSamples; ++SampleIdx)
	{
		const float CurrentTime = SampleIdx * Input.Schema->SamplingInterval;

		FTransform LocalRootMotion = Input.Sequence->ExtractRootMotion(CurrentTime, Input.Schema->SamplingInterval, false /*!allowLooping*/);
		Output.LocalRootMotion.Add(LocalRootMotion);

		AccumulatedRootMotion = LocalRootMotion * AccumulatedRootMotion;
		AccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		Output.AccumulatedRootMotion.Add(AccumulatedRootMotion);
		Output.AccumulatedRootDistance.Add((float)AccumulatedRootDistance);
	}
}


//////////////////////////////////////////////////////////////////////////
// FSequenceIndexer

class FSequenceIndexer
{
public:

	struct FInput
	{
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

	struct FSubsample
	{
		const FSequenceSampler* Sampler = nullptr;
		int32 AbsoluteSampleIdx = INDEX_NONE;
		FTransform AccumulatedRootMotion;
		float AccumulatedRootDistance = 0.0f;
	};

	FSubsample ResolveSubsample(int32 MainSubsampleIdx) const;

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
	Output.LastIndexedSample = FMath::Max(0, FMath::FloorToInt(SamplingRange.Max * Input.Schema->SampleRate) - 1);
	Output.NumIndexedPoses = Output.LastIndexedSample - Output.FirstIndexedSample + 1;
	Reserve();
}

void FSequenceIndexer::Process()
{
	for (int32 SampleIdx = Output.FirstIndexedSample; SampleIdx <= Output.LastIndexedSample; ++SampleIdx)
	{
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

FSequenceIndexer::FSubsample FSequenceIndexer::ResolveSubsample(int32 MainSubsampleIdx) const
{
	// MainSubsampleIdx is relative to the samples in the main sequence. With future subsampling,
	// SampleIdx may be greater than the  number of samples in the main sequence. For past subsampling,
	// SampleIdx may be negative. This function handles those edge cases by wrapping within the main
	// sequence if it is loopable, or by indexing into the lead-in or follow-up sequences which themselves
	// may or may not be loopable.
	// The relative SampleIdx may be multiple cycles away, so this function also handles the math for
	// accumulating multiple cycles of root motion.
	// It returns an absolute index into the relevant sample data and root motion info.

	FSubsample Subsample;

	FTransform RootMotionLast = FTransform::Identity;
	FTransform RootMotionInitial = FTransform::Identity;

	float RootDistanceLast = 0.0f;
	float RootDistanceInitial = 0.0f;

	FSequenceSampler::FWrappedSampleIndex MainSample = Input.MainSequence->WrapOrClampSubsampleIndex(MainSubsampleIdx);
	FSequenceSampler::FWrappedSampleIndex EffectiveSample;

	// Use the lead in anim if we had to clamp to the beginning of the main anim
	if (MainSample.bClamped && (MainSubsampleIdx < 0) && Input.LeadInSequence)
	{
		EffectiveSample = Input.LeadInSequence->WrapOrClampSubsampleIndex(MainSubsampleIdx);

		Subsample.Sampler = Input.LeadInSequence;
		Subsample.AbsoluteSampleIdx = EffectiveSample.Idx;

		RootMotionInitial = FTransform::Identity;
		RootDistanceInitial = 0.0f;

		RootMotionLast = Input.LeadInSequence->Output.AccumulatedRootMotion.Last();
		RootDistanceLast = Input.LeadInSequence->Output.AccumulatedRootDistance.Last();
	}

	// Use the follow up anim if we had to clamp to the end of the main anim
	if (MainSample.bClamped && (MainSubsampleIdx >= Input.MainSequence->Output.TotalSamples) && Input.FollowUpSequence)
	{
		EffectiveSample = Input.FollowUpSequence->WrapOrClampSubsampleIndex(MainSubsampleIdx - Input.MainSequence->Output.TotalSamples);

		Subsample.Sampler = Input.FollowUpSequence;
		Subsample.AbsoluteSampleIdx = EffectiveSample.Idx;

		RootMotionInitial = Input.MainSequence->Output.AccumulatedRootMotion.Last();
		RootDistanceInitial = Input.MainSequence->Output.AccumulatedRootDistance.Last();

		RootMotionLast = Input.FollowUpSequence->Output.AccumulatedRootMotion.Last();
		RootDistanceLast = Input.FollowUpSequence->Output.AccumulatedRootDistance.Last();
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (EffectiveSample.Idx == INDEX_NONE)
	{
		EffectiveSample = MainSample;

		Subsample.Sampler = Input.MainSequence;
		Subsample.AbsoluteSampleIdx = EffectiveSample.Idx;

		RootMotionInitial = FTransform::Identity;
		RootDistanceInitial = 0.0f;

		RootMotionLast = Input.MainSequence->Output.AccumulatedRootMotion.Last();
		RootDistanceLast = Input.MainSequence->Output.AccumulatedRootDistance.Last();
	}

	// Determine how to accumulate motion for every cycle of the anim. If the sample
	// had to be clamped, this motion will end up not getting applied below.
	// Also invert the accumulation direction if the requested sample was wrapped backwards.
	FTransform RootMotionPerCycle = RootMotionLast;
	float RootDistancePerCycle = RootDistanceLast;
	if (MainSubsampleIdx < 0)
	{
		RootMotionPerCycle = RootMotionPerCycle.Inverse();
		RootDistancePerCycle *= -1;
	}

	// Find the remaining motion deltas after wrapping
	FTransform RootMotionRemainder = Subsample.Sampler->Output.AccumulatedRootMotion[EffectiveSample.Idx];
	float RootDistanceRemainder = Subsample.Sampler->Output.AccumulatedRootDistance[EffectiveSample.Idx];

	// Invert motion deltas if we wrapped backwards
	if (MainSubsampleIdx < 0)
	{
		RootMotionRemainder.SetToRelativeTransform(RootMotionLast);
		RootDistanceRemainder = -(RootDistanceLast - RootDistanceRemainder);
	}

	Subsample.AccumulatedRootMotion = RootMotionInitial;
	Subsample.AccumulatedRootDistance = RootDistanceInitial;

	// Note if the sample was clamped, no motion will be applied here because NumCycles will be zero
	int32 CyclesRemaining = EffectiveSample.NumCycles;
	while (CyclesRemaining--)
	{
		Subsample.AccumulatedRootMotion *= RootMotionPerCycle;
		Subsample.AccumulatedRootDistance += RootDistancePerCycle;
	}

	Subsample.AccumulatedRootMotion *= RootMotionRemainder;
	Subsample.AccumulatedRootDistance += RootDistanceRemainder;

	return Subsample;
}

void FSequenceIndexer::AddPoseFeatures(int32 SampleIdx)
{
	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	const int32 NumBones = Input.Schema->NumBones();

	FSequenceIndexer::FSubsample OriginSample = ResolveSubsample(SampleIdx);
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Input.Schema->PoseSampleOffsets.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		const int32 SubsampleIdx = SampleIdx + Input.Schema->PoseSampleOffsets[SchemaSubsampleIdx];

		FSequenceIndexer::FSubsample Subsample = ResolveSubsample(SubsampleIdx);
		FSequenceIndexer::FSubsample SubsamplePrev = ResolveSubsample(SubsampleIdx - 1);

		FTransform SubsampleRoot = Subsample.AccumulatedRootMotion;
		SubsampleRoot.SetToRelativeTransform(OriginSample.AccumulatedRootMotion);

		for (int32 SchemaBoneIndex = 0; SchemaBoneIndex != NumBones; ++SchemaBoneIndex)
		{
			Feature.SchemaBoneIdx = SchemaBoneIndex;

			int32 BoneSampleIdx = NumBones * Subsample.AbsoluteSampleIdx + SchemaBoneIndex;
			int32 BonePrevSampleIdx = NumBones * SubsamplePrev.AbsoluteSampleIdx  + SchemaBoneIndex;

			FTransform BoneInComponentSpace = Subsample.Sampler->Output.ComponentSpacePose[BoneSampleIdx];
			FTransform BonePrevInComponentSpace = SubsamplePrev.Sampler->Output.ComponentSpacePose[BonePrevSampleIdx];

			FTransform BoneInSampleSpace = BoneInComponentSpace * SubsampleRoot;
			FTransform BonePrevInSampleSpace = BonePrevInComponentSpace * SubsampleRoot;

			FeatureVector.SetTransform(Feature, BoneInSampleSpace);
			FeatureVector.SetTransformDerivative(Feature, BoneInSampleSpace, BonePrevInSampleSpace, Input.Schema->SamplingInterval);
		}
	}
}

void FSequenceIndexer::AddTrajectoryTimeFeatures(int32 SampleIdx)
{
	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	FSequenceIndexer::FSubsample OriginSample = ResolveSubsample(SampleIdx);
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Input.Schema->TrajectorySampleOffsets.Num(); ++SchemaSubsampleIdx)
	{
		Feature.SubsampleIdx = SchemaSubsampleIdx;

		const int32 SubsampleIdx = SampleIdx + Input.Schema->TrajectorySampleOffsets[SchemaSubsampleIdx];

		FSequenceIndexer::FSubsample Subsample = ResolveSubsample(SubsampleIdx);
		FTransform SubsampleRoot = Subsample.AccumulatedRootMotion;
		SubsampleRoot.SetToRelativeTransform(OriginSample.AccumulatedRootMotion);

		FSequenceIndexer::FSubsample SubsamplePrev = ResolveSubsample(SubsampleIdx - 1);
		FTransform SubsamplePrevRoot = SubsamplePrev.AccumulatedRootMotion;
		SubsamplePrevRoot.SetToRelativeTransform(OriginSample.AccumulatedRootMotion);

		FeatureVector.SetTransform(Feature, SubsampleRoot);
		FeatureVector.SetTransformDerivative(Feature, SubsampleRoot, SubsamplePrevRoot, Input.Schema->SamplingInterval);
	}
}

void FSequenceIndexer::AddTrajectoryDistanceFeatures(int32 SampleIdx)
{
	// This function needs to be rewritten to work with the updated sampler
	// and lead-in/follow-up anims

//	FPoseSearchFeatureDesc Feature;
//	Feature.Domain = EPoseSearchFeatureDomain::Distance;
//	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
//
//	FSequenceIndexer::FSampleRef OriginSampleRef = ResolveSampleRef(SampleIdx);
//
//	for (int32 SubsampleIdx = 0; SubsampleIdx != Input.Schema->TrajectorySampleDistances.Num(); ++SubsampleIdx)
//	{
//		Feature.SubsampleIdx = SubsampleIdx;
//
//		const float TrajectoryDistance = Input.Schema->TrajectorySampleDistances[SubsampleIdx];
//		const float SampleAccumulatedRootDistance = TrajectoryDistance + AccumulatedRootDistances[SampleIdx];
//
//		int32 LowerBoundSampleIdx = Algo::LowerBound(AccumulatedRootDistances, SampleAccumulatedRootDistance);
//
//		// @@@ Add extrapolation. Clamp for now
//		int32 PrevSampleIdx = FMath::Clamp(LowerBoundSampleIdx - 1, 0, AccumulatedRootDistances.Num() - 1);
//		int32 NextSampleIdx = FMath::Clamp(LowerBoundSampleIdx, 0, AccumulatedRootDistances.Num() - 1);
//
//		const float PrevSampleDistance = AccumulatedRootDistances[PrevSampleIdx];
//		const float NextSampleDistance = AccumulatedRootDistances[NextSampleIdx];
//
//		FTransform PrevRootInSampleSpace = AccumulatedRootMotion[PrevSampleIdx];
//		PrevRootInSampleSpace.SetToRelativeTransform(SampleSpaceOrigin);
//
//		FTransform NextRootInSampleSpace = AccumulatedRootMotion[NextSampleIdx];
//		NextRootInSampleSpace.SetToRelativeTransform(SampleSpaceOrigin);
//		
//		float Alpha = FMath::GetRangePct(PrevSampleDistance, NextSampleDistance, SampleAccumulatedRootDistance);
//		FTransform BlendedRootInSampleSpace;
//		BlendedRootInSampleSpace.Blend(PrevRootInSampleSpace, NextRootInSampleSpace, Alpha);
//
//		FeatureVector.SetTransform(Feature, BlendedRootInSampleSpace);
//	}
}


//////////////////////////////////////////////////////////////////////////
// PoseSearch API

static void DrawTrajectoryFeatures(const FDebugDrawParams& DrawParams, const FFeatureVectorReader& Reader)
{
	const float LifeTime = DrawParams.DefaultLifeTime;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 2;

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	const int32 NumSubsamples = DrawParams.GetSchema()->TrajectorySampleOffsets.Num();

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

	const int32 NumSubsamples = Schema->PoseSampleOffsets.Num();
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
	DrawTrajectoryFeatures(DrawParams, Reader);
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

	for (int32 PoseIdx = StartPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
	{
		Reader.SetValues(SearchIndex->GetPoseValues(PoseIdx));

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
	Indexer.Init(IndexerInput);
	Indexer.Process();

	SequenceMetaData->SearchIndex.Values = Indexer.Output.FeatureVectorTable;
	SequenceMetaData->SearchIndex.NumPoses = Indexer.Output.NumIndexedPoses;
	SequenceMetaData->SearchIndex.Schema = SequenceMetaData->Schema;
	return true;
}

bool BuildIndex(UPoseSearchDatabase* Database)
{
	check(Database);

	if (!Database->IsValidForIndexing())
	{
		return false;
	}

	for (const FPoseSearchDatabaseSequence& DbSequence : Database->Sequences)
	{
		USkeleton* SeqSkeleton = DbSequence.Sequence->GetSkeleton();
		if (!SeqSkeleton || !SeqSkeleton->IsCompatible(Database->Schema->Skeleton))
		{
			return false;
		}
	}

	// Prepare animation sampling tasks
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

	// Sample animations independently
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

	// Join animation data into a single search index
	Database->SearchIndex.Values.Reset(TotalFloats);
	for (const FSequenceIndexer& Indexer : Indexers)
	{
		const FSequenceIndexer::FOutput& Output = Indexer.Output;
		Database->SearchIndex.Values.Append(Output.FeatureVectorTable.GetData(), Output.FeatureVectorTable.Num());
	}

	Database->SearchIndex.NumPoses = TotalPoses;
	Database->SearchIndex.Schema = Database->Schema;
	return true;
}

static FSearchResult Search(const FPoseSearchIndex& SearchIndex, TArrayView<const float> Query)
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

	float BestPoseDissimilarity = MAX_flt;
	int32 BestPoseIdx = INDEX_NONE;

	for (int32 PoseIdx = 0; PoseIdx != SearchIndex.NumPoses; ++PoseIdx)
	{
		const int32 FeatureValueOffset = PoseIdx * SearchIndex.Schema->Layout.NumFloats;

		float PoseDissimilarity = CompareFeatureVectors(
			SearchIndex.Schema->Layout.NumFloats,
			Query.GetData(),
			&SearchIndex.Values[FeatureValueOffset]
		);

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

FDbSearchResult Search(const UPoseSearchDatabase* Database, TArrayView<const float> Query, FDebugDrawParams DebugDrawParams)
{
	if (!ensure(Database && Database->IsValidForSearch()))
	{
		return FDbSearchResult();
	}

	const FPoseSearchIndex& SearchIndex = Database->SearchIndex;

	FDbSearchResult Result = Search(SearchIndex, Query);
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

float ComparePoses(const FPoseSearchIndex& SearchIndex, int32 PoseIdx, TArrayView<const float> Query)
{
	TArrayView<const float> PoseValues = SearchIndex.GetPoseValues(PoseIdx);
	check(PoseValues.Num() == Query.Num());
	return CompareFeatureVectors(PoseValues.Num(), PoseValues.GetData(), Query.GetData());
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
	QueryBuilder.SetPoseFeatures(&PoseHistory);

	::UE::PoseSearch::FSearchResult Result = ::UE::PoseSearch::Search(Sequence, QueryBuilder.GetValues());

	ProviderResult.Dissimilarity = Result.Dissimilarity;
	ProviderResult.PoseIdx = Result.PoseIdx;
	ProviderResult.TimeOffsetSeconds = Result.TimeOffsetSeconds;
	return ProviderResult;
}

}} // namespace UE::PoseSearch

IMPLEMENT_MODULE(UE::PoseSearch::FModule, PoseSearch)