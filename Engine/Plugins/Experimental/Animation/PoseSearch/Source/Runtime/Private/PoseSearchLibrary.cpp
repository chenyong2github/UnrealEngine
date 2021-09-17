// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Trace/PoseSearchTraceLogger.h"

DEFINE_LOG_CATEGORY_STATIC(LogPoseSearchLibrary, Verbose, All);

void FMotionMatchingState::InitNewDatabaseSearch(const UPoseSearchDatabase* Database, float SearchThrottleTime)
{
	DbPoseIdx = INDEX_NONE;
	DbSequenceIdx = INDEX_NONE;
	ElapsedPoseJumpTime = SearchThrottleTime;
	AssetPlayerTime = 0.0f;
	CurrentDatabase = Database;
}

void FMotionMatchingState::ComposeQuery(const UPoseSearchDatabase* Database, const FTrajectorySampleRange& Trajectory)
{
	FPoseSearchFeatureVectorBuilder Goal;
	Goal.Init(Database->Schema);
	Goal.BuildFromTrajectory(Trajectory);

	// Merge goal features into the query vector
	if (ComposedQuery.IsCompatible(Goal))
	{
		ComposedQuery.MergeReplace(Goal);
	}

	ComposedQuery.Normalize(Database->SearchIndex);
}

void FMotionMatchingState::JumpToPose(const UE::PoseSearch::FDbSearchResult& Result)
{
	// Remember which pose and sequence we're playing from the database
	DbPoseIdx = Result.PoseIdx;
	DbSequenceIdx = Result.DbSequenceIdx;

	ElapsedPoseJumpTime = 0.0f;
	AssetPlayerTime = Result.TimeOffsetSeconds;
}

static void RequestInertialBlend(const FAnimationUpdateContext& Context, float BlendTime)
{
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

void UpdateMotionMatchingState(const FAnimationUpdateContext& Context
	, const UPoseSearchDatabase* Database
	, const FTrajectorySampleRange& Trajectory
	, const FMotionMatchingSettings& Settings
	, FMotionMatchingState& InOutMotionMatchingState
)
{
	InOutMotionMatchingState.Flags = EMotionMatchingFlags::None;
	const float DeltaTime = Context.GetDeltaTime();

	if (Database && Database->IsValidForSearch())
	{
		if (InOutMotionMatchingState.CurrentDatabase != Database)
		{
			InOutMotionMatchingState.InitNewDatabaseSearch(Database, Settings.SearchThrottleTime);
		}

		if (!InOutMotionMatchingState.ComposedQuery.IsInitializedForSchema(Database->Schema))
		{
			InOutMotionMatchingState.ComposedQuery.Init(Database->Schema);
		}

		// Step the pose forward
		if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
		{
			check(InOutMotionMatchingState.DbSequenceIdx != INDEX_NONE);

			// Determine roughly which pose we're playing from the database based on the sequence player time
			InOutMotionMatchingState.DbPoseIdx = Database->GetPoseIndexFromAssetTime(InOutMotionMatchingState.DbSequenceIdx, InOutMotionMatchingState.AssetPlayerTime);

			// Overwrite query feature vector with the stepped pose from the database
			if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
			{
				InOutMotionMatchingState.ComposedQuery.CopyFromSearchIndex(Database->SearchIndex, InOutMotionMatchingState.DbPoseIdx);
			}
		}

		if (InOutMotionMatchingState.DbPoseIdx == INDEX_NONE)
		{
			UE::PoseSearch::IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<UE::PoseSearch::IPoseHistoryProvider>();
			if (PoseHistoryProvider)
			{
				UE::PoseSearch::FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
				InOutMotionMatchingState.ComposedQuery.TrySetPoseFeatures(&History);
			}
		}

		// Update features in the query with the latest inputs
		InOutMotionMatchingState.ComposeQuery(Database, Trajectory);

		// Update weight groups
		InOutMotionMatchingState.WeightsContext.Update(Settings.Weights, Database);

		// Determine how much the updated query vector deviates from the current pose vector
		float CurrentDissimilarity = MAX_flt;
		if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
		{
			CurrentDissimilarity = UE::PoseSearch::ComparePoses(
				Database->SearchIndex,
				InOutMotionMatchingState.DbPoseIdx,
				InOutMotionMatchingState.ComposedQuery.GetNormalizedValues(),
				&InOutMotionMatchingState.WeightsContext
			);
		}

		// Search the database for the nearest match to the updated query vector
		UE::PoseSearch::FDbSearchResult Result = UE::PoseSearch::Search(
			Database,
			InOutMotionMatchingState.ComposedQuery.GetNormalizedValues(),
			&InOutMotionMatchingState.WeightsContext
		);
		if (Result.IsValid() && (InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime))
		{
			const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[Result.DbSequenceIdx];

			// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
			bool bBetterPose = Result.Dissimilarity * (1.0f + (Settings.MinPercentImprovement / 100.0f)) < CurrentDissimilarity;

			// We'll ignore the candidate pose if it is too near to our current pose
			bool bNearbyPose = false;
			if (InOutMotionMatchingState.DbSequenceIdx == Result.DbSequenceIdx)
			{
				bNearbyPose = FMath::Abs(InOutMotionMatchingState.AssetPlayerTime - Result.TimeOffsetSeconds) < Settings.PoseJumpThresholdTime;
				if (!bNearbyPose && ResultDbSequence.bLoopAnimation)
				{
					const float AssetLength = Database->GetSequenceLength(InOutMotionMatchingState.DbSequenceIdx);
					bNearbyPose = FMath::Abs(AssetLength - InOutMotionMatchingState.AssetPlayerTime - Result.TimeOffsetSeconds) < Settings.PoseJumpThresholdTime;
				}
			}

			// Start playback from the candidate pose if we determined it was a better option
			if (bBetterPose && !bNearbyPose)
			{
				InOutMotionMatchingState.JumpToPose(Result);
				RequestInertialBlend(Context, Settings.BlendTime);
				InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToPose;
			}
		}

		// Continue with the follow up sequence if we're finishing a one shot anim
		if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
			&& (InOutMotionMatchingState.DbSequenceIdx != INDEX_NONE) 
			&& !Database->DoesSequenceLoop(InOutMotionMatchingState.DbSequenceIdx))
		{
			float AssetTimeAfterUpdate = InOutMotionMatchingState.AssetPlayerTime + DeltaTime;
			const float AssetLength = Database->GetSequenceLength(InOutMotionMatchingState.DbSequenceIdx);
			if (AssetTimeAfterUpdate > AssetLength)
			{
				const FPoseSearchDatabaseSequence& DbSequence = Database->Sequences[InOutMotionMatchingState.DbSequenceIdx];
				int32 FollowUpDbSequenceIdx = Database->Sequences.IndexOfByPredicate([&](const FPoseSearchDatabaseSequence& Entry) { return Entry.Sequence == DbSequence.FollowUpSequence; });
				if (FollowUpDbSequenceIdx != INDEX_NONE)
				{
					const FPoseSearchDatabaseSequence& FollowUpDbSequence = Database->Sequences[FollowUpDbSequenceIdx];
					float FollowUpAssetTime = AssetTimeAfterUpdate - AssetLength;
					int32 FollowUpPoseIdx = Database->GetPoseIndexFromAssetTime(FollowUpDbSequenceIdx, FollowUpAssetTime);

					UE::PoseSearch::FDbSearchResult FollowUpResult;
					FollowUpResult.DbSequenceIdx = FollowUpDbSequenceIdx;
					FollowUpResult.PoseIdx = FollowUpPoseIdx;
					FollowUpResult.TimeOffsetSeconds = FollowUpAssetTime;
					InOutMotionMatchingState.JumpToPose(FollowUpResult);
					RequestInertialBlend(Context, Settings.BlendTime);
					InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToPose;
				}
			}
		}
	}

	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose))
	{
		InOutMotionMatchingState.ElapsedPoseJumpTime += DeltaTime;
	}

