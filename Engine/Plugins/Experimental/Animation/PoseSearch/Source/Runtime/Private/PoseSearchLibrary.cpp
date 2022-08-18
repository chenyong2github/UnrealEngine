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

namespace UE::PoseSearch
{
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
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseJumpTime = INFINITY;
	PoseIndicesHistory.Reset();
}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
	CurrentSearchResult.Update(AssetTime);
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

		float SteppedTime = CurrentSearchResult.AssetTime;
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
				const float FollowUpAssetTime = CurrentSearchResult.AssetTime - AssetLength;

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
		float RealTime = CurrentSearchResult.AssetTime * PlayLength;
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

	const float JumpBlendTime = ComputeJumpBlendTime(Result, Settings);
	RequestInertialBlend(Context, JumpBlendTime);
	Flags |= EMotionMatchingFlags::JumpedToPose;
}

static void TraceMotionMatchingState(
	const FAnimationUpdateContext& UpdateContext,
	const UPoseSearchSearchableAsset* Searchable,
	UE::PoseSearch::FSearchContext& SearchContext,
	const FMotionMatchingState& MotionMatchingState,
	const FGameplayTagQuery* DatabaseTagQuery,
	const FTrajectorySampleRange& Trajectory,
	const UE::PoseSearch::FSearchResult& LastResult
)
{
#if UE_POSE_SEARCH_TRACE_ENABLED
	using namespace UE::PoseSearch;

	if (!MotionMatchingState.CurrentSearchResult.IsValid())
	{
		return;
	}

	const float DeltaTime = UpdateContext.GetDeltaTime();

	FTraceMotionMatchingState TraceState;
	bool bContinuingPoseTraced = false;
	bool bCurrentPoseTraced = false;
	while (!SearchContext.BestCandidates.IsEmpty())
	{
		FSearchContext::FPoseCandidate PoseCandidate;
		SearchContext.BestCandidates.Pop(PoseCandidate);

		const uint64 DatabaseId = FTraceMotionMatchingState::GetIdFromObject(PoseCandidate.Database);
		const int32 DbEntryIdx = TraceState.DatabaseEntries.AddUnique({DatabaseId});
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		FTraceMotionMatchingStatePoseEntry PoseEntry;
		PoseEntry.DbPoseIdx = PoseCandidate.PoseIdx;
		PoseEntry.Cost = PoseCandidate.Cost;

		if (PoseCandidate.Database == MotionMatchingState.CurrentSearchResult.Database)
		{
			DbEntry.Flags |= FTraceMotionMatchingStateDatabaseEntry::EFlags::CurrentDatabase;
		}
		
		if (PoseCandidate == MotionMatchingState.CurrentSearchResult)
		{
			PoseEntry.Flags |= FTraceMotionMatchingStatePoseEntry::EFlags::CurrentPose;
			bCurrentPoseTraced = true;
		}

		if (PoseCandidate == LastResult)
		{
			PoseEntry.Flags |= FTraceMotionMatchingStatePoseEntry::EFlags::ContinuingPose;
			bContinuingPoseTraced = true;
		}

		DbEntry.PoseEntries.Add(PoseEntry);
	}

	if (!bContinuingPoseTraced && LastResult.IsValid())
	{
		const uint64 LastResultDbId = FTraceMotionMatchingState::GetIdFromObject(LastResult.Database.Get());

		int32 LastResultDbIdx = TraceState.DatabaseEntries.AddUnique({LastResultDbId});
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[LastResultDbIdx];

		int32 LastResultPoseEntryIdx = DbEntry.PoseEntries.AddUnique({LastResult.PoseIdx});
		FTraceMotionMatchingStatePoseEntry& PoseEntry = DbEntry.PoseEntries[LastResultPoseEntryIdx];

		PoseEntry.Cost = LastResult.PoseCost.GetTotalCost();
		PoseEntry.Flags |= FTraceMotionMatchingStatePoseEntry::EFlags::ContinuingPose;
		bContinuingPoseTraced = true;
	}

	if (!bCurrentPoseTraced && MotionMatchingState.CurrentSearchResult.IsValid())
	{
		const uint64 CurrentResultDbId = FTraceMotionMatchingState::GetIdFromObject(MotionMatchingState.CurrentSearchResult.Database.Get());

		int32 DbEntryIdx = TraceState.DatabaseEntries.AddUnique({CurrentResultDbId});
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];
		DbEntry.Flags |= FTraceMotionMatchingStateDatabaseEntry::EFlags::CurrentDatabase;

		int32 PoseEntryIdx = DbEntry.PoseEntries.AddUnique({MotionMatchingState.CurrentSearchResult.PoseIdx});
		FTraceMotionMatchingStatePoseEntry& PoseEntry = DbEntry.PoseEntries[PoseEntryIdx];

		PoseEntry.Cost = MotionMatchingState.CurrentSearchResult.PoseCost.GetTotalCost();
		PoseEntry.Flags |= FTraceMotionMatchingStatePoseEntry::EFlags::CurrentPose;
		bCurrentPoseTraced = true;
	}

	TraceState.CurrentDbEntryIdx = TraceState.DatabaseEntries.IndexOfByPredicate(
		[](const auto& DbEntry)
		{
			return EnumHasAnyFlags(DbEntry.Flags, FTraceMotionMatchingStateDatabaseEntry::EFlags::CurrentDatabase);
		});

	if (TraceState.CurrentDbEntryIdx != INDEX_NONE)
	{
		TraceState.CurrentPoseEntryIdx = TraceState.DatabaseEntries[TraceState.CurrentDbEntryIdx].PoseEntries.IndexOfByPredicate(
			[](const auto& PoseEntry)
			{
				return EnumHasAnyFlags(PoseEntry.Flags, FTraceMotionMatchingStatePoseEntry::EFlags::CurrentPose);
			});
	}


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

		const FTransform AnimDelta = MotionMatchingState.RootMotionTransformDelta;

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
		MotionMatchingState.CurrentSearchResult.Database.Get(),
		DatabaseTagQuery, 
		DatabaseSequenceFilter);

	TArray<bool> DatabaseBlendSpaceFilter;
	ComputeDatabaseBlendSpaceFilter(
		MotionMatchingState.CurrentSearchResult.Database.Get(),
		DatabaseTagQuery, 
		DatabaseBlendSpaceFilter);

	if (EnumHasAnyFlags(MotionMatchingState.Flags, EMotionMatchingFlags::JumpedToFollowUp))
	{
		TraceState.Flags |= FTraceMotionMatchingState::EFlags::FollowupAnimation;
	}

	TraceState.SearchableAssetId = FTraceMotionMatchingState::GetIdFromObject(Searchable);
	TraceState.ElapsedPoseJumpTime = MotionMatchingState.ElapsedPoseJumpTime;
	TraceState.QueryVector = MotionMatchingState.CurrentSearchResult.ComposedQuery.GetValues();
	TraceState.QueryVectorNormalized = MotionMatchingState.CurrentSearchResult.ComposedQuery.GetNormalizedValues();
	TraceState.AssetPlayerTime = MotionMatchingState.CurrentSearchResult.AssetTime;
	TraceState.DeltaTime = DeltaTime;
	TraceState.SimLinearVelocity = SimLinearVelocity;
	TraceState.SimAngularVelocity = SimAngularVelocity;
	TraceState.AnimLinearVelocity = AnimLinearVelocity;
	TraceState.AnimAngularVelocity = AnimAngularVelocity;
	TraceState.DatabaseSequenceFilter = DatabaseSequenceFilter;
	TraceState.DatabaseBlendSpaceFilter = DatabaseBlendSpaceFilter;

	TraceState.Output(UpdateContext);
