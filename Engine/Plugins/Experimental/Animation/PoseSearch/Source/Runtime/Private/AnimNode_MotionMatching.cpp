// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "DynamicPlayRate/DynamicPlayRateLibrary.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearch.h"
#include "Trace/PoseSearchTraceLogger.h"

#define LOCTEXT_NAMESPACE "AnimNode_MotionMatching"

/////////////////////////////////////////////////////
// FAnimNode_MotionMatching

void FAnimNode_MotionMatching::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);

	MotionMatchingState.InitNewDatabaseSearch(Database, Settings.SearchThrottleTime);

	Source.SetLinkNode(&SequencePlayerNode);
	Source.Initialize(Context);
}

void FAnimNode_MotionMatching::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);
}

void FAnimNode_MotionMatching::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateAssetPlayer);

	GetEvaluateGraphExposedInputs().Execute(Context);

	// Update with the sequence player's current time.
	MotionMatchingState.AssetPlayerTime = SequencePlayerNode.GetAccumulatedTime();

	// Execute core motion matching algorithm and retain across frame state
	UpdateMotionMatchingState(Context
		, Database
		, Trajectory
		, Settings
		, MotionMatchingState
	);

	// If a new pose is requested, jump to the pose by updating the embedded sequence player node
	if ((MotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
	{
		const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[MotionMatchingState.DbSequenceIdx];
		SequencePlayerNode.SetSequence(ResultDbSequence.Sequence);
		SequencePlayerNode.SetAccumulatedTime(MotionMatchingState.AssetPlayerTime);
		SequencePlayerNode.SetLoopAnimation(ResultDbSequence.bLoopAnimation);
		SequencePlayerNode.SetPlayRate(1.f);
	}

	// Optionally applying dynamic play rate adjustment to chosen sequences based on predictive motion analysis
	const float PlayRate = DynamicPlayRateAdjustment(Context
		, Trajectory
		, DynamicPlayRateSettings
		, SequencePlayerNode.GetSequence()
		, SequencePlayerNode.GetAccumulatedTime()
		, SequencePlayerNode.GetPlayRate()
		, SequencePlayerNode.GetLoopAnimation()
	);

	SequencePlayerNode.SetPlayRate(PlayRate);
	Source.Update(Context);
}

bool FAnimNode_MotionMatching::HasPreUpdate() const
{
#if WITH_EDITORONLY_DATA
	return true;
#else
	return false;
#endif
}

void FAnimNode_MotionMatching::PreUpdate(const UAnimInstance* InAnimInstance)
{
#if WITH_EDITORONLY_DATA
	if (bDebugDraw)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = InAnimInstance->GetSkelMeshComponent();
		check(SkeletalMeshComponent);

		UE::PoseSearch::FDebugDrawParams DrawParams;
		DrawParams.RootTransform = SkeletalMeshComponent->GetComponentTransform();
		DrawParams.Database = Database;
		DrawParams.World = SkeletalMeshComponent->GetWorld();
		DrawParams.DefaultLifeTime = 0.0f;

		if (bDebugDrawMatch)
		{
			DrawParams.PoseIdx = MotionMatchingState.DbPoseIdx;
		}

		if (bDebugDrawQuery)
		{
			DrawParams.PoseVector = MotionMatchingState.ComposedQuery.GetValues();
		}

		UE::PoseSearch::Draw(DrawParams);
	}
#endif
}

void FAnimNode_MotionMatching::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

// FAnimNode_AssetPlayerBase interface
float FAnimNode_MotionMatching::GetAccumulatedTime() const
{
	return SequencePlayerNode.GetAccumulatedTime();
}

UAnimationAsset* FAnimNode_MotionMatching::GetAnimAsset() const
{
	return SequencePlayerNode.GetAnimAsset();
}

float FAnimNode_MotionMatching::GetCurrentAssetLength() const
{
	return SequencePlayerNode.GetCurrentAssetLength();
}

float FAnimNode_MotionMatching::GetCurrentAssetTime() const
{
	return SequencePlayerNode.GetCurrentAssetTime();
}

float FAnimNode_MotionMatching::GetCurrentAssetTimePlayRateAdjusted() const
{
	return SequencePlayerNode.GetCurrentAssetTimePlayRateAdjusted();
}

bool FAnimNode_MotionMatching::GetIgnoreForRelevancyTest() const
{
	return GET_ANIM_NODE_DATA(bool, bIgnoreForRelevancyTest);
}

bool FAnimNode_MotionMatching::SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest)
{
#if WITH_EDITORONLY_DATA
	bIgnoreForRelevancyTest = bInIgnoreForRelevancyTest;
#endif

	if(bool* bIgnoreForRelevancyTestPtr = GET_INSTANCE_ANIM_NODE_DATA_PTR(bool, bIgnoreForRelevancyTest))
	{
		*bIgnoreForRelevancyTestPtr = bInIgnoreForRelevancyTest;
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE