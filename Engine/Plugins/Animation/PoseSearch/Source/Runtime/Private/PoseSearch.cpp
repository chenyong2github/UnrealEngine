// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearch.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "DrawDebugHelpers.h"

//////////////////////////////////////////////////////////////////////////
// UPoseSearchSchema

void UPoseSearchSchema::PreSave(const class ITargetPlatform* TargetPlatform)
{
	// Initialize references to obtain bone indices
	for (FBoneReference& BoneRef : Bones)
	{
		BoneRef.Initialize(GetSkeleton());
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
	FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, GetSkeleton()->GetReferenceSkeleton());

	// Sort fragment offsets by largest offset first since larger offsets are closer to the beginning of the sample array
	FragmentSampleOffsets.Sort(TGreater<>());

	// Ensure we have at least one offset at zero which corresponds with instantaneous sample matching
	if (FragmentSampleOffsets.Num() == 0)
	{
		FragmentSampleOffsets.Add(0);
	}

	FloatsPerPose = BoneIndices.Num() * 3;

	Super::PreSave(TargetPlatform);
}


//////////////////////////////////////////////////////////////////////////
// UPoseSearchPoseHistory

/**
* Fills skeleton transforms with evaluated compact pose transforms.
* Bones that weren't evaluated are filled with the bone's reference pose.
*/
static void CopyCompactToSkeletonPose (const FCompactPose& Pose, TArray<FTransform>& OutTransforms)
{
	const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
	TArrayView<const FTransform> RefSkeletonTransforms = MakeArrayView(RefSkeleton.GetRefBonePose());

	const int32 NumSkeletonBones = BoneContainer.GetNumBones();
	OutTransforms.SetNum(NumSkeletonBones);

	for (auto SkeletonBoneIdx = FSkeletonPoseBoneIndex(0); SkeletonBoneIdx != NumSkeletonBones; ++SkeletonBoneIdx)
	{
		FCompactPoseBoneIndex CompactBoneIdx = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(SkeletonBoneIdx.GetInt());
		OutTransforms[SkeletonBoneIdx.GetInt()] = CompactBoneIdx.IsValid() ? Pose[CompactBoneIdx] : RefSkeletonTransforms[SkeletonBoneIdx.GetInt()];
	}
}

void FPoseSearchPoseHistory::Init(int32 InNumPoses, float InTimeHorizon)
{
	int32 Capacity = FMath::RoundUpToPowerOfTwo(InNumPoses);

	if ((Queue.GetCapacity() == Capacity) && (TimeHorizon == InTimeHorizon))
	{
		return;
	}

	Poses.SetNum(Capacity);
	Knots.SetNum(Capacity);
	Queue.Init(Capacity);
	TimeHorizon = InTimeHorizon;
}

void FPoseSearchPoseHistory::Init(const FPoseSearchPoseHistory& History)
{
	Poses = History.Poses;
	Knots = History.Knots;
	Queue = History.Queue;
	TimeHorizon = History.TimeHorizon;
}

bool FPoseSearchPoseHistory::Sample(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& OutPose) const
{
	// Find the upper bound knot
	uint32 UpperBoundIndex = MAX_uint32;
	int32 UpperBoundOffset = 1;
	for (; UpperBoundOffset < (int32)Queue.Num(); ++UpperBoundOffset)
	{
		uint32 TestIndex = Queue.GetOffsetFromBack(UpperBoundOffset);
		if (Knots[TestIndex] >= Time)
		{
			UpperBoundIndex = TestIndex;
			break;
		}
	}

	if (UpperBoundIndex == MAX_uint32)
	{
		return false;
	}

	// The lower bound knot is adjacent
	int32 LowerBoundOffset = UpperBoundOffset - 1;
	uint32 LowerBoundIndex = Queue.GetOffsetFromBack(LowerBoundOffset);

	// Compute alpha between upper and lower bound knots
	float Alpha = FMath::GetMappedRangeValueUnclamped(
		FVector2D(Knots[LowerBoundIndex], Knots[UpperBoundIndex]),
		FVector2D(0.0f, 1.0f),
		Time);

	// Lerp between upper and lower bound poses by alpha to produce output pose at requested sample time
	OutPose = Poses[LowerBoundIndex].LocalTransforms;
	FAnimationRuntime::LerpBoneTransforms(
		OutPose,
		Poses[UpperBoundIndex].LocalTransforms,
		Alpha,
		RequiredBones);

	return true;
}