#endif
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
	FSearchResult FollowUpAssetResult;
	const bool bCanAdvance = InOutMotionMatchingState.CanAdvance(DeltaTime, bAdvanceToFollowUpAsset, FollowUpAssetResult);

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
		SearchContext.CurrentResult = InOutMotionMatchingState.CurrentSearchResult;
		SearchContext.PoseJumpThresholdTime = Settings.PoseJumpThresholdTime;
		SearchContext.PoseIndicesHistory = &InOutMotionMatchingState.PoseIndicesHistory;

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

		// making sure we haven't calculated ContinuingPoseCost if we !bCanAdvance 
		check(bCanAdvance || !SearchResult.ContinuingPoseCost.IsValid());
		
		if (SearchResult.PoseCost.GetTotalCost() < SearchResult.ContinuingPoseCost.GetTotalCost())
		{
			InOutMotionMatchingState.JumpToPose(Context, Settings, SearchResult);
		}
		else
		{
			// copying few properties of SearchResult into CurrentSearchResult to facilitate debug drawing
#if WITH_EDITOR
			InOutMotionMatchingState.CurrentSearchResult.BruteForcePoseCost = SearchResult.BruteForcePoseCost;
#endif
			InOutMotionMatchingState.CurrentSearchResult.PoseCost = SearchResult.PoseCost;
			InOutMotionMatchingState.CurrentSearchResult.ContinuingPoseCost = SearchResult.ContinuingPoseCost;
			InOutMotionMatchingState.CurrentSearchResult.ComposedQuery = SearchResult.ComposedQuery;
		}
	}

	// If we didn't search or it didn't find a pose to jump to, and we can 
	// advance but only with the follow up asset, jump to that. Otherwise we 
	// are advancing as normal, and nothing needs to be done.
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose)
		&& bCanAdvance
		&& bAdvanceToFollowUpAsset)
	{
		InOutMotionMatchingState.JumpToPose(Context, Settings, FollowUpAssetResult);
		InOutMotionMatchingState.Flags |= EMotionMatchingFlags::JumpedToFollowUp;
	}

	// Tick elapsed pose jump timer
	if (!(InOutMotionMatchingState.Flags & EMotionMatchingFlags::JumpedToPose))
	{
		InOutMotionMatchingState.ElapsedPoseJumpTime += DeltaTime;
	}

	InOutMotionMatchingState.PoseIndicesHistory.Update(InOutMotionMatchingState.CurrentSearchResult, DeltaTime, Settings.PoseReselectHistory);
	
	// Record debugger details
	TraceMotionMatchingState(
		Context,
		Searchable,
		SearchContext,
		InOutMotionMatchingState,
		DatabaseTagQuery,
		Trajectory,
		LastResult
	);
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