#if UE_POSE_SEARCH_TRACE_ENABLED
	if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
	{
		UE::PoseSearch::FTraceMotionMatchingState TraceState;
		if ((InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
		{
			TraceState.Flags |= UE::PoseSearch::FTraceMotionMatchingState::EFlags::FollowupAnimation;
		}

		TraceState.ElapsedPoseJumpTime = InOutMotionMatchingState.ElapsedPoseJumpTime;
		// @TODO: Change this to only be the previous query, not persistently updated (i.e. if throttled)?
		TraceState.QueryVector = InOutMotionMatchingState.ComposedQuery.GetValues();
		TraceState.QueryVectorNormalized = InOutMotionMatchingState.ComposedQuery.GetNormalizedValues();
		TraceState.BiasWeights = InOutMotionMatchingState.WeightsContext.GetGroupWeights(0)->Weights;
		TraceState.DbPoseIdx = InOutMotionMatchingState.DbPoseIdx;
		TraceState.DatabaseId = FObjectTrace::GetObjectId(Database);
		UE_TRACE_POSE_SEARCH_MOTION_MATCHING_STATE(Context, TraceState)
	}
#endif
}

void UPoseSearchLibrary::UpdateMotionMatchingForSequencePlayer(const FAnimUpdateContext& AnimUpdateContext
	, const FSequencePlayerReference& SequencePlayer
	, const UPoseSearchDatabase* Database
	, const FTrajectorySampleRange& Trajectory
	, const FMotionMatchingSettings& Settings
	, UPARAM(ref) FMotionMatchingState& InOutMotionMatchingState
)
{
	if (const FAnimationUpdateContext* AnimationUpdateContext = AnimUpdateContext.GetContext())
	{
		if (FAnimNode_SequencePlayer* SequencePlayerNode = SequencePlayer.GetAnimNodePtr<FAnimNode_SequencePlayer>())
		{
			// Update with the sequence player's current time.
			InOutMotionMatchingState.AssetPlayerTime = SequencePlayerNode->GetAccumulatedTime();

			// Execute core motion matching algorithm and retain across frame state
			UpdateMotionMatchingState(*AnimationUpdateContext, Database, Trajectory, Settings, InOutMotionMatchingState);

			// If a new pose is requested, jump to the pose by updating the embedded sequence player node
			if ((InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
			{
				const FPoseSearchDatabaseSequence& ResultDbSequence = Database->Sequences[InOutMotionMatchingState.DbSequenceIdx];
				SequencePlayerNode->SetSequence(Cast<UAnimSequenceBase>(ResultDbSequence.Sequence));
				SequencePlayerNode->SetAccumulatedTime(InOutMotionMatchingState.AssetPlayerTime);
				SequencePlayerNode->SetLoopAnimation(ResultDbSequence.bLoopAnimation);
				SequencePlayerNode->SetPlayRate(1.f);
			}
		}
		else
		{
			UE_LOG(LogPoseSearchLibrary, Warning, TEXT("UpdateMotionMatchingForSequencePlayer must be called on a Sequence Player node"));
		}
	}
	else
	{
		UE_LOG(LogPoseSearchLibrary, Warning, TEXT("UpdateMotionMatchingForSequencePlayer called with invalid context"));
	}
}