// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace.h"
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

} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// FMotionMatchingState

bool FMotionMatchingState::InitNewDatabaseSearch(
	const UPoseSearchDatabase* Database,
	FText* OutError
)
{
	bool bValidDatabase = Database && Database->IsValidForSearch();

	if (bValidDatabase)
	{
		CurrentDatabase = Database;
#if WITH_EDITOR
		CurrentSearchIndexHash = Database->GetSearchIndexHash();
#endif
	}
	else
	{
		CurrentDatabase = nullptr;
#if WITH_EDITOR
		CurrentSearchIndexHash = FIoHash::Zero;
#endif	
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

	Reset();

	return bValidDatabase;
}

void FMotionMatchingState::Reset()
{
	DbPoseIdx = INDEX_NONE;
	SearchIndexAssetIdx = INDEX_NONE;
	AssetPlayerTime = 0.0f;
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseJumpTime = INFINITY;

	if (CurrentDatabase.IsValid() && CurrentDatabase->IsValidForSearch())
	{
		ComposedQuery.Init(CurrentDatabase->Schema);
	}
	else
	{
		ComposedQuery.Reset();
	}

}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
	if (SearchIndexAssetIdx == INDEX_NONE || !CurrentDatabase.IsValid() || !(CurrentDatabase->GetSearchIndex()))
	{
		return;
	}

	const FPoseSearchIndexAsset* SearchIndexAsset = GetCurrentSearchIndexAsset();

	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		const FPoseSearchDatabaseSequence& DbSequence = CurrentDatabase->GetSequenceSourceAsset(SearchIndexAsset);

		if (SearchIndexAsset->SamplingInterval.Contains(AssetTime))
		{
			DbPoseIdx = CurrentDatabase->GetPoseIndexFromTime(AssetTime, SearchIndexAsset);
			AssetPlayerTime = AssetTime;
		}
		else
		{
			DbPoseIdx = INDEX_NONE;
			AssetPlayerTime = 0.0f;
			SearchIndexAssetIdx = INDEX_NONE;
		}
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		const FPoseSearchDatabaseBlendSpace& DbBlendSpace = CurrentDatabase->GetBlendSpaceSourceAsset(SearchIndexAsset);

		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		DbBlendSpace.BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset->BlendParameters, BlendSamples, TriangulationIndex, true);

		float PlayLength = DbBlendSpace.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

		// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
		// to a real time before we advance it
		float RealTime = AssetTime * PlayLength;

		if (SearchIndexAsset->SamplingInterval.Contains(RealTime))
		{
			DbPoseIdx = CurrentDatabase->GetPoseIndexFromTime(RealTime, SearchIndexAsset);
			AssetPlayerTime = AssetTime;
		}
		else
		{
			DbPoseIdx = INDEX_NONE;
			AssetPlayerTime = 0.0f;
			SearchIndexAssetIdx = INDEX_NONE;
		}
	}
	else
	{
		checkNoEntry();
	}
}

