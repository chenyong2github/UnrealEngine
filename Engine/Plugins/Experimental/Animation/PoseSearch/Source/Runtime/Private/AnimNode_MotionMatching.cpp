// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "AnimNode_MotionMatching"


/////////////////////////////////////////////////////
// FAnimNode_MotionMatching

void FAnimNode_MotionMatching::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);

	InitNewDatabaseSearch();

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
	// Note: What if the input database changes? That's not being handled at all!

	bool bJumpedToPose = false;

	if (IsValidForSearch())
	{
		if (PreviousDatabase != Database)
		{
			InitNewDatabaseSearch();
		}

		if (!ComposedQuery.IsInitializedForSchema(Database->Schema))
		{
			ComposedQuery.Init(Database->Schema);
		}

		const float AssetTime = SequencePlayerNode.GetAccumulatedTime();
		const float AssetLength = SequencePlayerNode.GetCurrentAssetLength();

		// Step the pose forward
		if (DbPoseIdx != INDEX_NONE)
		{
			check(DbSequenceIdx != INDEX_NONE);

			// Determine roughly which pose we're playing from the database based on the sequence player time
			DbPoseIdx = Database->GetPoseIndexFromAssetTime(DbSequenceIdx, AssetTime);

			// Overwrite query feature vector with the stepped pose from the database
			if (DbPoseIdx != INDEX_NONE)
			{
				ComposedQuery.CopyFromSearchIndex(Database->SearchIndex, DbPoseIdx);
			}
		}

		if (DbPoseIdx == INDEX_NONE)
		{
			UE::PoseSearch::IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<UE::PoseSearch::IPoseHistoryProvider>();
			if (PoseHistoryProvider)
			{
				UE::PoseSearch::FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
				ComposedQuery.TrySetPoseFeatures(&History);
			}
		}

		// Update features in the query with the latest inputs
		ComposeQuery(Context);

		// Initialize the pose search query bias weights context
		FPoseSearchBiasWeights QueryBiasWeights;
		QueryBiasWeights.Init(BiasWeights, Database->Schema->Layout);

		const FPoseSearchBiasWeightsContext BiasWeightsContext = { &QueryBiasWeights, Database };

		// Determine how much the updated query vector deviates from the current pose vector
		float CurrentDissimilarity = MAX_flt;
		if (DbPoseIdx != INDEX_NONE)
		{
			CurrentDissimilarity = UE::PoseSearch::ComparePoses(Database->SearchIndex, DbPoseIdx, ComposedQuery.GetNormalizedValues(), &BiasWeightsContext);
		}

		// Search the database for the nearest match to the updated query vector
		UE::PoseSearch::FDbSearchResult Result = UE::PoseSearch::Search(Database, ComposedQuery.GetNormalizedValues(), &BiasWeightsContext);
		if (Result.IsValid() && (ElapsedPoseJumpTime >= SearchThrottleTime))
		{
			const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[Result.DbSequenceIdx];

			// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
			bool bBetterPose = Result.Dissimilarity * (1.0f + (MinPercentImprovement / 100.0f)) < CurrentDissimilarity;

			// We'll ignore the candidate pose if it is too near to our current pose
			bool bNearbyPose = false;
			if (DbSequenceIdx == Result.DbSequenceIdx)
			{
				bNearbyPose = FMath::Abs(AssetTime - Result.TimeOffsetSeconds) < PoseJumpThreshold;
				if (!bNearbyPose && ResultDbSequence.bLoopAnimation)
				{
					bNearbyPose = FMath::Abs(AssetLength - AssetTime - Result.TimeOffsetSeconds) < PoseJumpThreshold;
				}
			}

			// Start playback from the candidate pose if we determined it was a better option
			if (bBetterPose && !bNearbyPose)
			{
				JumpToPose(Context, Result);
				bJumpedToPose = true;
			}
		}

		// Continue with the follow up sequence if we're finishing a one shot anim
		if (!bJumpedToPose && SequencePlayerNode.GetSequence() && !SequencePlayerNode.GetLoopAnimation())
		{
			float AssetTimeAfterUpdate = AssetTime + Context.GetDeltaTime();
			if (AssetTimeAfterUpdate > AssetLength)
			{
				const FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[DbSequenceIdx];
				int32 FollowUpDbSequenceIdx = Database->Sequences.IndexOfByPredicate([&](const FPoseSearchDatabaseSequence& Entry){ return Entry.Sequence == DbSequence.FollowUpSequence; });
				if (FollowUpDbSequenceIdx != INDEX_NONE)
				{
					const FPoseSearchDatabaseSequence& FollowUpDbSequence = Database->Sequences[FollowUpDbSequenceIdx];
					float FollowUpAssetTime = AssetTimeAfterUpdate - AssetLength;
					int32 FollowUpPoseIdx = Database->GetPoseIndexFromAssetTime(FollowUpDbSequenceIdx, FollowUpAssetTime);

					UE::PoseSearch::FDbSearchResult FollowUpResult;
					FollowUpResult.DbSequenceIdx = FollowUpDbSequenceIdx;
					FollowUpResult.PoseIdx = FollowUpPoseIdx;
					FollowUpResult.TimeOffsetSeconds = FollowUpAssetTime;
					JumpToPose(Context, FollowUpResult);
					bJumpedToPose = true;
				}
			}
		}
	}

	if (bJumpedToPose)
	{
		ElapsedPoseJumpTime = 0.0f;
	}
	else
	{
		ElapsedPoseJumpTime += Context.GetDeltaTime();
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
	if (bDebugDraw)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = InAnimInstance->GetSkelMeshComponent();
		check(SkeletalMeshComponent);

		UE::PoseSearch::FDebugDrawParams DrawParams;
		DrawParams.Flags = UE::PoseSearch::EDebugDrawFlags::None;
		DrawParams.RootTransform = SkeletalMeshComponent->GetComponentTransform();
		DrawParams.Database = Database;
		DrawParams.Query = ComposedQuery.GetValues();
		DrawParams.World = SkeletalMeshComponent->GetWorld();
		DrawParams.DefaultLifeTime = 0.0f;
		DrawParams.HighlightPoseIdx = DbPoseIdx;

		if (bDebugDrawMatch)
		{
			DrawParams.Flags |= UE::PoseSearch::EDebugDrawFlags::DrawBest;
		}

		if (bDebugDrawGoal)
		{
			DrawParams.Flags |= UE::PoseSearch::EDebugDrawFlags::DrawQuery;
		}

		UE::PoseSearch::Draw(DrawParams);
	}
