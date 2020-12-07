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

constexpr float DrawDebugLineThickness = 1.0f;
constexpr float DrawDebugPointSize = 3.0f;
constexpr float DrawDebugVelocityScale = 0.1f;
constexpr float DrawDebugArrowSize = 5.0f;
constexpr float DrawDebugSphereSize = 1.0f;
constexpr int32 DrawDebugSphereSegments = 8;
constexpr float DrawDebugSphereLineThickness = 0.2f;

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
	FLinearColor ColorHSV(Lerp * 360.0f, 0.8f, 0.5f, 0.6f);
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
	SamplingInterval = 1.0f / SampleRate;

	PoseSampleOffsets.Sort(TLess<>());
	TrajectorySampleOffsets.Sort(TLess<>());
	TrajectoryDistanceOffsets.Sort(TLess<>());

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

 	for (int32 TrajectoryDistSubsampleIdx = 0; TrajectoryDistSubsampleIdx != TrajectoryDistanceOffsets.Num(); ++TrajectoryDistSubsampleIdx)
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
	if (NextIdx >= Knots.Num())
	{
		return false;
	}

	int32 PrevIdx = NextIdx - 1;

	// Compute alpha between previous and next knots
	float Alpha = FMath::GetMappedRangeValueUnclamped(
		FVector2D(Knots[PrevIdx], Knots[NextIdx]),
		FVector2D(0.0f, 1.0f),
		SecondsAgo);

	TArray<FTransform>& PrevPose = Poses[PrevIdx].LocalTransforms;
	TArray<FTransform>& NextPose = Poses[NextIdx].LocalTransforms;

	// We may not have accumulated enough poses yet
	if (PrevPose.Num() != NextPose.Num())
	{
		return false;
	}

	if (RequiredBones.Num() > PrevPose.Num())
	{
		return false;
	}

	// Lerp between poses by alpha to produce output local pose at requested sample time
	LocalPose = PrevPose;
	FAnimationRuntime::LerpBoneTransforms(
		LocalPose,
		NextPose,
		Alpha,
		RequiredBones);

	return true;
}

bool FPoseHistory::Sample(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones)
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

void FPoseHistory::Update(float SecondsElapsed, const FCompactPose& Pose)
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
		// at or beyond the desired time horizon H so we can fulfill sample requests at t=H. We also
		// want to evenly distribute knots across the entire history buffer so we only push additional
		// poses when enough time has elapsed.

		const float SampleInterval = GetSampleInterval();

		bool bCanEvictOldest = Knots[1] >= TimeHorizon;
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
	CopyCompactToSkeletonPose(Pose, Poses.Last().LocalTransforms);
}

float FPoseHistory::GetSampleInterval() const
{
	return TimeHorizon / Knots.Max();
}


//////////////////////////////////////////////////////////////////////////
// FFeatureVectorBuilder

void FFeatureVectorBuilder::Init(const FPoseSearchFeatureVectorLayout* InLayout, TArrayView<float> Buffer)
{
	check(InLayout);
	check(Buffer.Num() == InLayout->NumFloats);
	Layout = InLayout;
	Values = Buffer;
	ResetFeatures();
}

void FFeatureVectorBuilder::ResetFeatures()
{
	NumFeaturesAdded = 0;
	FeaturesAdded.Init(false, Layout->Features.Num());
}

void FFeatureVectorBuilder::SetTransform(FPoseSearchFeatureDesc Element, const FTransform& Transform)
{
	SetPosition(Element, Transform.GetTranslation());
	SetRotation(Element, Transform.GetRotation());
}

void FFeatureVectorBuilder::SetTransformDerivative(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	SetLinearVelocity(Element, Transform, PrevTransform, DeltaTime);
	SetAngularVelocity(Element, Transform, PrevTransform, DeltaTime);
}

void FFeatureVectorBuilder::SetPosition(FPoseSearchFeatureDesc Element, const FVector& Position)
{
	Element.Type = EPoseSearchFeatureType::Position;
	SetVector(Element, Position);
}

