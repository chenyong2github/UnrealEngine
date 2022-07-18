// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Trace/PoseSearchTraceLogger.h"
#include "MotionTrajectoryLibrary.h"

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

void FMotionMatchingState::Reset()
{
	CurrentSearchResult.Reset();
	AssetPlayerTime = 0.0f;
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseJumpTime = INFINITY;
}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
	CurrentSearchResult.Update(AssetTime);
	AssetPlayerTime = CurrentSearchResult.AssetTime;
}

bool FMotionMatchingState::CanAdvance(float DeltaTime, bool& bOutAdvanceToFollowUpAsset, UE::PoseSearch::FSearchResult& OutFollowUpAsset) const
{
	bOutAdvanceToFollowUpAsset = false;
	OutFollowUpAsset = UE::PoseSearch::FSearchResult();

	if (!CurrentSearchResult.IsValid())
	{
		return false;
	}

	const FPoseSearchIndexAsset* SearchIndexAsset = GetCurrentSearchIndexAsset();

	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		const FPoseSearchDatabaseSequence& DbSequence = 
			CurrentSearchResult.Database->GetSequenceSourceAsset(SearchIndexAsset);
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
			int32 FollowUpDbSequenceIdx = CurrentSearchResult.Database->Sequences.IndexOfByPredicate(
				[&](const FPoseSearchDatabaseSequence& Entry)
				{
					return Entry.Sequence == DbSequence.FollowUpSequence;
				});

			int32 FollowUpSearchIndexAssetIdx = CurrentSearchResult.Database->GetSearchIndex()->Assets.IndexOfByPredicate(
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
					&CurrentSearchResult.Database->GetSearchIndex()->Assets[FollowUpSearchIndexAssetIdx];

				// Follow up asset time will start slightly before the beginning of the sequence as 
				// this is essentially what the matching time in the corresponding main sequence is.
				// Here we are assuming that the tick will advance the asset player timer into the 
				// valid region
				const float FollowUpAssetTime = AssetPlayerTime - AssetLength;

				// There is no correspoding pose index when we switch due to what is mentioned above
				// so for now we just take whatever pose index is associated with the first frame.
				OutFollowUpAsset.PoseIdx = CurrentSearchResult.Database->GetPoseIndexFromTime(FollowUpSearchIndexAsset->SamplingInterval.Min, FollowUpSearchIndexAsset);
				OutFollowUpAsset.SearchIndexAsset = FollowUpSearchIndexAsset;
				OutFollowUpAsset.AssetTime = FollowUpAssetTime;
				return true;
			}
		}
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		const FPoseSearchDatabaseBlendSpace& DbBlendSpace = 
			CurrentSearchResult.Database->GetBlendSpaceSourceAsset(SearchIndexAsset);

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
	CurrentSearchResult = Result;

	ElapsedPoseJumpTime = 0.0f;
	AssetPlayerTime = Result.AssetTime;

	const float JumpBlendTime = ComputeJumpBlendTime(Result, Settings);
	RequestInertialBlend(Context, JumpBlendTime);
	Flags |= EMotionMatchingFlags::JumpedToPose;
}