void FPoseSearchPoseHistory::Update(float TimeDelta, const FCompactPose& Pose)
{
	// Age our elapsed times
	for (int32 Offset = 0; Offset != (int32)Queue.Num(); ++Offset)
	{
		uint32 Index = Queue.GetOffsetFromFront(Offset);
		Knots[Index] += TimeDelta;
	}

	if (!Queue.IsFull())
	{
		// Consume every pose until the queue is full
		Queue.PushBack();
	}
	else
	{
		// Exercise pose retention policy. We must guarantee there is always one additional knot
		// at or beyond the desired time horizon H so we can fulfill sample requests at t=H. We also
		// want to evenly distribute knots across the entire history buffer so we only push additional
		// poses when enough time has elapsed.

		const float SampleInterval = GetSampleInterval();

		uint32 SecondOldest = Queue.GetOffsetFromFront(1);
		bool bCanEvictOldest = Knots[SecondOldest] >= TimeHorizon;

		uint32 SecondNewest = Queue.GetOffsetFromBack(1);
		bool bShouldPushNewest = Knots[SecondNewest] >= SampleInterval;

		if (bCanEvictOldest && bShouldPushNewest)
		{
			Queue.PopFront();
			Queue.PushBack();
		}
	}

	// Regardless of the retention policy, we always update the most recent pose
	uint32 Newest = Queue.GetOffsetFromBack(0);
	Knots[Newest] = 0.0f;
	CopyCompactToSkeletonPose(Pose, Poses[Newest].LocalTransforms);
}

float FPoseSearchPoseHistory::GetSampleInterval() const
{
	return TimeHorizon / Queue.GetCapacity();
}


//////////////////////////////////////////////////////////////////////////
// PoseSearch API

void PoseSearchDrawPose(const FPoseSearchDebugDrawParams& DrawParams, const FColor& Color, TArrayView<const float> Pose, float LifeTimeDelta, uint8 DepthPriorityDelta = 0)
{
	const float LifeTime = DrawParams.DefaultLifeTime + LifeTimeDelta;
	const uint8 DepthPriority = ESceneDepthPriorityGroup::SDPG_Foreground + 1 + DepthPriorityDelta;

	FVector PointPrev(
		Pose[0],
		Pose[1],
		Pose[2]);

	PointPrev = DrawParams.ComponentTransform.TransformPosition(PointPrev);

	const int32 NumPoints = Pose.Num() / 3;

	for (int32 PointIdx = 1; PointIdx != NumPoints; ++PointIdx)
	{
		FVector PointNext(
			Pose[PointIdx * 3 + 0],
			Pose[PointIdx * 3 + 1],
			Pose[PointIdx * 3 + 2]);

		PointNext = DrawParams.ComponentTransform.TransformPosition(PointNext);

		DrawDebugPoint(DrawParams.World, PointNext, 1.0f, Color, false, LifeTime, DepthPriority);

		bool bIsChildOfPrev = DrawParams.Schema->GetSkeleton()->GetReferenceSkeleton().BoneIsChildOf(
			DrawParams.Schema->BoneIndices[PointIdx],
			DrawParams.Schema->BoneIndices[PointIdx-1]);

		if (bIsChildOfPrev)
		{
			DrawDebugLine(DrawParams.World, PointPrev, PointNext, Color, false, LifeTime, DepthPriority);
		}
		PointPrev = PointNext;
	}
}

void PoseSearchDrawSearchIndex(const FPoseSearchDebugDrawParams& DrawParams, const UPoseSearchIndex& SearchIndex, int32 HighlightPoseIdx)
{
	if (!DrawParams.CanDraw())
	{
		return;
	}

	int32 LastPoseIdx = SearchIndex.NumPoses;
	int32 StartPoseIdx = 0;
	if (!(DrawParams.Flags & EPoseSearchDebugDrawFlags::DrawSearchIndex))
	{
		StartPoseIdx = HighlightPoseIdx;
		LastPoseIdx = StartPoseIdx + 1;
	}

	if (StartPoseIdx < 0)
	{
		return;
	}

	for (int32 PoseDrawIdx = StartPoseIdx; PoseDrawIdx != LastPoseIdx; ++PoseDrawIdx)
	{
		float LifeTimeDelta = 0.0f;
		FLinearColor Color;
		if (PoseDrawIdx == HighlightPoseIdx)
		{
			Color = FLinearColor::Yellow;
		}
		else
		{
			float Lerp = (float)(PoseDrawIdx + 1) / SearchIndex.NumPoses;
			LifeTimeDelta = Lerp - 1.0f;
			Color = FLinearColor::LerpUsingHSV(FLinearColor(FColor::Cyan), FLinearColor(FColor::Blue), Lerp);
		}

		TArrayView<const float> Pose = MakeArrayView(&SearchIndex.Values[PoseDrawIdx * SearchIndex.FloatsPerPose], SearchIndex.FloatsPerPose);
		PoseSearchDrawPose(DrawParams, Color.ToFColor(true), Pose, LifeTimeDelta);
	}
}

