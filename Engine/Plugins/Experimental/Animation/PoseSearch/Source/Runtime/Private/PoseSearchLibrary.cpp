// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimRootMotionProvider.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Trace/PoseSearchTraceLogger.h"

#define LOCTEXT_NAMESPACE "PoseSearchLibrary"

namespace UE::PoseSearch {

static void ComputeDatabaseSequenceFilter(
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

static void ComputeDatabaseBlendSpaceFilter(
	const UPoseSearchDatabase* Database,
	const FGameplayTagQuery* Query,
	TArray<bool>& OutDbBlendSpaceFilter)
{
	OutDbBlendSpaceFilter.SetNum(Database->BlendSpaces.Num());

	if (Query)
	{
		for (int BlendSpaceIdx = 0; BlendSpaceIdx < Database->BlendSpaces.Num(); ++BlendSpaceIdx)
		{
			OutDbBlendSpaceFilter[BlendSpaceIdx] = Query->Matches(Database->BlendSpaces[BlendSpaceIdx].GroupTags);
		}
	}
	else
	{
		for (int BlendSpaceIdx = 0; BlendSpaceIdx < Database->BlendSpaces.Num(); ++BlendSpaceIdx)
		{
			OutDbBlendSpaceFilter[BlendSpaceIdx] = true;
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

	Result.PoseIdx = INDEX_NONE;

	const FPoseSearchIndexAsset* SearchIndexAsset = State.GetCurrentSearchIndexAsset();

	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		const FPoseSearchDatabaseSequence& DbSequence = State.CurrentDatabase->GetSequenceSourceAsset(SearchIndexAsset);
		const float AssetLength = DbSequence.Sequence->GetPlayLength();
		const float DeltaTime = UpdateContext.GetDeltaTime();

		float SteppedTime = State.AssetPlayerTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DbSequence.bLoopAnimation,
			DeltaTime,
			SteppedTime,
			AssetLength);

		if (AdvanceType != ETAA_Finished)
		{
			// if the sampling interval doesn't contain the new time, there's no pose to return.
			if (SearchIndexAsset->SamplingInterval.Contains(SteppedTime))
			{
				// we can continue ticking the same sequence forward
				Result.PoseIdx = State.CurrentDatabase->GetPoseIndexFromTime(State.AssetPlayerTime, SearchIndexAsset);
				Result.SearchIndexAsset = SearchIndexAsset;
				Result.AssetTime = State.AssetPlayerTime;
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
				const int32 FollowUpPoseIdx = State.CurrentDatabase->GetPoseIndexFromTime(
					FollowUpAssetTime,
					FollowUpSearchIndexAsset);
				const FFloatInterval SamplingRange = FollowUpSearchIndexAsset->SamplingInterval;

				Result.PoseIdx = FollowUpPoseIdx;
				Result.SearchIndexAsset = FollowUpSearchIndexAsset;
				Result.AssetTime =
					SamplingRange.Min +
					(State.CurrentDatabase->Schema->SamplingInterval *
						(Result.PoseIdx - FollowUpSearchIndexAsset->FirstPoseIdx));
				bJumpRequired = true;
			}
		}
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		const FPoseSearchDatabaseBlendSpace& DbBlendSpace = State.CurrentDatabase->GetBlendSpaceSourceAsset(SearchIndexAsset);
		const float DeltaTime = UpdateContext.GetDeltaTime();

		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		DbBlendSpace.BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset->BlendParameters, BlendSamples, TriangulationIndex, true);

		float PlayLength = DbBlendSpace.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

		// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
		// to a real time before we advance it
		float RealTime = State.AssetPlayerTime * PlayLength;		
		float SteppedTime = RealTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DbBlendSpace.bLoopAnimation,
			DeltaTime,
			SteppedTime,
			PlayLength);

		if (AdvanceType != ETAA_Finished)
		{
			// if the sampling interval doesn't contain the new time, there's no pose to return.
			if (SearchIndexAsset->SamplingInterval.Contains(SteppedTime))
			{
				// we can continue ticking the same sequence forward
				Result.PoseIdx = State.CurrentDatabase->GetPoseIndexFromTime(RealTime, SearchIndexAsset);
				Result.SearchIndexAsset = SearchIndexAsset;
				Result.AssetTime = State.AssetPlayerTime;
			}
		}
	}
	else
	{
		checkNoEntry();
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
	AssetPlayerTime = Result.AssetTime;

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

	// Jump to the candidate pose if the stepper couldn't continue to the next frame
	if (!PoseStepper.CanContinue())
	{
		FSearchResult Result = Search(SearchContext);
		InOutMotionMatchingState.JumpToPose(Context, Settings, Result);
	}

	// Do a search when enough time has elapsed since the last pose jump
	else if ((InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime))
	{
		// Determine how much the updated query vector deviates from the current pose vector
		FPoseCost CurrentPoseCost;
		if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
		{
			const FPoseSearchIndexAsset* SearchIndexAsset = &Database->SearchIndex.Assets[InOutMotionMatchingState.SearchIndexAssetIdx];
			CurrentPoseCost = ComparePoses(InOutMotionMatchingState.DbPoseIdx, SearchContext, SearchIndexAsset->SourceGroupIdx);
		}

		// Search the database for the nearest match to the updated query vector
		FSearchResult Result = Search(SearchContext);

		// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
		check(Result.PoseCost.Dissimilarity >= 0.0f);
		bool bBetterPose = true;
		if (CurrentPoseCost.IsValid())
		{
			if ((CurrentPoseCost.TotalCost <= Result.PoseCost.TotalCost) || (CurrentPoseCost.Dissimilarity <= Result.PoseCost.Dissimilarity))
			{
				bBetterPose = false;
			}
			else
			{
				checkSlow(CurrentPoseCost.Dissimilarity > 0.0f && CurrentPoseCost.Dissimilarity > Result.PoseCost.Dissimilarity);
				const float RelativeSimilarityGain = -1.0f * (Result.PoseCost.Dissimilarity - CurrentPoseCost.Dissimilarity) / CurrentPoseCost.Dissimilarity;
				bBetterPose = RelativeSimilarityGain >= Settings.MinPercentImprovement / 100.0f;
			}
		}

		// Ignore the candidate poses from the same anim when they are too near to the current pose
		bool bNearbyPose = false;
		const FPoseSearchIndexAsset* StateSearchIndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset();
		if (StateSearchIndexAsset == Result.SearchIndexAsset)
		{
			// We need to check in terms of PoseIdx rather than AssetTime because
			// for blendspaces, AssetTime is not in seconds, but in the normalized range 
			// [0, 1] so comparing to `PoseJumpThresholdTime` will not make sense		
			bNearbyPose = FMath::Abs(InOutMotionMatchingState.DbPoseIdx - Result.PoseIdx) * Database->Schema->SamplingInterval < Settings.PoseJumpThresholdTime;

			// Handle looping anims when checking for the pose being too close
			if (!bNearbyPose && Database->IsSourceAssetLooping(StateSearchIndexAsset))
			{
				bNearbyPose = FMath::Abs(StateSearchIndexAsset->NumPoses - InOutMotionMatchingState.DbPoseIdx - Result.PoseIdx) * Database->Schema->SamplingInterval < Settings.PoseJumpThresholdTime;
			}
		}

		// Start playback from the candidate pose if we determined it was a better option
		if (bBetterPose && !bNearbyPose)
		{
			InOutMotionMatchingState.JumpToPose(Context, Settings, Result);
		}
	}

	// Jump to the pose stepper's next anim if we didn't jump to a pose as a result of the search above
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
		&& PoseStepper.CanContinue()
		&& PoseStepper.bJumpRequired)
	{
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
		float SimLinearVelocity, SimAngularVelocity, AnimLinearVelocity, AnimAngularVelocity;

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

			const FTransform SimDelta = CurrSample.Transform.GetRelativeTransform(PrevSample.Transform);

			SimLinearVelocity = SimDelta.GetTranslation().Size() / DeltaTime;
			SimAngularVelocity = FMath::RadiansToDegrees(SimDelta.GetRotation().GetAngle()) / DeltaTime;

			// animation

			const FTransform AnimDelta = InOutMotionMatchingState.RootMotionTransformDelta;

			AnimLinearVelocity = AnimDelta.GetTranslation().Size() / DeltaTime;
			AnimAngularVelocity = FMath::RadiansToDegrees(AnimDelta.GetRotation().GetAngle()) / DeltaTime;
		}
		else
		{
			SimLinearVelocity = 0.0f;
			SimAngularVelocity = 0.0f;
			AnimLinearVelocity = 0.0f;
			AnimAngularVelocity = 0.0f;
		}

		TArray<bool> DatabaseSequenceFilter;
		ComputeDatabaseSequenceFilter(Database, DatabaseTagQuery, DatabaseSequenceFilter);

		TArray<bool> DatabaseBlendSpaceFilter;
		ComputeDatabaseBlendSpaceFilter(Database, DatabaseTagQuery, DatabaseBlendSpaceFilter);

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
		TraceState.DatabaseBlendSpaceFilter = DatabaseBlendSpaceFilter;
		UE_TRACE_POSE_SEARCH_MOTION_MATCHING_STATE(Context, TraceState)
	}
#endif
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