bool FMotionMatchingState::CanAdvance(float DeltaTime, bool& bOutAdvanceToFollowUpAsset, UE::PoseSearch::FSearchResult& OutFollowUpAsset) const
{
	bOutAdvanceToFollowUpAsset = false;
	OutFollowUpAsset = UE::PoseSearch::FSearchResult();

	if (SearchIndexAssetIdx == INDEX_NONE || !CurrentDatabase.IsValid() || !(CurrentDatabase->GetSearchIndex()))
	{
		return false;
	}

	const FPoseSearchIndexAsset* SearchIndexAsset = GetCurrentSearchIndexAsset();

	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		const FPoseSearchDatabaseSequence& DbSequence = CurrentDatabase->GetSequenceSourceAsset(SearchIndexAsset);
		const float AssetLength = DbSequence.Sequence->GetPlayLength();

		float SteppedTime = AssetPlayerTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DbSequence.Sequence->bLoop,
			DeltaTime,
			SteppedTime,
			AssetLength);

		if (AdvanceType != ETAA_Finished)
		{
			return SearchIndexAsset->SamplingInterval.Contains(SteppedTime);
		}
		else
		{
			// check if there's a follow-up that can be used
			int32 FollowUpDbSequenceIdx = CurrentDatabase->Sequences.IndexOfByPredicate(
				[&](const FPoseSearchDatabaseSequence& Entry)
				{
					return Entry.Sequence == DbSequence.FollowUpSequence;
				});

			int32 FollowUpSearchIndexAssetIdx = CurrentDatabase->GetSearchIndex()->Assets.IndexOfByPredicate(
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
				bOutAdvanceToFollowUpAsset = true;

				const FPoseSearchIndexAsset* FollowUpSearchIndexAsset =
					&CurrentDatabase->GetSearchIndex()->Assets[FollowUpSearchIndexAssetIdx];

				// Follow up asset time will start slightly before the beginning of the sequence as 
				// this is essentially what the matching time in the corresponding main sequence is.
				// Here we are assuming that the tick will advance the asset player timer into the 
				// valid region
				const float FollowUpAssetTime = AssetPlayerTime - AssetLength;

				// There is no correspoding pose index when we switch due to what is mentioned above
				// so for now we just take whatever pose index is associated with the first frame.
				OutFollowUpAsset.PoseIdx = CurrentDatabase->GetPoseIndexFromTime(FollowUpSearchIndexAsset->SamplingInterval.Min, FollowUpSearchIndexAsset);
				OutFollowUpAsset.SearchIndexAsset = FollowUpSearchIndexAsset;
				OutFollowUpAsset.AssetTime = FollowUpAssetTime;
				return true;
			}
		}
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		const FPoseSearchDatabaseBlendSpace& DbBlendSpace = CurrentDatabase->GetBlendSpaceSourceAsset(SearchIndexAsset);

		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		DbBlendSpace.BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset->BlendParameters, BlendSamples, TriangulationIndex, true);

		float PlayLength = DbBlendSpace.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

		// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
		// to a real time before we advance it
		float RealTime = AssetPlayerTime * PlayLength;
		float SteppedTime = RealTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DbBlendSpace.BlendSpace->bLoop,
			DeltaTime,
			SteppedTime,
			PlayLength);

		if (AdvanceType != ETAA_Finished)
		{
			return SearchIndexAsset->SamplingInterval.Contains(SteppedTime);
		}
	}
	else
	{
		checkNoEntry();
	}

	return false;
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
	SearchIndexAssetIdx = CurrentDatabase->GetSearchIndex()->FindAssetIndex(Result.SearchIndexAsset);

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
	
	// Check for a switch in the database
	if (!InOutMotionMatchingState.IsCompatibleDatabase(Database))
	{
		FText InitError;
		if (!InOutMotionMatchingState.InitNewDatabaseSearch(Database, &InitError))
		{
			Context.LogMessage(EMessageSeverity::Error, InitError);
			return;
		}
	}

#if WITH_EDITOR
	if (InOutMotionMatchingState.CurrentDatabase->IsDerivedDataBuildPending())
	{
		InOutMotionMatchingState.Reset();
		Context.LogMessage(
			EMessageSeverity::Error, 
			LOCTEXT("PendingDerivedData", "Derived data build pending."));
		return;
	}