void FFeatureVectorBuilder::SetRotation(FPoseSearchFeatureDesc Element, const FQuat& Rotation)
{
	Element.Type = EPoseSearchFeatureType::Rotation;
	int32 ElementIndex = Layout->Features.Find(Element);
	if (ElementIndex >= 0)
	{
		FVector X = Rotation.GetAxisX();
		FVector Y = Rotation.GetAxisY();

		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

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

void FFeatureVectorBuilder::SetLinearVelocity(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Element.Type = EPoseSearchFeatureType::LinearVelocity;
	FVector LinearVelocity = (Transform.GetTranslation() - PrevTransform.GetTranslation()) / DeltaTime;
	SetVector(Element, LinearVelocity);
}

void FFeatureVectorBuilder::SetAngularVelocity(FPoseSearchFeatureDesc Element, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Element.Type = EPoseSearchFeatureType::AngularVelocity;
	int32 ElementIndex = Layout->Features.Find(Element);
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

		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

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

void FFeatureVectorBuilder::SetVector(FPoseSearchFeatureDesc Element, const FVector& Vector)
{
	int32 ElementIndex = Layout->Features.Find(Element);
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

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

bool FFeatureVectorBuilder::SetPoseFeatures(const UPoseSearchSchema* Schema, FPoseHistory* History)
{
	check(Schema && Schema->IsValid());
	check(History);

	FPoseSearchFeatureDesc Feature;
	Feature.Domain = EPoseSearchFeatureDomain::Time;

	for (int32 SubsampleIdx = 0; SubsampleIdx != Schema->PoseSampleOffsets.Num(); ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		int32 Offset = Schema->PoseSampleOffsets[SubsampleIdx];
		float TimeDelta = -Offset * Schema->SamplingInterval;

		if (!History->Sample(TimeDelta, Schema->Skeleton->GetReferenceSkeleton(), Schema->BoneIndicesWithParents))
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

void FFeatureVectorBuilder::Copy(TArrayView<const float> FeatureVector)
{
	check(FeatureVector.Num() == Values.Num());
	FMemory::Memcpy(Values.GetData(), FeatureVector.GetData(), FeatureVector.GetTypeSize() * FeatureVector.Num());
	NumFeaturesAdded = Layout->Features.Num();
	FeaturesAdded.SetRange(0, FeaturesAdded.Num(), true);
}

bool FFeatureVectorBuilder::IsComplete() const
{
	return NumFeaturesAdded == Layout->Features.Num();
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
// FSequenceIndexer

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
// FSequenceIndexer

class FSequenceIndexer
{
public:
	struct Result
	{
		int32 NumIndexedPoses = 0;
		TArrayView<const float> Values;
	};
	Result Process(const UPoseSearchSchema* Schema, const UAnimSequence* Sequence, FFloatInterval SamplingRange);
	Result GetResult() const;

private:
	void SampleBegin(int32 SampleIdx);
	void SampleEnd();
	void ExtractPoses(const UAnimSequence* Sequence);
	void ExtractRootMotion(const UAnimSequence* Sequence);
	void AddPoseFeatures(int32 SampleIdx);
	void AddTrajectoryTimeFeatures(int32 SampleIdx);
	void AddTrajectoryDistanceFeatures(int32 SampleIdx);

	struct FSampleContext
	{
		TArray<FTransform> ComponentSpacePose;		// Indexed by SampleIdx * NumBones + SchemaBoneIdx
		TArray<FTransform> LocalRootMotion;			// Indexed by SampleIdx
		TArray<FTransform> AccumulatedRootMotion;	// Indexed by SampleIdx
		TArray<float> AccumulatedRootDistance;		// Indexed by SampleIdx

		int32 TotalSamples = 0;
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedSamples = 0;
		int32 NumBones = 0;

		void Reset();
		void Reserve();
	};

	const UPoseSearchSchema* Schema = nullptr;

	TArray<float> Values;
	
	FFeatureVectorBuilder Builder;
	FSampleContext Context;
};

void FSequenceIndexer::FSampleContext::Reset()
{
	TotalSamples = 0;
	FirstIndexedSample = 0;
	LastIndexedSample = 0;
	NumIndexedSamples = 0;
	NumBones = 0;

	ComponentSpacePose.Reset(0);
	LocalRootMotion.Reset(0);
	AccumulatedRootMotion.Reset(0);
	AccumulatedRootDistance.Reset(0);
}

void FSequenceIndexer::FSampleContext::Reserve()
{
	ComponentSpacePose.Reserve(NumBones * TotalSamples);
	LocalRootMotion.Reserve(TotalSamples);
	AccumulatedRootMotion.Reserve(TotalSamples);
	AccumulatedRootDistance.Reserve(TotalSamples);
}

FSequenceIndexer::Result FSequenceIndexer::Process(const UPoseSearchSchema* InSchema, const UAnimSequence* Sequence, FFloatInterval RequestedSamplingRange)
{
	check(InSchema);
	check(Sequence);

	Schema = InSchema;

	USkeleton* Skeleton = Sequence->GetSkeleton();
	check(Skeleton);
	check(Skeleton->IsCompatible(Schema->Skeleton));

	const float SequencePlayLength = Sequence->GetPlayLength();
	const FFloatInterval SamplingRange = GetEffectiveSamplingRange(Sequence, RequestedSamplingRange);

	Context.Reset();
	Context.NumBones = Schema->BoneIndices.Num();
	Context.TotalSamples = FMath::FloorToInt(SequencePlayLength * Schema->SampleRate);
	Context.FirstIndexedSample = FMath::FloorToInt(SamplingRange.Min * Schema->SampleRate);
	Context.LastIndexedSample = FMath::Max(0, FMath::FloorToInt(SamplingRange.Max * Schema->SampleRate) - 1);
	Context.NumIndexedSamples = Context.LastIndexedSample - Context.FirstIndexedSample + 1;
	Context.Reserve();

	Values.SetNumZeroed(Schema->Layout.NumFloats * Context.NumIndexedSamples);

	ExtractPoses(Sequence);
	ExtractRootMotion(Sequence);

	for (int32 SampleIdx = Context.FirstIndexedSample; SampleIdx <= Context.LastIndexedSample; ++SampleIdx)
	{
		SampleBegin(SampleIdx);

		AddPoseFeatures(SampleIdx);
		AddTrajectoryTimeFeatures(SampleIdx);
		AddTrajectoryDistanceFeatures(SampleIdx);

		SampleEnd();
	}

	return GetResult();
}

FSequenceIndexer::Result FSequenceIndexer::GetResult() const
{
	Result Result;
	Result.NumIndexedPoses = Context.NumIndexedSamples;
	Result.Values = Values;
	return Result;
}

void FSequenceIndexer::SampleBegin(int32 SampleIdx)
{
	int32 FirstValueIdx = (SampleIdx - Context.FirstIndexedSample) * Schema->Layout.NumFloats;
	TArrayView<float> FeatureVectorValues = MakeArrayView(&Values[FirstValueIdx], Schema->Layout.NumFloats);
	Builder.Init(&Schema->Layout, FeatureVectorValues);
}

void FSequenceIndexer::SampleEnd()
{
	check(Builder.IsComplete());
}

void FSequenceIndexer::ExtractPoses(const UAnimSequence* Sequence)
{
	if (Schema->Bones.IsEmpty())
	{
		return;
	}

	USkeleton* Skeleton = Sequence->GetSkeleton();
	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Skeleton);

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
	for (int32 SampleIdx = 0; SampleIdx != Context.TotalSamples; ++SampleIdx)
	{
		const float CurrentTime = SampleIdx * Schema->SamplingInterval;

		ExtractionCtx.CurrentTime = CurrentTime;
		Sequence->GetAnimationPose(AnimPoseData, ExtractionCtx);
		ComponentSpacePose.InitPose(Pose);

		for (int32 BoneIndex : Schema->BoneIndices)
		{
			FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
			const FTransform& Transform = ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex);
			Context.ComponentSpacePose.Add(Transform);
		}
	}
}

void FSequenceIndexer::ExtractRootMotion(const UAnimSequence* Sequence)
{
	double AccumulatedRootDistance = 0.0;
	FTransform AccumulatedRootMotion = FTransform::Identity;
	for (int32 SampleIdx = 0; SampleIdx != Context.TotalSamples; ++SampleIdx)
	{
		const float CurrentTime = SampleIdx * Schema->SamplingInterval;

		FTransform LocalRootMotion = Sequence->ExtractRootMotion(CurrentTime, Schema->SamplingInterval, false /*!allowLooping*/);
		Context.LocalRootMotion.Add(LocalRootMotion);

		AccumulatedRootMotion = LocalRootMotion * AccumulatedRootMotion;
		AccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		Context.AccumulatedRootMotion.Add(AccumulatedRootMotion);
		Context.AccumulatedRootDistance.Add((float)AccumulatedRootDistance);
	}
}

void FSequenceIndexer::AddPoseFeatures(int32 SampleIdx)
{
	FPoseSearchFeatureDesc CurrentElement;
	CurrentElement.Domain = EPoseSearchFeatureDomain::Time;

	FTransform SampleSpaceOrigin = Context.AccumulatedRootMotion[SampleIdx];
	
	for (int32 SchemaSubsampleIdx = 0; SchemaSubsampleIdx != Schema->PoseSampleOffsets.Num(); ++SchemaSubsampleIdx)
	{
		CurrentElement.SubsampleIdx = SchemaSubsampleIdx;

		const int32 SampleOffset = Schema->PoseSampleOffsets[SchemaSubsampleIdx];
		const int32 SubsampleIdx = FMath::Clamp(SampleIdx + SampleOffset, 0, Context.AccumulatedRootMotion.Num() - 1);

		FTransform SubsampleRoot = Context.AccumulatedRootMotion[SubsampleIdx];
		SubsampleRoot.SetToRelativeTransform(SampleSpaceOrigin);

		for (int32 SchemaBoneIndex = 0; SchemaBoneIndex != Context.NumBones; ++SchemaBoneIndex)
		{
			CurrentElement.SchemaBoneIdx = SchemaBoneIndex;

			int32 BoneSampleIdx = Context.NumBones * (SampleIdx + SampleOffset) + SchemaBoneIndex;
			int32 BonePrevSampleIdx = Context.NumBones * (SampleIdx - 1 + SampleOffset) + SchemaBoneIndex;
			
			// @@@Add extrapolation. Clamp for now
			BoneSampleIdx = FMath::Clamp(BoneSampleIdx, 0, Context.ComponentSpacePose.Num() - 1);
			BonePrevSampleIdx = FMath::Clamp(BonePrevSampleIdx, 0, Context.ComponentSpacePose.Num() - 1);

			FTransform BoneInSampleSpace = Context.ComponentSpacePose[BoneSampleIdx] * SubsampleRoot;
			FTransform BonePrevInSampleSpace = Context.ComponentSpacePose[BonePrevSampleIdx] * SubsampleRoot;

			Builder.SetTransform(CurrentElement, BoneInSampleSpace);
			Builder.SetTransformDerivative(CurrentElement, BoneInSampleSpace, BonePrevInSampleSpace, Schema->SamplingInterval);
		}
	}
}

void FSequenceIndexer::AddTrajectoryTimeFeatures(int32 SampleIdx)
{
	FPoseSearchFeatureDesc CurrentElement;
	CurrentElement.Domain = EPoseSearchFeatureDomain::Time;
	CurrentElement.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	FTransform SampleSpaceOrigin = Context.AccumulatedRootMotion[SampleIdx];

	for (int32 SubsampleIdx = 0; SubsampleIdx != Schema->TrajectorySampleOffsets.Num(); ++SubsampleIdx)
	{
		CurrentElement.SubsampleIdx = SubsampleIdx;

		int32 RootMotionIdx = SampleIdx + Schema->TrajectorySampleOffsets[SubsampleIdx];
		int32 RootMotionPrevIdx = RootMotionIdx - 1;

		// @@@ Add extrapolation. Clamp for now
		RootMotionIdx = FMath::Clamp(RootMotionIdx, 0, Context.AccumulatedRootMotion.Num() - 1);
		RootMotionPrevIdx = FMath::Clamp(RootMotionPrevIdx, 0, Context.AccumulatedRootMotion.Num() - 1);

		FTransform SubsampleRoot = Context.AccumulatedRootMotion[RootMotionIdx];
		SubsampleRoot.SetToRelativeTransform(SampleSpaceOrigin);

		FTransform SubsamplePrevRoot = Context.AccumulatedRootMotion[RootMotionPrevIdx];
		SubsamplePrevRoot.SetToRelativeTransform(SampleSpaceOrigin);

		Builder.SetTransform(CurrentElement, SubsampleRoot);
		Builder.SetTransformDerivative(CurrentElement, SubsampleRoot, SubsamplePrevRoot, Schema->SamplingInterval);
	}
}

void FSequenceIndexer::AddTrajectoryDistanceFeatures(int32 SampleIdx)
{
	FPoseSearchFeatureDesc CurrentElement;
	CurrentElement.Domain = EPoseSearchFeatureDomain::Distance;
	CurrentElement.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;

	TArrayView<const float> AccumulatedRootDistances = Context.AccumulatedRootDistance;

	FTransform SampleSpaceOrigin = Context.AccumulatedRootMotion[SampleIdx];

	for (int32 SubsampleIdx = 0; SubsampleIdx != Schema->TrajectoryDistanceOffsets.Num(); ++SubsampleIdx)
	{
		CurrentElement.SubsampleIdx = SubsampleIdx;

		const float TrajectoryDistance = Schema->TrajectoryDistanceOffsets[SubsampleIdx];
		const float SampleAccumulatedRootDistance = TrajectoryDistance + AccumulatedRootDistances[SampleIdx];

		int32 LowerBoundSampleIdx = Algo::LowerBound(AccumulatedRootDistances, SampleAccumulatedRootDistance);

		// @@@ Add extrapolation. Clamp for now
		int32 PrevSampleIdx = FMath::Clamp(LowerBoundSampleIdx - 1, 0, AccumulatedRootDistances.Num() - 1);
		int32 NextSampleIdx = FMath::Clamp(LowerBoundSampleIdx, 0, AccumulatedRootDistances.Num() - 1);

		const float PrevSampleDistance = AccumulatedRootDistances[PrevSampleIdx];
		const float NextSampleDistance = AccumulatedRootDistances[NextSampleIdx];

		FTransform PrevRootInSampleSpace = Context.AccumulatedRootMotion[PrevSampleIdx];
		PrevRootInSampleSpace.SetToRelativeTransform(SampleSpaceOrigin);

		FTransform NextRootInSampleSpace = Context.AccumulatedRootMotion[NextSampleIdx];
		NextRootInSampleSpace.SetToRelativeTransform(SampleSpaceOrigin);
		
		float Alpha = FMath::GetRangePct(PrevSampleDistance, NextSampleDistance, SampleAccumulatedRootDistance);
		FTransform BlendedRootInSampleSpace;
		BlendedRootInSampleSpace.Blend(PrevRootInSampleSpace, NextRootInSampleSpace, Alpha);

		Builder.SetTransform(CurrentElement, BlendedRootInSampleSpace);
	}
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

	FVector TrajectoryPosPrev(0.0f);
	for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;

		FVector TrajectoryPos;
		if (Reader.GetPosition(Feature, &TrajectoryPos))
		{	
			Feature.Type = EPoseSearchFeatureType::Position;
			FLinearColor LinearColor = GetColorForFeature(Feature, Reader.GetLayout());
			FColor Color = LinearColor.ToFColor(true);

			TrajectoryPos = DrawParams.RootTransform.TransformPosition(TrajectoryPos);
			DrawDebugSphere(DrawParams.World, TrajectoryPos, DrawDebugSphereSize, DrawDebugSphereSegments, Color, false, LifeTime, DepthPriority,  DrawDebugSphereLineThickness);

			if (SubsampleIdx != 0)
			{
				FVector Direction;
				float Length;
				(TrajectoryPos - TrajectoryPosPrev).ToDirectionAndLength(Direction, Length);
				DrawDebugLine(DrawParams.World, TrajectoryPosPrev, TrajectoryPos, Color, false, LifeTime, DepthPriority, DrawDebugLineThickness);
			}

			TrajectoryPosPrev = TrajectoryPos;
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

	for (int32 SubsampleIdx = 0; SubsampleIdx != NumSubsamples; ++SubsampleIdx)
	{
		Feature.SubsampleIdx = SubsampleIdx;
		Feature.SchemaBoneIdx = 0;
		
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

	FSequenceIndexer Indexer;
	FSequenceIndexer::Result Result = Indexer.Process(SequenceMetaData->Schema, Sequence, SequenceMetaData->SamplingRange);

	SequenceMetaData->SearchIndex.Values = Result.Values;
	SequenceMetaData->SearchIndex.NumPoses = Result.NumIndexedPoses;
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

	// Prepare animation indexing tasks
	TArray<FSequenceIndexer> Indexers;
	Indexers.SetNum(Database->Sequences.Num());

	auto IndexerTask = [&Database, &Indexers](int32 SequenceIdx)
	{
		const FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[SequenceIdx];
		FSequenceIndexer& Indexer = Indexers[SequenceIdx];
		Indexer.Process(Database->Schema, DbSequence.Sequence, DbSequence.SamplingRange);
	};

	// Index animations independently
	ParallelFor(Database->Sequences.Num(), IndexerTask);

	// Write index info to sequence and count up total poses and storage required
	int32 TotalPoses = 0;
	int32 TotalFloats = 0;
	for (int32 SequenceIdx = 0; SequenceIdx != Database->Sequences.Num(); ++SequenceIdx)
	{
		FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[SequenceIdx];
		FSequenceIndexer::Result Result = Indexers[SequenceIdx].GetResult();
		DbSequence.NumPoses = Result.NumIndexedPoses;
		DbSequence.FirstPoseIdx = TotalPoses;
		TotalPoses += Result.NumIndexedPoses;
		TotalFloats += Result.Values.Num();
	}

	// Join animation data into a single search index
	Database->SearchIndex.Values.Reset(TotalFloats);
	for (const FSequenceIndexer& Indexer : Indexers)
	{
		FSequenceIndexer::Result Result = Indexer.GetResult();
		Database->SearchIndex.Values.Append(Result.Values.GetData(), Result.Values.Num());
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
	TArray<float>& QueryBuffer = PoseHistory.GetQueryBuffer();

	QueryBuffer.SetNum(MetaData->Schema->Layout.NumFloats);
	TArrayView<float> Query = MakeArrayView(QueryBuffer.GetData(), QueryBuffer.Num());

	FFeatureVectorBuilder QueryBuilder;
	QueryBuilder.Init(&MetaData->Schema->Layout, Query);
	QueryBuilder.SetPoseFeatures(MetaData->Schema, &PoseHistory);

	::UE::PoseSearch::FSearchResult Result = ::UE::PoseSearch::Search(Sequence, Query);

	ProviderResult.Dissimilarity = Result.Dissimilarity;
	ProviderResult.PoseIdx = Result.PoseIdx;
	ProviderResult.TimeOffsetSeconds = Result.TimeOffsetSeconds;
	return ProviderResult;
}

}} // namespace UE::PoseSearch

IMPLEMENT_MODULE(UE::PoseSearch::FModule, PoseSearch)