#endif
}

void FAnimNode_MotionMatching::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

bool FAnimNode_MotionMatching::IsValidForSearch() const
{
	return Database && Database->IsValidForSearch();
}

void FAnimNode_MotionMatching::ComposeQuery(const FAnimationBaseContext& Context)
{
	// Set past trajectory features
	UE::PoseSearch::IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<UE::PoseSearch::IPoseHistoryProvider>();
	if (PoseHistoryProvider)
	{
		UE::PoseSearch::FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
		ComposedQuery.TrySetPastTrajectoryFeatures(&History);
	}

	// Merge goal features into the query vector
	if (ComposedQuery.IsCompatible(Goal))
	{
		ComposedQuery.MergeReplace(Goal);
	}

	ComposedQuery.Normalize(Database->SearchIndex);
}

void FAnimNode_MotionMatching::JumpToPose(const FAnimationUpdateContext& Context, UE::PoseSearch::FDbSearchResult Result)
{
	// Remember which pose and sequence we're playing from the database
	DbPoseIdx = Result.PoseIdx;
	DbSequenceIdx = Result.DbSequenceIdx;

	// Immediately jump to the pose by updating the embedded sequence player node
	const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[Result.DbSequenceIdx];
	SequencePlayerNode.SetSequence(ResultDbSequence.Sequence);
	SequencePlayerNode.SetAccumulatedTime(Result.TimeOffsetSeconds);
	SequencePlayerNode.SetLoopAnimation(ResultDbSequence.bLoopAnimation);

	// Use inertial blending to smooth over the transition
	// It would be cool in the future to adjust the blend time by amount of dissimilarity, but we'll need a standardized distance metric first.
	if (BlendTime > 0.0f)
	{
		UE::Anim::IInertializationRequester* InertializationRequester = Context.GetMessage<UE::Anim::IInertializationRequester>();
		if (InertializationRequester)
		{
			InertializationRequester->RequestInertialization(BlendTime);
		}
	}
}

void FAnimNode_MotionMatching::InitNewDatabaseSearch()
{
	DbPoseIdx = INDEX_NONE;
	DbSequenceIdx = INDEX_NONE;
	ElapsedPoseJumpTime = SearchThrottleTime;
	PreviousDatabase = Database;
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

void FAnimNode_MotionMatching::SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest)
{
#if WITH_EDITORONLY_DATA
	bIgnoreForRelevancyTest = bInIgnoreForRelevancyTest;
#endif
}

#undef LOCTEXT_NAMESPACE