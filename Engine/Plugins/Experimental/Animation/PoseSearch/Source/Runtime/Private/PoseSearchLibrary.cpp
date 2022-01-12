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
	SearchIndexAssetIdx = INDEX_NONE;
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

void FMotionMatchingState::JumpToPose(const UE::PoseSearch::FSearchResult& Result)
{
	// Remember which pose and sequence we're playing from the database
	DbPoseIdx = Result.PoseIdx;
	SearchIndexAssetIdx = CurrentDatabase->SearchIndex.FindAssetIndex(Result.SearchIndexAsset);

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

void ComputeCurrentVelocityMagnitudes(
	const FMotionMatchingState& InOutMotionMatchingState, 
	const FTrajectorySampleRange& Trajectory, 
	const float DeltaTime, 
	float& SimLinearVelocity, float& SimAngularVelocity, 
	float& AnimLinearVelocity, float& AnimAngularVelocity)
{
	if (DeltaTime > SMALL_NUMBER)
	{
		// simulation

		int32 FirstIdx = 0;
		const FTrajectorySample PrevSample = FTrajectorySampleRange::IterSampleTrajectory(
			Trajectory.Samples,
			ETrajectorySampleDomain::Time,
			-DeltaTime, FirstIdx);

		const FTrajectorySample CurrSample = FTrajectorySampleRange::IterSampleTrajectory(
			Trajectory.Samples,
			ETrajectorySampleDomain::Time,
			0.0f, FirstIdx);

		const FTransform RelativeMotion = CurrSample.Transform.GetRelativeTransform(PrevSample.Transform);

		SimLinearVelocity = RelativeMotion.GetTranslation().Size() / DeltaTime;
		SimAngularVelocity = FMath::RadiansToDegrees(RelativeMotion.GetRotation().GetAngle()) / DeltaTime;

		// animation
		if (const FPoseSearchIndexAsset* IndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset())
		{
			const FPoseSearchDatabaseSequence& DbSequence =
				InOutMotionMatchingState.CurrentDatabase->GetSourceAsset(IndexAsset);
			const UAnimSequence* SourceSequence = DbSequence.Sequence;

			const FTransform AnimRootMotion = SourceSequence->ExtractRootMotion(
				InOutMotionMatchingState.AssetPlayerTime,
				-DeltaTime,
				DbSequence.bLoopAnimation);

			AnimLinearVelocity = AnimRootMotion.GetTranslation().Size() / DeltaTime;
			AnimAngularVelocity = FMath::RadiansToDegrees(AnimRootMotion.GetRotation().GetAngle()) / DeltaTime;
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
		UE::PoseSearch::FMotionMatchingContinuityParams ContinuityParameters = 
			InOutMotionMatchingState.ComputeContinuityParameters(Context);
		const bool bCanContinue = ContinuityParameters.IsValid();
		if (bCanContinue)
		{
			InOutMotionMatchingState.DbPoseIdx = ContinuityParameters.Result.PoseIdx;
			InOutMotionMatchingState.SearchIndexAssetIdx = 
				InOutMotionMatchingState.CurrentDatabase->SearchIndex.FindAssetIndex(ContinuityParameters.Result.SearchIndexAsset);
		}

		// build query
		if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
		{
			InOutMotionMatchingState.ComposedQuery.CopyFromSearchIndex(Database->SearchIndex, InOutMotionMatchingState.DbPoseIdx);
		}
		else
		{
			UE::PoseSearch::IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<UE::PoseSearch::IPoseHistoryProvider>();
			if (PoseHistoryProvider)
			{
				UE::PoseSearch::FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
				InOutMotionMatchingState.ComposedQuery.TrySetPoseFeatures(
					&History, 
					Context.AnimInstanceProxy->GetRequiredBones());
			}
		}

		// Update features in the query with the latest inputs
		InOutMotionMatchingState.ComposeQuery(Database, Trajectory);

		UE::PoseSearch::FSearchContext SearchContext;
		SearchContext.SetSource(InOutMotionMatchingState.CurrentDatabase.Get());
		SearchContext.QueryValues = InOutMotionMatchingState.ComposedQuery.GetNormalizedValues();
		SearchContext.WeightsContext = &InOutMotionMatchingState.WeightsContext;
		if (const FPoseSearchIndexAsset* CurrentIndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset())
		{
			SearchContext.QueryMirrorRequest =
				CurrentIndexAsset->bMirrored ?
				EPoseSearchBooleanRequest::TrueValue :
				EPoseSearchBooleanRequest::FalseValue;
		}

		// Update weight groups
		InOutMotionMatchingState.WeightsContext.Update(Settings.Weights, Database);

		// Determine how much the updated query vector deviates from the current pose vector
		float CurrentDissimilarity = MAX_flt;
		if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
		{
			CurrentDissimilarity = UE::PoseSearch::ComparePoses(InOutMotionMatchingState.DbPoseIdx, SearchContext);
		}

		// Search the database for the nearest match to the updated query vector
		UE::PoseSearch::FSearchResult Result = UE::PoseSearch::Search(SearchContext);
		if (Result.IsValid() && ((InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime) || !bCanContinue))
		{
			if (!bCanContinue)
			{
				const float JumpBlendTime = InOutMotionMatchingState.ComputeJumpBlendTime(Result, Settings);
				InOutMotionMatchingState.JumpToPose(Result);
				RequestInertialBlend(Context, JumpBlendTime);
				InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToPose;
			}
			else
			{
				// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
				bool bBetterPose = Result.Dissimilarity * (1.0f + (Settings.MinPercentImprovement / 100.0f)) < CurrentDissimilarity;

				// We'll ignore the candidate pose if it is from the same animation and too near to the current pose
				bool bNearbyPose = false;
				const FPoseSearchIndexAsset* StateSearchIndexAsset =  InOutMotionMatchingState.GetCurrentSearchIndexAsset();
				if (StateSearchIndexAsset == Result.SearchIndexAsset)
				{
					const FPoseSearchDatabaseSequence& ResultDbSequence = Database->GetSourceAsset(Result.SearchIndexAsset);

					// Consider the candidate pose nearby if the animation loops, regardless of how far away the pose is
					if (ResultDbSequence.bLoopAnimation)
					{
						bNearbyPose = true;
					}

					// Otherwise consider the pose nearby if it is within the pose jump threshold
					else
					{
						bNearbyPose = FMath::Abs(InOutMotionMatchingState.AssetPlayerTime - Result.TimeOffsetSeconds) < Settings.PoseJumpThresholdTime;
					}
				}

				// Start playback from the candidate pose if we determined it was a better option
				if (bBetterPose && !bNearbyPose)
				{
					const float JumpBlendTime = InOutMotionMatchingState.ComputeJumpBlendTime(Result, Settings);
					InOutMotionMatchingState.JumpToPose(Result);
					RequestInertialBlend(Context, JumpBlendTime);
					InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToPose;
				}
			}
		}

		// Continue with the follow up sequence if we're finishing a one shot anim
		if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
			&& bCanContinue
			&& ContinuityParameters.bJumpRequired)
		{
			InOutMotionMatchingState.JumpToPose(ContinuityParameters.Result);
			RequestInertialBlend(Context, Settings.BlendTime);
			InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToPose;
		}
	}

	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose))
	{
		InOutMotionMatchingState.ElapsedPoseJumpTime += DeltaTime;
	}