void PoseSearchDrawQuery(const FPoseSearchDebugDrawParams& DrawParams, TArrayView<const float> Query)
{
	if (!DrawParams.CanDraw())
	{
		return;
	}

	int32 NumSamples = DrawParams.Schema->FragmentSampleOffsets.Num();
	for (int32 Sample = 0; Sample != NumSamples; ++Sample)
	{
		float Lerp = (Sample + 1) / NumSamples;
		FLinearColor Color = FLinearColor::LerpUsingHSV(FLinearColor(FColor::Magenta), FLinearColor(FColor::Purple), Lerp);

		TArrayView<const float> Pose = MakeArrayView(&Query[Sample * DrawParams.Schema->FloatsPerPose], DrawParams.Schema->FloatsPerPose);
		PoseSearchDrawPose(DrawParams, Color.ToFColor(true), Pose, 0.0f, 1);
	}
}

void PoseSearchBuildIndex(const UAnimSequenceBase& AnimSequence, const UPoseSearchIndexConfig& SearchConfig, const UPoseSearchSchema& SearchSchema, UPoseSearchIndex* SearchIndex)
{
	USkeleton* Skeleton = AnimSequence.GetSkeleton();
	check(Skeleton);
	check(Skeleton->IsCompatible(SearchSchema.GetSkeleton()));

	FBoneContainer BoneContainer;
	BoneContainer.InitializeTo(SearchSchema.BoneIndicesWithParents, FCurveEvaluationOption(false), *Skeleton);

	FBlendedCurve UnusedCurve;
	FAnimExtractContext ExtractionCtx;
	// ExtractionCtx.PoseCurves is intentionally left empty
	// ExtractionCtx.BonesRequired is unused by UAnimSequence::GetAnimationPose
	ExtractionCtx.bExtractRootMotion = true;

	double CurrTime = SearchConfig.FrameSamplingRange.GetLowerBoundValue();
	const double EndTime = FMath::Min(AnimSequence.GetPlayLength(), SearchConfig.FrameSamplingRange.GetUpperBoundValue());
	const double DeltaTime = 1.0 / SearchConfig.SampleRate;

	const int32 NumPoses = FMath::FloorToInt((EndTime - CurrTime) / DeltaTime);

	SearchIndex->Values.Reset(SearchSchema.FloatsPerPose * NumPoses);

	FCompactPose Pose;
	Pose.SetBoneContainer(&BoneContainer);
	FCSPose<FCompactPose> ComponentSpacePose;

	for (int32 PoseIdx = 0; PoseIdx != NumPoses; ++PoseIdx, CurrTime += DeltaTime)
	{
		// Extract pose
		ExtractionCtx.CurrentTime = CurrTime;
		AnimSequence.GetAnimationPose(Pose, UnusedCurve, ExtractionCtx);
		ComponentSpacePose.InitPose(Pose);

		for (int32 BoneIndex : SearchSchema.BoneIndices)
		{
			FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
			const FTransform& Transform = ComponentSpacePose.GetComponentSpaceTransform(CompactBoneIndex);
 
			FVector Translation = Transform.GetTranslation();
			SearchIndex->Values.Add(Translation.X);
			SearchIndex->Values.Add(Translation.Y);
			SearchIndex->Values.Add(Translation.Z);
		}
	}

	SearchIndex->NumPoses = NumPoses;
	SearchIndex->FloatsPerPose = SearchSchema.FloatsPerPose;
	SearchIndex->Schema = &SearchSchema;
	SearchIndex->SequenceSampleRate = SearchConfig.SampleRate;
	SearchIndex->SequenceFrameSamplingRange = SearchConfig.FrameSamplingRange;
}

