// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Trace/PoseSearchTraceLogger.h"

#define LOCTEXT_NAMESPACE "PoseSearchLibrary"

DEFINE_LOG_CATEGORY_STATIC(LogPoseSearchLibrary, Verbose, All);

namespace UE::PoseSearch {

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

void ComputeDatabaseSequenceFilter(
	const UPoseSearchDatabase* Database, 
	const FGameplayTagQuery* Query, 
	TArray<bool>& OutDbSequenceFilter)
{
	OutDbSequenceFilter.SetNum(Database->Sequences.Num());

	if (Query)
	{
		for (int SeqIdx = 0; SeqIdx < Database->Sequences.Num(); ++SeqIdx)
		{
			OutDbSequenceFilter[SeqIdx] = Query->Matches(Database->Sequences[SeqIdx].GroupTags);
		}
	}
	else
	{
		for (int SeqIdx = 0; SeqIdx < Database->Sequences.Num(); ++SeqIdx)
		{
			OutDbSequenceFilter[SeqIdx] = true;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// FMotionMatchingPoseStepper

void FMotionMatchingPoseStepper::Update(const FAnimationUpdateContext& UpdateContext, const FMotionMatchingState& State)
{
	if (State.DbPoseIdx == INDEX_NONE)
	{
		return;
	}

	check(State.SearchIndexAssetIdx != INDEX_NONE);

	const FPoseSearchIndexAsset* SearchIndexAsset = State.GetCurrentSearchIndexAsset();
	const FPoseSearchDatabaseSequence& DbSequence = State.CurrentDatabase->GetSourceAsset(SearchIndexAsset);
	const float AssetLength = DbSequence.Sequence->GetPlayLength();
	const float DeltaTime = UpdateContext.GetDeltaTime();

	float SteppedAssetTime = State.AssetPlayerTime;
	ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
		DbSequence.bLoopAnimation,
		DeltaTime,
		SteppedAssetTime,
		AssetLength);

	if (AdvanceType != ETAA_Finished)
	{
		// if the sampling interval doesn't contain the new time, there's no pose to return.
		if (SearchIndexAsset->SamplingInterval.Contains(SteppedAssetTime))
		{
			// we can continue ticking the same sequence forward
			Result.PoseIdx = State.CurrentDatabase->GetPoseIndexFromAssetTime(SteppedAssetTime, SearchIndexAsset);
			Result.SearchIndexAsset = SearchIndexAsset;
			Result.TimeOffsetSeconds = State.AssetPlayerTime;
		}
	}
	else
	{
		// check if there's a follow-up that can be used
		int32 FollowUpDbSequenceIdx = State.CurrentDatabase->Sequences.IndexOfByPredicate(
			[&](const FPoseSearchDatabaseSequence& Entry)
			{
				return Entry.Sequence == DbSequence.FollowUpSequence;
			});

		int32 FollowUpSearchIndexAssetIdx = State.CurrentDatabase->SearchIndex.Assets.IndexOfByPredicate(
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
				&State.CurrentDatabase->SearchIndex.Assets[FollowUpSearchIndexAssetIdx];
			const float FollowUpAssetTime = State.AssetPlayerTime + DeltaTime - AssetLength;
			const int32 FollowUpPoseIdx = State.CurrentDatabase->GetPoseIndexFromAssetTime(
				FollowUpAssetTime,
				FollowUpSearchIndexAsset);
			const FFloatInterval SamplingRange = FollowUpSearchIndexAsset->SamplingInterval;

			Result.PoseIdx = FollowUpPoseIdx;
			Result.SearchIndexAsset = FollowUpSearchIndexAsset;
			Result.TimeOffsetSeconds =
				SamplingRange.Min +
				(State.CurrentDatabase->Schema->SamplingInterval *
					(Result.PoseIdx - FollowUpSearchIndexAsset->FirstPoseIdx));
			bJumpRequired = true;
		}
	}
}

} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// FMotionMatchingState

bool FMotionMatchingState::InitNewDatabaseSearch(
	const UPoseSearchDatabase* Database,
	float SearchThrottleTime,
	FText* OutError
)
{
	bool bValidDatabase = Database && Database->IsValidForSearch();

	if (bValidDatabase)
	{
		DbPoseIdx = INDEX_NONE;
		SearchIndexAssetIdx = INDEX_NONE;
		ElapsedPoseJumpTime = SearchThrottleTime;
		AssetPlayerTime = 0.0f;
		CurrentDatabase = Database;

		if (!ComposedQuery.IsInitializedForSchema(Database->Schema))
		{
			ComposedQuery.Init(Database->Schema);
		}
	}

	if (!bValidDatabase && OutError)
	{
		if (Database)
		{
			*OutError = FText::Format(LOCTEXT("InvalidDatabase", "Invalid database for motion matching. Try re-saving {0}."), FText::FromString(Database->GetPathName()));
		}
		else
		{
			*OutError = LOCTEXT("NoDatabase", "No database provided for motion matching.");
		}
	}

	return bValidDatabase;
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

void FMotionMatchingState::JumpToPose(const FAnimationUpdateContext& Context, const FMotionMatchingSettings& Settings, const UE::PoseSearch::FSearchResult& Result)
{
	// Remember which pose and sequence we're playing from the database
	DbPoseIdx = Result.PoseIdx;
	SearchIndexAssetIdx = CurrentDatabase->SearchIndex.FindAssetIndex(Result.SearchIndexAsset);

	ElapsedPoseJumpTime = 0.0f;
	AssetPlayerTime = Result.TimeOffsetSeconds;

	const float JumpBlendTime = ComputeJumpBlendTime(Result, Settings);
	RequestInertialBlend(Context, JumpBlendTime);
	Flags |= EMotionMatchingFlags::JumpedToPose;
}

void UpdateMotionMatchingState(
	const FAnimationUpdateContext& Context,
	const UPoseSearchDatabase* Database,
	const FGameplayTagQuery* DatabaseTagQuery,
	const FTrajectorySampleRange& Trajectory,
	const FMotionMatchingSettings& Settings,
	FMotionMatchingState& InOutMotionMatchingState
)
{
	using namespace UE::PoseSearch;

	if (!Database)
	{
		Context.LogMessage(EMessageSeverity::Error, LOCTEXT("NoDatabase", "No database provided for motion matching."));
		return;
	}

	InOutMotionMatchingState.Flags = EMotionMatchingFlags::None;
	if (InOutMotionMatchingState.CurrentDatabase != Database)
	{
		FText InitError;
		if (!InOutMotionMatchingState.InitNewDatabaseSearch(Database, Settings.SearchThrottleTime, &InitError))
		{
			Context.LogMessage(EMessageSeverity::Error, InitError);
			return;
		}
	}

	const float DeltaTime = Context.GetDeltaTime();

	// Step the pose forward
	FMotionMatchingPoseStepper PoseStepper;
	PoseStepper.Update(Context, InOutMotionMatchingState);
	if (PoseStepper.CanContinue())
	{
		InOutMotionMatchingState.DbPoseIdx = PoseStepper.Result.PoseIdx;
		InOutMotionMatchingState.SearchIndexAssetIdx = 
			InOutMotionMatchingState.CurrentDatabase->SearchIndex.FindAssetIndex(PoseStepper.Result.SearchIndexAsset);
	}

	// Build the search query
	if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
	{
		// Copy search query directly from the database if we have an active pose
		InOutMotionMatchingState.ComposedQuery.CopyFromSearchIndex(Database->SearchIndex, InOutMotionMatchingState.DbPoseIdx);
	}
	else
	{
		// When we don't have an active pose, initialize the search query from the pose history provider
		IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<IPoseHistoryProvider>();
		if (PoseHistoryProvider)
		{
			FPoseHistory& History = PoseHistoryProvider->GetPoseHistory();
			InOutMotionMatchingState.ComposedQuery.TrySetPoseFeatures(
				&History, 
				Context.AnimInstanceProxy->GetRequiredBones());
		}
	}

	// Update features in the query with the latest inputs
	InOutMotionMatchingState.ComposeQuery(Database, Trajectory);

	FSearchContext SearchContext;
	SearchContext.SetSource(InOutMotionMatchingState.CurrentDatabase.Get());
	SearchContext.QueryValues = InOutMotionMatchingState.ComposedQuery.GetNormalizedValues();
	SearchContext.WeightsContext = &InOutMotionMatchingState.WeightsContext;
	SearchContext.DatabaseTagQuery = DatabaseTagQuery;
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
	FPoseCost CurrentPoseCost;
	if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
	{
		CurrentPoseCost = ComparePoses(InOutMotionMatchingState.DbPoseIdx, SearchContext);
	}

	// Search the database for the nearest match to the updated query vector
	FSearchResult Result = Search(SearchContext);

	if (Result.IsValid())
	{
		// Jump to the candidate pose if the stepper couldn't continue to the next frame
		if (!PoseStepper.CanContinue())
		{
			InOutMotionMatchingState.JumpToPose(Context, Settings, Result);
		}

		// Consider the candidate pose when enough time has elapsed since the last pose jump
		else if ((InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime))
		{
			// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
			check(Result.PoseCost.Dissimilarity >= 0.0f && CurrentPoseCost.Dissimilarity >= 0.0f);
			bool bBetterPose = Result.PoseCost.Dissimilarity * (1.0f + (Settings.MinPercentImprovement / 100.0f)) < CurrentPoseCost.Dissimilarity;

			// Ignore the candidate poses from the same anim when they are too near to the current pose
			bool bNearbyPose = false;
			const FPoseSearchIndexAsset* StateSearchIndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset();
			if (StateSearchIndexAsset == Result.SearchIndexAsset)
			{
				const FPoseSearchDatabaseSequence& ResultDbSequence = Database->GetSourceAsset(Result.SearchIndexAsset);
				bNearbyPose = FMath::Abs(InOutMotionMatchingState.AssetPlayerTime - Result.TimeOffsetSeconds) < Settings.PoseJumpThresholdTime;

				// Handle looping anims when checking for the pose being too close
				if (!bNearbyPose && ResultDbSequence.bLoopAnimation)
				{
					const float AssetLength = ResultDbSequence.Sequence->GetPlayLength();
					bNearbyPose = FMath::Abs(AssetLength - InOutMotionMatchingState.AssetPlayerTime - Result.TimeOffsetSeconds) < Settings.PoseJumpThresholdTime;
				}
			}

			// Start playback from the candidate pose if we determined it was a better option
			if (bBetterPose && !bNearbyPose)
			{
				InOutMotionMatchingState.JumpToPose(Context, Settings, Result);
			}
		}
	}

	// Jump to the pose stepper's next anim if we didn't jump to a pose as a result of the search above
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
		&& PoseStepper.CanContinue()
		&& PoseStepper.bJumpRequired)
	{
		const FPoseSearchDatabaseSequence& DbSequence = Database->GetSourceAsset(PoseStepper.Result.SearchIndexAsset);
		InOutMotionMatchingState.JumpToPose(Context, Settings, PoseStepper.Result);
		InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToFollowUp;
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

		TArray<bool> DatabaseSequenceFilter;
		ComputeDatabaseSequenceFilter(Database, DatabaseTagQuery, DatabaseSequenceFilter);

		FTraceMotionMatchingState TraceState;
		if (EnumHasAnyFlags(InOutMotionMatchingState.Flags, EMotionMatchingFlags::JumpedToFollowUp))
		{
			TraceState.Flags |= FTraceMotionMatchingState::EFlags::FollowupAnimation;
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
		TraceState.DatabaseSequenceFilter = DatabaseSequenceFilter;
		UE_TRACE_POSE_SEARCH_MOTION_MATCHING_STATE(Context, TraceState)
	}
#endif
}

void UPoseSearchLibrary::UpdateMotionMatchingForSequencePlayer(
	const FAnimUpdateContext& AnimUpdateContext,
	const FSequencePlayerReference& SequencePlayer,
	const UPoseSearchDatabase* Database,
	const FTrajectorySampleRange& Trajectory,
	const FMotionMatchingSettings& Settings,
	UPARAM(ref) FMotionMatchingState& InOutMotionMatchingState
)
{
	if (const FAnimationUpdateContext* AnimationUpdateContext = AnimUpdateContext.GetContext())
	{
		if (FAnimNode_SequencePlayer* SequencePlayerNode = SequencePlayer.GetAnimNodePtr<FAnimNode_SequencePlayer>())
		{
			// Update with the sequence player's current time.
			InOutMotionMatchingState.AssetPlayerTime = SequencePlayerNode->GetAccumulatedTime();

			// Execute core motion matching algorithm and retain across frame state
			UpdateMotionMatchingState(
				*AnimationUpdateContext, 
				Database, 
				nullptr, // DatabaseTagQuery
				Trajectory, 
				Settings, 
				InOutMotionMatchingState);

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
	const FMotionMatchingSettings& Settings
) const
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

#undef LOCTEXT_NAMESPACE