#if UE_POSE_SEARCH_TRACE_ENABLED
	if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
	{

		float SimLinearVelocity = -1.0f;
		float SimAngularVelocity = -1.0f;
		float AnimLinearVelocity = -1.0f;
		float AnimAngularVelocity = -1.0f;
		ComputeCurrentVelocityMagnitudes(
			InOutMotionMatchingState, 
			Trajectory, 
			DeltaTime, 
			SimLinearVelocity, SimAngularVelocity, 
			AnimLinearVelocity, AnimAngularVelocity);

		UE::PoseSearch::FTraceMotionMatchingState TraceState;
		if ((InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose) == EMotionMatchingFlags::JumpedToPose)
		{
			TraceState.Flags |= UE::PoseSearch::FTraceMotionMatchingState::EFlags::FollowupAnimation;
		}

		TraceState.ElapsedPoseJumpTime = InOutMotionMatchingState.ElapsedPoseJumpTime;
		// @TODO: Change this to only be the previous query, not persistently updated (i.e. if throttled)?
		TraceState.QueryVector = InOutMotionMatchingState.ComposedQuery.GetValues();
		TraceState.QueryVectorNormalized = InOutMotionMatchingState.ComposedQuery.GetNormalizedValues();
		TraceState.Weights = Settings.Weights;
		TraceState.DbPoseIdx = InOutMotionMatchingState.DbPoseIdx;
		TraceState.DatabaseId = FObjectTrace::GetObjectId(Database);

		TraceState.AssetPlayerTime = InOutMotionMatchingState.AssetPlayerTime;
		TraceState.DeltaTime = DeltaTime;
		TraceState.SimLinearVelocity = SimLinearVelocity;
		TraceState.SimAngularVelocity = SimAngularVelocity;
		TraceState.AnimLinearVelocity = AnimLinearVelocity;
		TraceState.AnimAngularVelocity = AnimAngularVelocity;
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
				const FPoseSearchIndexAsset* SearchIndexAsset = &Database->SearchIndex.Assets[InOutMotionMatchingState.SearchIndexAssetIdx];
				const FPoseSearchDatabaseSequence& ResultDbSequence = Database->GetSourceAsset(SearchIndexAsset);
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

UE::PoseSearch::FMotionMatchingContinuityParams FMotionMatchingState::ComputeContinuityParameters(const FAnimationUpdateContext& Context) const
{
	UE::PoseSearch::FMotionMatchingContinuityParams ContinuityParameters;

	if (DbPoseIdx != INDEX_NONE)
	{
		check(SearchIndexAssetIdx != INDEX_NONE);

		const FPoseSearchIndexAsset* SearchIndexAsset = GetCurrentSearchIndexAsset();
		const FPoseSearchDatabaseSequence& DbSequence = CurrentDatabase->GetSourceAsset(SearchIndexAsset);
		const float AssetLength = DbSequence.Sequence->GetPlayLength();
		const float DeltaTime = Context.GetDeltaTime();

		float ContinuityAssetTime = AssetPlayerTime;
		ETypeAdvanceAnim ContinuityAdvanceType = FAnimationRuntime::AdvanceTime(
			DbSequence.bLoopAnimation,
			DeltaTime,
			ContinuityAssetTime,
			AssetLength);

		if (ContinuityAdvanceType != ETAA_Finished)
		{
			// if the sampling interval doesn't contain the new time, there's no pose to return.
			if (SearchIndexAsset->SamplingInterval.Contains(ContinuityAssetTime))
			{
				// we can continue ticking the same sequence forward
				ContinuityParameters.Result.PoseIdx = CurrentDatabase->GetPoseIndexFromAssetTime(ContinuityAssetTime, SearchIndexAsset);
				ContinuityParameters.Result.SearchIndexAsset = SearchIndexAsset;
				ContinuityParameters.Result.TimeOffsetSeconds = AssetPlayerTime;
			}
		}
		else
		{
			// check if there's a follow-up that can be used
			int32 FollowUpDbSequenceIdx = CurrentDatabase->Sequences.IndexOfByPredicate(
				[&](const FPoseSearchDatabaseSequence& Entry)
			{
				return Entry.Sequence == DbSequence.FollowUpSequence;
			});

			int32 FollowUpSearchIndexAssetIdx = CurrentDatabase->SearchIndex.Assets.IndexOfByPredicate(
				[&](const FPoseSearchIndexAsset& Entry)
			{
				const bool bIsMatch = 
					Entry.SourceAssetIdx == FollowUpDbSequenceIdx && 
					Entry.bMirrored == SearchIndexAsset->bMirrored &&
					Entry.SamplingInterval.Contains(0.0f);
				return bIsMatch;
			});

			if (FollowUpSearchIndexAssetIdx != INDEX_NONE)
			{
				const FPoseSearchIndexAsset* FollowUpSearchIndexAsset = 
					&CurrentDatabase->SearchIndex.Assets[FollowUpSearchIndexAssetIdx];
				const float FollowUpAssetTime = AssetPlayerTime + DeltaTime - AssetLength;
				const int32 FollowUpPoseIdx = CurrentDatabase->GetPoseIndexFromAssetTime(
					FollowUpAssetTime,
					FollowUpSearchIndexAsset);
				const FFloatInterval SamplingRange = FollowUpSearchIndexAsset->SamplingInterval;

				ContinuityParameters.Result.PoseIdx = FollowUpPoseIdx;
				ContinuityParameters.Result.SearchIndexAsset = FollowUpSearchIndexAsset;
				ContinuityParameters.Result.TimeOffsetSeconds =
					SamplingRange.Min + 
					(CurrentDatabase->Schema->SamplingInterval *
					 (ContinuityParameters.Result.PoseIdx - FollowUpSearchIndexAsset->FirstPoseIdx));
				ContinuityParameters.bJumpRequired = true;
			}
		}
	}
	return ContinuityParameters;
}

const FPoseSearchIndexAsset* FMotionMatchingState::GetCurrentSearchIndexAsset() const
{
	if (!CurrentDatabase->SearchIndex.Assets.IsValidIndex(SearchIndexAssetIdx))
	{
		return nullptr;
	}

	return &CurrentDatabase->SearchIndex.Assets[SearchIndexAssetIdx];
}

float FMotionMatchingState::ComputeJumpBlendTime(
	const UE::PoseSearch::FSearchResult& Result, 
	const FMotionMatchingSettings& Settings) const
{
	const FPoseSearchIndexAsset* SearchIndexAsset = GetCurrentSearchIndexAsset();
	// Use alternate blend time when changing between mirrored and unmirrored
	float JumpBlendTime = Settings.BlendTime;
	if ((SearchIndexAsset != nullptr) && (Settings.MirrorChangeBlendTime > 0.0f))
	{
		if (Result.SearchIndexAsset->bMirrored != SearchIndexAsset->bMirrored)
		{
			JumpBlendTime = Settings.MirrorChangeBlendTime;
		}
	}

	return JumpBlendTime;
}