bool PoseSearchBuildQuery(const UPoseSearchSchema& SearchSchema, int32 AssetSampleRate, const FPoseSearchPoseHistory& History, FPoseSearchBuildQueryScratch* Scratch, TArray<float>* Query)
{
	Query->Reset(0);
	Query->Reserve(SearchSchema.FragmentSampleOffsets.Num() * SearchSchema.FloatsPerPose);

	for (int32 Offset : SearchSchema.FragmentSampleOffsets)
	{
		float TimeDelta = Offset * (1.0f / AssetSampleRate);

		if (!History.Sample(TimeDelta, SearchSchema.BoneIndicesWithParents, Scratch->LocalPose))
		{
			return false;
		}

		FAnimationRuntime::FillUpComponentSpaceTransforms(SearchSchema.GetSkeleton()->GetReferenceSkeleton(), Scratch->LocalPose, Scratch->ComponentPose);

		for (int32 SkeletonBoneIndex : SearchSchema.BoneIndices)
		{
			const FTransform& Transform = Scratch->ComponentPose[SkeletonBoneIndex];
			FVector Translation = Transform.GetTranslation();
			Query->Add(Translation.X);
			Query->Add(Translation.Y);
			Query->Add(Translation.Z);
		}
	}

	return true;
}

FPoseSearchResult PoseSearch(const UPoseSearchIndex& SearchIndex, TArrayView<const float> Query, FPoseSearchDebugDrawParams DebugDrawParams)
{
	check(SearchIndex.Schema != nullptr);
	check(SearchIndex.NumPoses * SearchIndex.FloatsPerPose == SearchIndex.Values.Num());

	if (Query.Num() != SearchIndex.FloatsPerPose * SearchIndex.Schema->FragmentSampleOffsets.Num())
	{
		return FPoseSearchResult();
	}

	float BestPoseDifference = TNumericLimits<float>::Max();
	int32 BestPoseIdx = INDEX_NONE;

	for (int32 PoseIdx = SearchIndex.Schema->FragmentSampleOffsets[0]; PoseIdx < SearchIndex.NumPoses; ++PoseIdx)
	{
		float PoseDifference = 0.0f;
		int32 QueryValueIdx = 0;
		for (int32 PoseOffset : SearchIndex.Schema->FragmentSampleOffsets)
		{
			int32 SearchValueIdx = (PoseIdx - PoseOffset) * SearchIndex.FloatsPerPose;

			for (int32 PoseElemCount = 0; PoseElemCount != SearchIndex.FloatsPerPose; ++PoseElemCount, ++QueryValueIdx, ++SearchValueIdx)
			{
				PoseDifference += FMath::Square(Query[QueryValueIdx] - SearchIndex.Values[SearchValueIdx]);
			}
		}

		if (PoseDifference < BestPoseDifference)
		{
			BestPoseDifference = PoseDifference;
			BestPoseIdx = PoseIdx;
		}
	}

	check(BestPoseIdx != INDEX_NONE);

	const float SampleDelta = 1.0f / SearchIndex.SequenceSampleRate;

	float BestPoseTime = FMath::Min(
		SearchIndex.SequenceFrameSamplingRange.GetUpperBoundValue(),
		SampleDelta * BestPoseIdx + SearchIndex.SequenceFrameSamplingRange.GetLowerBoundValue());

	// Do debug visualization
	DebugDrawParams.Schema = SearchIndex.Schema;
	if (DebugDrawParams.CanDraw())
	{
		if (EnumHasAnyFlags(DebugDrawParams.Flags, EPoseSearchDebugDrawFlags::DrawQuery))
		{
			PoseSearchDrawQuery(DebugDrawParams, Query);
		}

		if (EnumHasAnyFlags(DebugDrawParams.Flags, EPoseSearchDebugDrawFlags::DrawSearchIndex | EPoseSearchDebugDrawFlags::DrawBest))
		{
			int32 HighlightPoseIdx = EnumHasAnyFlags(DebugDrawParams.Flags, EPoseSearchDebugDrawFlags::DrawBest) ? BestPoseIdx : -1;
			PoseSearchDrawSearchIndex(DebugDrawParams, SearchIndex, HighlightPoseIdx);
		}
	}

	FPoseSearchResult Result;
	Result.Dissimilarity = BestPoseDifference;
	Result.TimeOffsetSeconds = BestPoseTime;
	Result.PoseIdx = BestPoseIdx;
	return Result;
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchModule

void FPoseSearchModule::StartupModule()
{
}

void FPoseSearchModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FPoseSearchModule, PoseSearch)