// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
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

	CurrentAssetPlayerNode = &SequencePlayerNode;

	MirrorNode.SetSourceLinkNode(CurrentAssetPlayerNode);

	BlendSpacePlayerNode.SetResetPlayTimeWhenBlendSpaceChanges(false /*!bReset*/);

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
	MotionMatchingState.RootMotionTransformDelta = FTransform::Identity;

	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

	if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
	{
		if (RootMotionProvider->HasRootMotion(Output.CustomAttributes))
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

	bool bNeedsReset =
		bResetOnBecomingRelevant &&
		UpdateCounter.HasEverBeenUpdated() &&
		!UpdateCounter.WasSynchronizedCounter(Context.AnimInstanceProxy->GetUpdateCounter());

	// If we just became relevant and haven't been initialized yet, then reset motion matching state, otherwise update the asset time using the player node.
	if (bNeedsReset)
	{
		MotionMatchingState.Reset();
	}
	else
	{
		// We adjust the motion matching state asset time to the current player node's asset time. This is done 
		// because the player node may have ticked more or less time than we expected due to variable dt or the 
		// dynamic playback rate adjustment and as such the motion matching state does not update by itself
		MotionMatchingState.AdjustAssetTime(CurrentAssetPlayerNode->GetAccumulatedTime());
	}
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());

	// Execute core motion matching algorithm
	UpdateMotionMatchingState(
		Context,
		Searchable,
		bUseDatabaseTagQuery ? &DatabaseTagQuery : nullptr,
		&ActiveTagsContainer,
		Trajectory,
		Settings,
		MotionMatchingState,
		bForceInterrupt
	);


	if (MotionMatchingState.CurrentSearchResult.Database.IsValid() && 
		MotionMatchingState.CurrentSearchResult.Database->Schema)
	{
		MirrorNode.SetMirrorDataTable(MotionMatchingState.CurrentSearchResult.Database->Schema->MirrorDataTable.Get());
	}

	const FPoseSearchIndexAsset* SearchIndexAsset = MotionMatchingState.GetCurrentSearchIndexAsset();

	// If a new pose is requested, jump to the pose by updating the embedded sequence player node
	if (SearchIndexAsset && (MotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
	{
		const UPoseSearchDatabase* Database = MotionMatchingState.CurrentSearchResult.Database.Get();

		if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
		{
			CurrentAssetPlayerNode = &SequencePlayerNode;

			const FPoseSearchDatabaseSequence& ResultDbSequence = Database->GetSequenceSourceAsset(SearchIndexAsset);
			SequencePlayerNode.SetAccumulatedTime(MotionMatchingState.AssetPlayerTime);
			SequencePlayerNode.SetSequence(ResultDbSequence.Sequence);
			SequencePlayerNode.SetLoopAnimation(ResultDbSequence.Sequence->bLoop);
			SequencePlayerNode.SetPlayRate(1.0f);
		}
		else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
		{
			CurrentAssetPlayerNode = &BlendSpacePlayerNode;

			const FPoseSearchDatabaseBlendSpace& ResultDbBlendSpace = Database->GetBlendSpaceSourceAsset(SearchIndexAsset);
			BlendSpacePlayerNode.SetAccumulatedTime(MotionMatchingState.AssetPlayerTime);
			BlendSpacePlayerNode.SetBlendSpace(ResultDbBlendSpace.BlendSpace);
			BlendSpacePlayerNode.SetLoop(ResultDbBlendSpace.BlendSpace->bLoop);
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
		DrawParams.Database = MotionMatchingState.CurrentSearchResult.Database.Get();
		DrawParams.World = SkeletalMeshComponent->GetWorld();
		DrawParams.DefaultLifeTime = 0.0f;

		if (bDebugDrawMatch)
		{
			DrawParams.PoseIdx = MotionMatchingState.CurrentSearchResult.PoseIdx;
		}

		if (bDebugDrawQuery)
		{
			DrawParams.PoseVector = MotionMatchingState.CurrentSearchResult.ComposedQuery.GetValues();
		}

		DrawParams.SearchCostHistoryKDTree = &MotionMatchingState.SearchCostHistoryKDTree;
		DrawParams.SearchCostHistoryBruteForce = &MotionMatchingState.SearchCostHistoryBruteForce;

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