// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "Animation/AnimRootMotionProvider.h"
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

	MotionMatchingState.InitNewDatabaseSearch(Database, Settings.SearchThrottleTime, nullptr /*OutError*/);

	CurrentAssetPlayerNode = &SequencePlayerNode;

	MirrorNode.SetSourceLinkNode(CurrentAssetPlayerNode);
	MirrorNode.SetMirrorDataTable(Database->Schema->MirrorDataTable.Get());

	Source.SetLinkNode(&MirrorNode);
	Source.Initialize(Context);
}

void FAnimNode_MotionMatching::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);

#if WITH_EDITORONLY_DATA
	bWasEvaluated = true;
#endif

#if UE_POSE_SEARCH_TRACE_ENABLED
	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

	if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
	{
		if (ensureMsgf(RootMotionProvider->HasRootMotion(Output.CustomAttributes), TEXT("Motion Matching Output no Root Motion Attribute.")))
		{
			RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, MotionMatchingState.RootMotionTransformDelta);
		}
	}
#endif
}

void FAnimNode_MotionMatching::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateAssetPlayer);

	GetEvaluateGraphExposedInputs().Execute(Context);

	// Update the current time.
	MotionMatchingState.AssetPlayerTime = CurrentAssetPlayerNode->GetAccumulatedTime();

	// Execute core motion matching algorithm and retain across frame state
	UpdateMotionMatchingState(
		Context,
		Database,
		bUseDatabaseTagQuery ? &DatabaseTagQuery : nullptr,
		Trajectory,
		Settings,
		MotionMatchingState
	);

	const FPoseSearchIndexAsset* SearchIndexAsset = MotionMatchingState.GetCurrentSearchIndexAsset();

	// If a new pose is requested, jump to the pose by updating the embedded sequence player node
	if (SearchIndexAsset && (MotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
	{
		if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
		{
			CurrentAssetPlayerNode = &SequencePlayerNode;

			const FPoseSearchDatabaseSequence& ResultDbSequence = Database->GetSequenceSourceAsset(SearchIndexAsset);
			SequencePlayerNode.SetAccumulatedTime(MotionMatchingState.AssetPlayerTime);
			SequencePlayerNode.SetSequence(ResultDbSequence.Sequence);
			SequencePlayerNode.SetLoopAnimation(ResultDbSequence.bLoopAnimation);
			SequencePlayerNode.SetPlayRate(1.0f);
		}
		else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
		{
			CurrentAssetPlayerNode = &BlendSpacePlayerNode;

			const FPoseSearchDatabaseBlendSpace& ResultDbBlendSpace = Database->GetBlendSpaceSourceAsset(SearchIndexAsset);
			BlendSpacePlayerNode.SetAccumulatedTime(MotionMatchingState.AssetPlayerTime);
			BlendSpacePlayerNode.SetBlendSpace(ResultDbBlendSpace.BlendSpace);
			BlendSpacePlayerNode.SetLoop(ResultDbBlendSpace.bLoopAnimation);
			BlendSpacePlayerNode.SetPlayRate(1.0f);
			BlendSpacePlayerNode.SetPosition(SearchIndexAsset->BlendParameters);
		}
		else
		{
			checkNoEntry();
		}

		MirrorNode.SetSourceLinkNode(CurrentAssetPlayerNode);
		MirrorNode.SetMirror(SearchIndexAsset->bMirrored);
	}

	if (SearchIndexAsset && SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		// Optionally applying dynamic play rate adjustment to chosen sequences based on predictive motion analysis
		const float PlayRate = DynamicPlayRateAdjustment(
			Context,
			Trajectory,
			DynamicPlayRateSettings,
			SequencePlayerNode.GetSequence(),
			SequencePlayerNode.GetAccumulatedTime(),
			SequencePlayerNode.GetPlayRate(),
			SequencePlayerNode.GetLoopAnimation()
		);

		SequencePlayerNode.SetPlayRate(PlayRate);
	}

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
	if (bWasEvaluated && bDebugDraw)
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

	bWasEvaluated = false;
#endif
}

void FAnimNode_MotionMatching::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

// FAnimNode_AssetPlayerBase interface
float FAnimNode_MotionMatching::GetAccumulatedTime() const
{
	return CurrentAssetPlayerNode->GetAccumulatedTime();
}

UAnimationAsset* FAnimNode_MotionMatching::GetAnimAsset() const
{
	return CurrentAssetPlayerNode->GetAnimAsset();
}

float FAnimNode_MotionMatching::GetCurrentAssetLength() const
{
	return CurrentAssetPlayerNode->GetCurrentAssetLength();
}

float FAnimNode_MotionMatching::GetCurrentAssetTime() const
{
	return CurrentAssetPlayerNode->GetCurrentAssetLength();
}

float FAnimNode_MotionMatching::GetCurrentAssetTimePlayRateAdjusted() const
{
	return CurrentAssetPlayerNode->GetCurrentAssetTimePlayRateAdjusted();
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