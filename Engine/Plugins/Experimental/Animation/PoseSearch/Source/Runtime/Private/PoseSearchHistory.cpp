// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchHistory.h"
#include "AnimationRuntime.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimRootMotionProvider.h"
#include "BonePose.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchDatabase.h"

IMPLEMENT_ANIMGRAPH_MESSAGE(UE::PoseSearch::IPoseHistoryProvider);

#define LOCTEXT_NAMESPACE "PoseSearch"

namespace UE::PoseSearch
{

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

static FBoneIndexType GetMaxFBoneIndexType(const TArray<FBoneIndexType>& RequiredBones)
{
	FBoneIndexType MaxBoneIndexType = 0;
	for (FBoneIndexType BoneIndexType : RequiredBones)
	{
		if (BoneIndexType > MaxBoneIndexType)
		{
			MaxBoneIndexType = BoneIndexType;
		}
	}
	return MaxBoneIndexType;
}

void FPoseHistory::GetLocalPoseAtTime(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose) const
{
	const int32 Num = Poses.Num();
	if (Num > 1)
	{
		const float SecondsAgo = -Time;
		const int32 LowerBoundIdx = LowerBound(Poses.begin(), Poses.end(), SecondsAgo, [](const FPose& Pose, float Value) { return Value < Pose.Time; });
		const int32 NextIdx = FMath::Clamp(LowerBoundIdx, 1, Num - 1);
		const int32 PrevIdx = NextIdx - 1;

		const FPose& PrevPose = Poses[PrevIdx];
		const FPose& NextPose = Poses[NextIdx];
				
		// Compute alpha between previous and next Poses
		const float Alpha = FMath::GetMappedRangeValueClamped(FVector2f(PrevPose.Time, NextPose.Time), FVector2f(0.0f, 1.0f), SecondsAgo);

		// Lerp between poses by alpha to produce output local pose at requested sample time
		check(PrevPose.LocalTransforms.Num() == NextPose.LocalTransforms.Num());
		check(GetMaxFBoneIndexType(RequiredBones) < PrevPose.LocalTransforms.Num());

		LocalPose = PrevPose.LocalTransforms;
		FAnimationRuntime::LerpBoneTransforms(LocalPose, NextPose.LocalTransforms, Alpha, RequiredBones);
	}
	else if (Num > 0)
	{
		check(GetMaxFBoneIndexType(RequiredBones) < Poses[0].LocalTransforms.Num());
		LocalPose = Poses[0].LocalTransforms;
	}
	else
	{
		const FBoneIndexType MaxBoneIndexType = GetMaxFBoneIndexType(RequiredBones);
		LocalPose.Init(FTransform::Identity, MaxBoneIndexType + 1);
	}
}

void FPoseHistory::GetRootTransformAtTime(float Time, FTransform& RootTransform) const
{
	const int32 Num = Poses.Num();
	if (Num > 1)
	{
		const float SecondsAgo = -Time;
		const int32 LowerBoundIdx = LowerBound(Poses.begin(), Poses.end(), SecondsAgo, [](const FPose& Pose, float Value) { return Value < Pose.Time; });
		const int32 NextIdx = FMath::Clamp(LowerBoundIdx, 1, Num - 1);
		const int32 PrevIdx = NextIdx - 1;

		const FPose& PrevPose = Poses[PrevIdx];
		const FPose& NextPose = Poses[NextIdx];

		// Compute alpha between previous and next Poses
		const float Alpha = FMath::GetMappedRangeValueClamped(FVector2f(PrevPose.Time, NextPose.Time), FVector2f(0.0f, 1.0f), SecondsAgo);

		RootTransform.Blend(PrevPose.RootTransform, NextPose.RootTransform, Alpha);
	}
	else if (Num > 0)
	{
		RootTransform = Poses[0].RootTransform;
	}
	else
	{
		RootTransform = FTransform::Identity;
	}
}

void FPoseHistory::Update(float SecondsElapsed, const FPoseContext& PoseContext, FTransform ComponentTransform)
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
	CurrentPose.RootTransform = ComponentTransform;
	CopyCompactToSkeletonPose(PoseContext.Pose, CurrentPose.LocalTransforms);
}

float FPoseHistory::GetSampleTimeInterval() const
{
	// Reserve one pose for computing derivatives at the time horizon
	return TimeHorizon / (Poses.Max() - 1);
}

void FPoseHistory::DebugDraw(const UWorld* World, const USkeleton* Skeleton) const
{
#if ENABLE_DRAW_DEBUG

	auto LerpColor = [](FColor A, FColor B, float T) -> FColor
	{
		return FColor(
			FMath::RoundToInt(float(A.R) * (1.f - T) + float(B.R) * T),
			FMath::RoundToInt(float(A.G) * (1.f - T) + float(B.G) * T),
			FMath::RoundToInt(float(A.B) * (1.f - T) + float(B.B) * T),
			FMath::RoundToInt(float(A.A) * (1.f - T) + float(B.A) * T));
	};

	TArray<FTransform> LocalTransforms;
	TArray<FTransform> GlobalTransforms;
	TArray<FTransform> PrevGlobalTransforms;
	for (int32 PoseIndex = 0; PoseIndex < Poses.Num(); ++PoseIndex)
	{
		const FPose& Pose = Poses[PoseIndex];
		if (Pose.LocalTransforms.IsEmpty())
		{
			LocalTransforms.Reset();
			GlobalTransforms.Reset();
		}
		else
		{
			LocalTransforms = Pose.LocalTransforms;
			LocalTransforms[0] = LocalTransforms[0] * Pose.RootTransform;
			FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton->GetReferenceSkeleton(), LocalTransforms, GlobalTransforms);
		}

		if (PrevGlobalTransforms.Num() == GlobalTransforms.Num())
		{
			const float LerpFactor = float(PoseIndex - 1) / float(Poses.Num() - 1);
			const FColor Color = LerpColor(FColorList::Red, FColorList::Orange, LerpFactor);
			for (int32 TransformIndex = 0; TransformIndex < GlobalTransforms.Num(); ++TransformIndex)
			{
				DrawDebugLine(World, PrevGlobalTransforms[TransformIndex].GetLocation(), GlobalTransforms[TransformIndex].GetLocation(), Color, false, 0.f, ESceneDepthPriorityGroup::SDPG_Foreground + 2);
			}
		}

		PrevGlobalTransforms = GlobalTransforms;
	}
#endif // ENABLE_DRAW_DEBUG
}

//////////////////////////////////////////////////////////////////////////
// FPoseIndicesHistory
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

} // namespace UE::PoseSearch

#undef LOCTEXT_NAMESPACE