void UpdateMotionMatchingState(
	const FAnimationUpdateContext& Context,
	const UPoseSearchSearchableAsset* Searchable,
	const FGameplayTagQuery* DatabaseTagQuery,
	const FGameplayTagContainer* ActiveTagsContainer,
	const FTrajectorySampleRange& Trajectory,
	const FMotionMatchingSettings& Settings,
	FMotionMatchingState& InOutMotionMatchingState,
	bool bForceInterrupt
)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_Update);

	using namespace UE::PoseSearch;

	if (!Searchable)
	{
		Context.LogMessage(
			EMessageSeverity::Error, 
			LOCTEXT("NoSearchable", "No searchable asset provided for motion matching."));
		return;
	}

	const float DeltaTime = Context.GetDeltaTime();

	// Reset State Flags
	InOutMotionMatchingState.Flags = EMotionMatchingFlags::None;

	// Record Current Pose Index for Debugger
	const FSearchResult LastResult = InOutMotionMatchingState.CurrentSearchResult;

	// Check if we can advance. Includes the case where we can advance but only by switching to a follow up asset.
	bool bAdvanceToFollowUpAsset = false;
	FSearchResult FollowUpAsset;
	const bool bCanAdvance = InOutMotionMatchingState.CanAdvance(DeltaTime, bAdvanceToFollowUpAsset, FollowUpAsset);

	// If we can't advance or enough time has elapsed since the last pose jump then search
	FSearchContext SearchContext;
	if (!bCanAdvance || (InOutMotionMatchingState.ElapsedPoseJumpTime >= Settings.SearchThrottleTime))
	{
		// Build the search context
		SearchContext.DatabaseTagQuery = DatabaseTagQuery;
		SearchContext.ActiveTagsContainer = ActiveTagsContainer;
		SearchContext.Trajectory = &Trajectory;
		SearchContext.OwningComponent = Context.AnimInstanceProxy->GetSkelMeshComponent();
		SearchContext.BoneContainer = &Context.AnimInstanceProxy->GetRequiredBones();
		SearchContext.bForceInterrupt = bForceInterrupt;
		SearchContext.bCanAdvance = bCanAdvance;

#if WITH_EDITORONLY_DATA
		SearchContext.DebugDrawParams.SearchCostHistoryBruteForce = &InOutMotionMatchingState.SearchCostHistoryBruteForce;
		SearchContext.DebugDrawParams.SearchCostHistoryKDTree = &InOutMotionMatchingState.SearchCostHistoryKDTree;
#endif

		SearchContext.CurrentResult = InOutMotionMatchingState.CurrentSearchResult;
		SearchContext.PoseJumpThresholdTime = Settings.PoseJumpThresholdTime;

		IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<IPoseHistoryProvider>();
		if (PoseHistoryProvider)
		{
			SearchContext.History = &PoseHistoryProvider->GetPoseHistory();
		}

		if (const FPoseSearchIndexAsset* CurrentIndexAsset = InOutMotionMatchingState.GetCurrentSearchIndexAsset())
		{
			SearchContext.QueryMirrorRequest =
				CurrentIndexAsset->bMirrored ?
				EPoseSearchBooleanRequest::TrueValue :
				EPoseSearchBooleanRequest::FalseValue;
		}

		// Search the database for the nearest match to the updated query vector
		FSearchResult SearchResult = Searchable->Search(SearchContext);
		const float ContinuingPoseCost = bCanAdvance && SearchResult.ContinuingPoseCost.IsValid() ? SearchResult.ContinuingPoseCost.GetTotalCost() : MAX_flt;
		const float PoseCost = SearchResult.PoseCost.IsValid()  ? SearchResult.PoseCost.GetTotalCost() : MAX_flt;

		if (PoseCost < ContinuingPoseCost)
		{
			InOutMotionMatchingState.JumpToPose(Context, Settings, SearchResult);
		}

		// todo: cache the queries into TraceState, one per TraceState.DatabaseEntries or
		// InOutMotionMatchingState.CurrentSearchResult.ComposedQuery = SearchResult.ComposedQuery;
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
	FTraceMotionMatchingState TraceState;
	TArray<const UPoseSearchDatabase*> Databases;
	while (!SearchContext.BestCandidates.IsEmpty())
	{
		FSearchContext::FPoseCandidate PoseCandidate;
		SearchContext.BestCandidates.Pop(PoseCandidate);

		int32 DatabaseEntryIndex = 0;
		if (!Databases.Find(PoseCandidate.Database, DatabaseEntryIndex))
		{
			Databases.Push(PoseCandidate.Database);
			DatabaseEntryIndex = Databases.Num() - 1;

			TraceState.DatabaseEntries.AddDefaulted();
			TraceState.DatabaseEntries[DatabaseEntryIndex].DatabaseId = FTraceMotionMatchingState::GetIdFromDatabase(PoseCandidate.Database);
		}

		FTraceMotionMatchingStatePoseEntry PoseEntry;
		PoseEntry.DbPoseIdx = PoseCandidate.PoseIdx;
		PoseEntry.Cost = PoseCandidate.Cost;
		TraceState.DatabaseEntries[DatabaseEntryIndex].PoseEntries.Add(PoseEntry);
	}

	if (InOutMotionMatchingState.CurrentSearchResult.IsValid())
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
		ComputeDatabaseSequenceFilter(
			InOutMotionMatchingState.CurrentSearchResult.Database.Get(), 
			DatabaseTagQuery, 
			DatabaseSequenceFilter);

		TArray<bool> DatabaseBlendSpaceFilter;
		ComputeDatabaseBlendSpaceFilter(
			InOutMotionMatchingState.CurrentSearchResult.Database.Get(),
			DatabaseTagQuery, 
			DatabaseBlendSpaceFilter);

		if (EnumHasAnyFlags(InOutMotionMatchingState.Flags, EMotionMatchingFlags::JumpedToFollowUp))
		{
			TraceState.Flags |= FTraceMotionMatchingState::EFlags::FollowupAnimation;
		}

		TraceState.ElapsedPoseJumpTime = InOutMotionMatchingState.ElapsedPoseJumpTime;
		// @TODO: Change this to only be the previous query, not persistently updated (i.e. if throttled)?
		TraceState.QueryVector = InOutMotionMatchingState.CurrentSearchResult.ComposedQuery.GetValues();
		TraceState.QueryVectorNormalized = InOutMotionMatchingState.CurrentSearchResult.ComposedQuery.GetNormalizedValues();
		TraceState.DbPoseIdx = InOutMotionMatchingState.CurrentSearchResult.PoseIdx;
		TraceState.SetDatabase(InOutMotionMatchingState.CurrentSearchResult.Database.Get());
		TraceState.ContinuingPoseIdx = LastResult.PoseIdx;

		TraceState.AssetPlayerTime = InOutMotionMatchingState.AssetPlayerTime;
		TraceState.DeltaTime = DeltaTime;
		TraceState.SimLinearVelocity = SimLinearVelocity;
		TraceState.SimAngularVelocity = SimAngularVelocity;
		TraceState.AnimLinearVelocity = AnimLinearVelocity;
		TraceState.AnimAngularVelocity = AnimAngularVelocity;
		TraceState.DatabaseSequenceFilter = DatabaseSequenceFilter;
		TraceState.DatabaseBlendSpaceFilter = DatabaseBlendSpaceFilter;

		TraceState.Output(Context);
	}
#endif
}

const FPoseSearchIndexAsset* FMotionMatchingState::GetCurrentSearchIndexAsset() const
{
	if (CurrentSearchResult.IsValid())
	{
		return CurrentSearchResult.SearchIndexAsset;
	}

	return nullptr;
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