#endif

	const float DeltaTime = Context.GetDeltaTime();

	// Reset State Flags
	InOutMotionMatchingState.Flags = EMotionMatchingFlags::None;

	// Record Current Pose Index for Debugger
	int32 CurrentPoseIdx = InOutMotionMatchingState.DbPoseIdx;

	FQueryBuildingContext QueryBuildingContext(InOutMotionMatchingState.ComposedQuery);
	QueryBuildingContext.Schema = Database->Schema;
	QueryBuildingContext.History = nullptr;
	QueryBuildingContext.Trajectory = &Trajectory;

	// Build the search query. This is done even when we don't search 
	// since we still want to record it for debugging purposes.
	if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
	{
		// Copy search query directly from the database if we have an active pose
		InOutMotionMatchingState.ComposedQuery.CopyFromSearchIndex(
			*Database->GetSearchIndex(),
			InOutMotionMatchingState.DbPoseIdx);
	}
	else
	{
		InOutMotionMatchingState.ComposedQuery.ResetFeatures();
		IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<IPoseHistoryProvider>();
		if (PoseHistoryProvider)
		{
			QueryBuildingContext.History = &PoseHistoryProvider->GetPoseHistory();
		}
	}

	BuildQuery(QueryBuildingContext);
	InOutMotionMatchingState.ComposedQuery.Normalize(*Database->GetSearchIndex());

	// Check if we can advance. Includes the case where we can advance but only by switching to a follow up asset.
	bool bAdvanceToFollowUpAsset = false;
	FSearchResult FollowUpAsset;
	bool bCanAdvance = InOutMotionMatchingState.CanAdvance(Context.GetDeltaTime(), bAdvanceToFollowUpAsset, FollowUpAsset);

	// If we can't advance or enough time has elapsed since the last pose jump then search
	if (!bCanAdvance || (InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime))
	{
		// Build the search context
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
		InOutMotionMatchingState.WeightsContext.Update(Database);

		// Search the database for the nearest match to the updated query vector
		FSearchResult SearchResult = Search(SearchContext);

		if (SearchResult.IsValid())
		{
			// If the result is valid and we couldn't advance we should always jump to the search result
			if (!bCanAdvance)
			{
				InOutMotionMatchingState.JumpToPose(Context, Settings, SearchResult);
			}
			// Otherwise we need to check if the result is a good improvement over the current pose
			else
			{
				// Determine how much the updated query vector deviates from the current pose vector
				FPoseCost CurrentPoseCost;
				if (InOutMotionMatchingState.DbPoseIdx != INDEX_NONE)
				{
					const FPoseSearchIndexAsset* SearchIndexAsset = &Database->GetSearchIndex()->Assets[InOutMotionMatchingState.SearchIndexAssetIdx];
					CurrentPoseCost = ComparePoses(InOutMotionMatchingState.DbPoseIdx, SearchContext, SearchIndexAsset->SourceGroupIdx);
				}

				// Consider the search result better if it is more similar to the query than the current pose we're playing back from the database
				check(SearchResult.PoseCost.Dissimilarity >= 0.0f);
				bool bBetterPose = true;
				if (CurrentPoseCost.IsValid())
				{
					if ((CurrentPoseCost.TotalCost <= SearchResult.PoseCost.TotalCost) || (CurrentPoseCost.Dissimilarity <= SearchResult.PoseCost.Dissimilarity))
					{
						bBetterPose = false;
					}
					else
					{
						checkSlow(CurrentPoseCost.Dissimilarity > 0.0f && CurrentPoseCost.Dissimilarity > SearchResult.PoseCost.Dissimilarity);
						const float RelativeSimilarityGain = -1.0f * (SearchResult.PoseCost.Dissimilarity - CurrentPoseCost.Dissimilarity) / CurrentPoseCost.Dissimilarity;
						bBetterPose = RelativeSimilarityGain >= Settings.MinPercentImprovement / 100.0f;
					}
				}

				// Ignore the candidate poses from the same anim when they are too near to the current pose
				bool bNearbyPose = false;
				const FPoseSearchIndexAsset* StateSearchIndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset();
				if (StateSearchIndexAsset == SearchResult.SearchIndexAsset)
				{
					// We need to check in terms of PoseIdx rather than AssetTime because
					// for blendspaces, AssetTime is not in seconds, but in the normalized range 
					// [0, 1] so comparing to `PoseJumpThresholdTime` will not make sense		
					bNearbyPose = FMath::Abs(InOutMotionMatchingState.DbPoseIdx - SearchResult.PoseIdx) * Database->Schema->SamplingInterval < Settings.PoseJumpThresholdTime;

					// Handle looping anims when checking for the pose being too close
					if (!bNearbyPose && Database->IsSourceAssetLooping(StateSearchIndexAsset))
					{
						bNearbyPose = FMath::Abs(StateSearchIndexAsset->NumPoses - InOutMotionMatchingState.DbPoseIdx - SearchResult.PoseIdx) * Database->Schema->SamplingInterval < Settings.PoseJumpThresholdTime;
					}
				}

				// Jump to candidate pose if there was a better option
				if (bBetterPose && !bNearbyPose)
				{
					InOutMotionMatchingState.JumpToPose(Context, Settings, SearchResult);
				}
			}
		}
	}

	// If we didn't search or it didn't find a pose to jump to, and we can 
	// advance but only with the follow up asset, jump to that. Otherwise we 
	// are advancing as normal, and nothing needs to be done.
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
		&& bCanAdvance
		&& bAdvanceToFollowUpAsset)
	{
		InOutMotionMatchingState.JumpToPose(Context, Settings, FollowUpAsset);
		InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToFollowUp;
	}

	// Tick elapsed pose jump timer
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose))
	{
		InOutMotionMatchingState.ElapsedPoseJumpTime += DeltaTime;
	}

	// Record debugger details
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
		TraceState.DbPoseIdx = InOutMotionMatchingState.DbPoseIdx;
		TraceState.DatabaseId = FObjectTrace::GetObjectId(Database);
		TraceState.ContinuingPoseIdx = CurrentPoseIdx;

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
	if (!CurrentDatabase.Get() || 
		!CurrentDatabase->GetSearchIndex() ||
		!CurrentDatabase->GetSearchIndex()->Assets.IsValidIndex(SearchIndexAssetIdx))
	{
		return nullptr;
	}

	return &CurrentDatabase->GetSearchIndex()->Assets[SearchIndexAssetIdx];
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

bool FMotionMatchingState::IsCompatibleDatabase(const UPoseSearchDatabase* Database) const
{
	if (Database != CurrentDatabase)
	{
		return false;
	}

#if WITH_EDITOR
	if (Database->GetSearchIndexHash() != CurrentSearchIndexHash)
	{
		return false;
	}
#endif

	return true;
}

#if WITH_EDITOR
bool FMotionMatchingState::HasSearchIndexChanged() const
{
	const bool bIsConsistent = 
		!CurrentDatabase.IsValid() ||
		CurrentDatabase->IsDerivedDataBuildPending() ||
		CurrentDatabase->GetSearchIndexHash() != CurrentSearchIndexHash;
	return bIsConsistent;
}
#endif

#undef LOCTEXT_NAMESPACE