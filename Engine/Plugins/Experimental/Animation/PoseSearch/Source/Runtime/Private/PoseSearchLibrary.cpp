// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendSpace.h"
#include "InstancedStruct.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearchFeatureChannel_Trajectory.h"
#include "Trace/PoseSearchTraceLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchLibrary)

#define LOCTEXT_NAMESPACE "PoseSearchLibrary"

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarAnimMotionMatchDrawQueryEnable(TEXT("a.MotionMatch.DrawQuery.Enable"), 0, TEXT("Enable / Disable MotionMatch Draw Query"));
TAutoConsoleVariable<int32> CVarAnimMotionMatchDrawMatchEnable(TEXT("a.MotionMatch.DrawMatch.Enable"), 0, TEXT("Enable / Disable MotionMatch Draw Match"));
TAutoConsoleVariable<int32> CVarAnimMotionMatchDrawHistoryEnable(TEXT("a.MotionMatch.DrawHistory.Enable"), 0, TEXT("Enable / Disable MotionMatch Draw History"));
#endif

//////////////////////////////////////////////////////////////////////////
// FMotionMatchingState

void FMotionMatchingState::Reset()
{
	CurrentSearchResult.Reset();
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseSearchTime = INFINITY;
	PoseIndicesHistory.Reset();
	WantedPlayRate = 1.f;
}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
	CurrentSearchResult.Update(AssetTime);
}

bool FMotionMatchingState::CanAdvance(float DeltaTime) const
{
	if (!CurrentSearchResult.IsValid())
	{
		return false;
	}

	const FPoseSearchIndexAsset* SearchIndexAsset = CurrentSearchResult.GetSearchIndexAsset(true);

	const FInstancedStruct& DatabaseAsset = CurrentSearchResult.Database->GetAnimationAssetStruct(*SearchIndexAsset);
	if (const FPoseSearchDatabaseSequence* DatabaseSequence = DatabaseAsset.GetPtr<FPoseSearchDatabaseSequence>())
	{
		const float AssetLength = DatabaseSequence->Sequence->GetPlayLength();

		float SteppedTime = CurrentSearchResult.AssetTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DatabaseSequence->Sequence->bLoop,
			DeltaTime,
			SteppedTime,
			AssetLength);

		if (AdvanceType != ETAA_Finished)
		{
			return SearchIndexAsset->SamplingInterval.Contains(SteppedTime);
		}
	}
	else if (const FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = DatabaseAsset.GetPtr<FPoseSearchDatabaseAnimComposite>())
	{
		const float AssetLength = DatabaseAnimComposite->GetAnimationAsset()->GetPlayLength();

		float SteppedTime = CurrentSearchResult.AssetTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DatabaseAnimComposite->IsLooping(),
			DeltaTime,
			SteppedTime,
			AssetLength);

		if (AdvanceType != ETAA_Finished)
		{
			return SearchIndexAsset->SamplingInterval.Contains(SteppedTime);
		}
	}
	else if (const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = DatabaseAsset.GetPtr<FPoseSearchDatabaseBlendSpace>())
	{
		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		DatabaseBlendSpace->BlendSpace->GetSamplesFromBlendInput(SearchIndexAsset->BlendParameters, BlendSamples, TriangulationIndex, true);

		float PlayLength = DatabaseBlendSpace->BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

		// Asset player time for blendspaces is normalized [0, 1] so we need to convert 
		// to a real time before we advance it
		float RealTime = CurrentSearchResult.AssetTime * PlayLength;
		float SteppedTime = RealTime;
		ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(
			DatabaseBlendSpace->BlendSpace->bLoop,
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
	// requesting inertial blending only if blendstack is disabled
	if (Settings.MaxActiveBlends <= 0)
	{
		RequestInertialBlend(Context, Settings.BlendTime);
	}

	// Remember which pose and sequence we're playing from the database
	CurrentSearchResult = Result;

	bJumpedToPose = true;
}

void FMotionMatchingState::UpdateWantedPlayRate(const UE::PoseSearch::FSearchContext& SearchContext, const FMotionMatchingSettings& Settings)
{
	if (CurrentSearchResult.IsValid())
	{
		if (!FMath::IsNearlyEqual(Settings.PlayRate.Min, 1.f, UE_KINDA_SMALL_NUMBER) || !FMath::IsNearlyEqual(Settings.PlayRate.Max, 1.f, UE_KINDA_SMALL_NUMBER))
		{
			if (const FPoseSearchFeatureVectorBuilder* PoseSearchFeatureVectorBuilder = SearchContext.GetCachedQuery(CurrentSearchResult.Database->Schema))
			{
				if (const UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = CurrentSearchResult.Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())
				{
					TConstArrayView<float> QueryData = PoseSearchFeatureVectorBuilder->GetValues();
					TConstArrayView<float> ResultData = CurrentSearchResult.Database->GetSearchIndex().GetPoseValues(CurrentSearchResult.PoseIdx);
					const float EstimatedSpeedRatio = TrajectoryChannel->GetEstimatedSpeedRatio(QueryData, ResultData);
					check(Settings.PlayRate.Min <= Settings.PlayRate.Max);
					WantedPlayRate = FMath::Clamp(EstimatedSpeedRatio, Settings.PlayRate.Min, Settings.PlayRate.Max);
				}
				else
				{
					UE_LOG(LogPoseSearch, Warning,
						TEXT("Couldn't update the WantedPlayRate in FMotionMatchingState::UpdateWantedPlayRate, because Schema '%s' couldn't find a UPoseSearchFeatureChannel_Trajectory channel"),
						*GetNameSafe(CurrentSearchResult.Database->Schema));
				}
			}
		}
	}
}

void UPoseSearchLibrary::TraceMotionMatchingState(
	const UPoseSearchSearchableAsset* Searchable,
	UE::PoseSearch::FSearchContext& SearchContext,
	const UE::PoseSearch::FSearchResult& CurrentResult,
	const UE::PoseSearch::FSearchResult& LastResult,
	float ElapsedPoseSearchTime,
	const FTransform& RootMotionTransformDelta,
	const UObject* AnimInstance,
	int32 NodeId,
	float DeltaTime,
	bool bSearch)
{
#if UE_POSE_SEARCH_TRACE_ENABLED
	using namespace UE::PoseSearch;
	
	auto AddUniqueDatabase = [](TArray<FTraceMotionMatchingStateDatabaseEntry>& DatabaseEntries, const UPoseSearchDatabase* Database, UE::PoseSearch::FSearchContext& SearchContext) -> int32
	{
		const uint64 DatabaseId = FTraceMotionMatchingState::GetIdFromObject(Database);

		int32 DbEntryIdx = INDEX_NONE;
		for (int32 i = 0; i < DatabaseEntries.Num(); ++i)
		{
			if (DatabaseEntries[i].DatabaseId == DatabaseId)
			{
				DbEntryIdx = i;
				break;
			}
		}
		if (DbEntryIdx == -1)
		{
			DbEntryIdx = DatabaseEntries.Add({ DatabaseId });

			// if throttling is on, the continuing pose can be valid, but no actual search occurred, so the query will not be cached, and we need to build it
			FPoseSearchFeatureVectorBuilder FeatureVectorBuilder;
			FeatureVectorBuilder.Init(Database->Schema);
			SearchContext.GetOrBuildQuery(Database->Schema, FeatureVectorBuilder);
			DatabaseEntries[DbEntryIdx].QueryVector = FeatureVectorBuilder.GetValues();
		}

		return DbEntryIdx;
	};

	FTraceMotionMatchingState TraceState;
	while (!SearchContext.BestCandidates.IsEmpty())
	{
		FSearchContext::FPoseCandidate PoseCandidate;
		SearchContext.BestCandidates.Pop(PoseCandidate);

		const int32 DbEntryIdx = AddUniqueDatabase(TraceState.DatabaseEntries, PoseCandidate.Database, SearchContext);
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		FTraceMotionMatchingStatePoseEntry PoseEntry;
		PoseEntry.DbPoseIdx = PoseCandidate.PoseIdx;
		PoseEntry.Cost = PoseCandidate.Cost;
		PoseEntry.PoseCandidateFlags = PoseCandidate.PoseCandidateFlags;

		DbEntry.PoseEntries.Add(PoseEntry);
	}

	if (bSearch && CurrentResult.ContinuingPoseCost.IsValid())
	{
		check(LastResult.IsValid());

		const int32 DbEntryIdx = AddUniqueDatabase(TraceState.DatabaseEntries, LastResult.Database.Get(), SearchContext);
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		const int32 LastResultPoseEntryIdx = DbEntry.PoseEntries.Add({ LastResult.PoseIdx });
		FTraceMotionMatchingStatePoseEntry& PoseEntry = DbEntry.PoseEntries[LastResultPoseEntryIdx];

		PoseEntry.Cost = CurrentResult.ContinuingPoseCost;
		PoseEntry.PoseCandidateFlags = EPoseCandidateFlags::Valid_ContinuingPose;
	}

	if (bSearch && CurrentResult.PoseCost.IsValid())
	{
		const int32 DbEntryIdx = AddUniqueDatabase(TraceState.DatabaseEntries, CurrentResult.Database.Get(), SearchContext);
		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		const int32 PoseEntryIdx = DbEntry.PoseEntries.Add({ CurrentResult.PoseIdx });
		FTraceMotionMatchingStatePoseEntry& PoseEntry = DbEntry.PoseEntries[PoseEntryIdx];

		PoseEntry.Cost = CurrentResult.PoseCost;
		PoseEntry.PoseCandidateFlags = EPoseCandidateFlags::Valid_CurrentPose;

		TraceState.CurrentDbEntryIdx = DbEntryIdx;
		TraceState.CurrentPoseEntryIdx = PoseEntryIdx;
	}

	if (DeltaTime > SMALL_NUMBER && SearchContext.GetTrajectory())
	{
		// simulation
		const FTrajectorySample PrevSample = SearchContext.GetTrajectory()->GetSampleAtTime(-DeltaTime);
		const FTrajectorySample CurrSample = SearchContext.GetTrajectory()->GetSampleAtTime(0.f);

		const FTransform SimDelta = CurrSample.Transform.GetRelativeTransform(PrevSample.Transform);

		TraceState.SimLinearVelocity = SimDelta.GetTranslation().Size() / DeltaTime;
		TraceState.SimAngularVelocity = FMath::RadiansToDegrees(SimDelta.GetRotation().GetAngle()) / DeltaTime;

		// animation
		TraceState.AnimLinearVelocity = RootMotionTransformDelta.GetTranslation().Size() / DeltaTime;
		TraceState.AnimAngularVelocity = FMath::RadiansToDegrees(RootMotionTransformDelta.GetRotation().GetAngle()) / DeltaTime;
	}

	TraceState.SearchableAssetId = FTraceMotionMatchingState::GetIdFromObject(Searchable);
	TraceState.ElapsedPoseSearchTime = ElapsedPoseSearchTime;
	TraceState.AssetPlayerTime = CurrentResult.AssetTime;
	TraceState.DeltaTime = DeltaTime;

	TraceState.Output(AnimInstance, NodeId);
#endif
}

void UPoseSearchLibrary::UpdateMotionMatchingState(
	const FAnimationUpdateContext& Context,
	const UPoseSearchSearchableAsset* Searchable,
	const FTrajectorySampleRange& Trajectory,
	const FMotionMatchingSettings& Settings,
	FMotionMatchingState& InOutMotionMatchingState,
	bool bForceInterrupt)
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

	InOutMotionMatchingState.bJumpedToPose = false;

#if UE_POSE_SEARCH_TRACE_ENABLED
	// Record Current Pose Index for Debugger
	const FSearchResult LastResult = InOutMotionMatchingState.CurrentSearchResult;
#endif

	const IPoseHistory* History = nullptr;
	if (IPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<IPoseHistoryProvider>())
	{
		History = &PoseHistoryProvider->GetPoseHistory();
	}
	
	EPoseSearchBooleanRequest QueryMirrorRequest = EPoseSearchBooleanRequest::Indifferent;
	if (const FPoseSearchIndexAsset* CurrentIndexAsset = InOutMotionMatchingState.CurrentSearchResult.GetSearchIndexAsset())
	{
		QueryMirrorRequest = CurrentIndexAsset->bMirrored ? EPoseSearchBooleanRequest::TrueValue : EPoseSearchBooleanRequest::FalseValue;
	}

	FSearchContext SearchContext(&Trajectory, History, 0.f, &InOutMotionMatchingState.PoseIndicesHistory, QueryMirrorRequest, 
		InOutMotionMatchingState.CurrentSearchResult, Settings.PoseJumpThresholdTime, bForceInterrupt, InOutMotionMatchingState.CanAdvance(DeltaTime));

	// If we can't advance or enough time has elapsed since the last pose jump then search
	const bool bSearch = !SearchContext.CanAdvance() || (InOutMotionMatchingState.ElapsedPoseSearchTime >= Settings.SearchThrottleTime);
	if (bSearch)
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime = 0.f;

		// Evaluate continuing pose
		FPoseSearchCost ContinuingPoseCost;
		FPoseSearchFeatureVectorBuilder ContinuingPoseComposedQuery;
		if (!SearchContext.IsForceInterrupt() && SearchContext.CanAdvance() && SearchContext.GetCurrentResult().Database.IsValid()
#if WITH_EDITOR
			&& FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(SearchContext.GetCurrentResult().Database.Get(), ERequestAsyncBuildFlag::ContinueRequest)
#endif
		)
		{
			const UPoseSearchDatabase* ContinuingPoseDatabase = SearchContext.GetCurrentResult().Database.Get();
			if (ensure(ContinuingPoseDatabase->Schema))
			{	
				SearchContext.GetOrBuildQuery(ContinuingPoseDatabase->Schema, ContinuingPoseComposedQuery);

				const FPoseSearchIndex& SearchIndex = ContinuingPoseDatabase->GetSearchIndex();
				const int32 PoseIdx = SearchContext.GetCurrentResult().PoseIdx;
				const int32 NumDimensions = ContinuingPoseDatabase->Schema->SchemaCardinality;
				TArrayView<float> ReconstructedPoseValuesBuffer((float*)FMemory_Alloca(NumDimensions * sizeof(float)), NumDimensions);
				const TConstArrayView<float> PoseValues = SearchIndex.Values.IsEmpty() ? SearchIndex.GetReconstructedPoseValues(PoseIdx, ReconstructedPoseValuesBuffer) : SearchIndex.GetPoseValues(PoseIdx);

				ContinuingPoseCost = ContinuingPoseDatabase->GetSearchIndex().ComparePoses(SearchContext.GetCurrentResult().PoseIdx, SearchContext.GetQueryMirrorRequest(),
					EPoseComparisonFlags::ContinuingPose, ContinuingPoseDatabase->Schema->MirrorMismatchCostBias, PoseValues, ContinuingPoseComposedQuery.GetValues());
				SearchContext.UpdateCurrentBestCost(ContinuingPoseCost);
			}
		}

		// Search the database for the nearest match to the updated query vector
		FSearchResult SearchResult = Searchable->Search(SearchContext);
		if (SearchResult.PoseCost.IsValid())
		{
			SearchContext.UpdateCurrentBestCost(SearchResult.PoseCost);
		}

		if (SearchResult.PoseCost.GetTotalCost() < ContinuingPoseCost.GetTotalCost())
		{
			SearchResult.ContinuingPoseCost = ContinuingPoseCost;
			InOutMotionMatchingState.JumpToPose(Context, Settings, SearchResult);
		}
		else
		{
			// copying few properties of SearchResult into CurrentSearchResult to facilitate debug drawing
#if WITH_EDITOR
			InOutMotionMatchingState.CurrentSearchResult.BruteForcePoseCost = ContinuingPoseCost;
#endif
			InOutMotionMatchingState.CurrentSearchResult.PoseCost = ContinuingPoseCost;
			InOutMotionMatchingState.CurrentSearchResult.ContinuingPoseCost = ContinuingPoseCost;
			InOutMotionMatchingState.CurrentSearchResult.ComposedQuery = ContinuingPoseComposedQuery;
		}

		InOutMotionMatchingState.UpdateWantedPlayRate(SearchContext, Settings);
	}
	else
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime += DeltaTime;
	}

	InOutMotionMatchingState.PoseIndicesHistory.Update(InOutMotionMatchingState.CurrentSearchResult, DeltaTime, Settings.PoseReselectHistory);

#if UE_POSE_SEARCH_TRACE_ENABLED
	// Record debugger details
	if (IsTracing(Context))
	{
		TraceMotionMatchingState(Searchable, SearchContext, InOutMotionMatchingState.CurrentSearchResult, LastResult, InOutMotionMatchingState.ElapsedPoseSearchTime,
			InOutMotionMatchingState.RootMotionTransformDelta, Context.AnimInstanceProxy->GetAnimInstanceObject(), Context.GetCurrentNodeId(), DeltaTime, bSearch);
	}
#endif
}

void UPoseSearchLibrary::MotionMatch(
	UAnimInstance* AnimInstance,
	const UPoseSearchSearchableAsset* Searchable,
	const FTrajectorySampleRange Trajectory,
	const FName PoseHistoryName,
	UAnimationAsset*& SelectedAnimation,
	float& SelectedTime,
	bool& bLoop,
	bool& bIsMirrored,
	FVector& BlendParameters,
	const UAnimationAsset* FutureAnimation,
	float FutureAnimationStartTime,
	float TimeToFutureAnimationStart,
	const int DebugSessionUniqueIdentifier)
{
#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	class UAnimInstanceProxyProvider : public UAnimInstance
	{
	public:
		static FAnimInstanceProxy& GetAnimInstanceProxy(UAnimInstance* AnimInstance)
		{
			check(AnimInstance);
			return static_cast<UAnimInstanceProxyProvider*>(AnimInstance)->GetProxyOnAnyThread<FAnimInstanceProxy>();
		}
	};
#endif //ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG

	using namespace UE::Anim;
	using namespace UE::PoseSearch;

	static constexpr float FiniteDelta = 1 / 60.0f;

	SelectedAnimation = nullptr;
	SelectedTime = 0.f;
	bLoop = false;
	bIsMirrored = false;
	BlendParameters = FVector::ZeroVector;

	if (FutureAnimationStartTime < FiniteDelta)
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided FutureAnimationStartTime (%f) is too small to be able to calculate velocities. Clamping it to minimum value of %f"), TimeToFutureAnimationStart, FiniteDelta);
		FutureAnimationStartTime = FiniteDelta;
	}

	const float MinTimeToFutureAnimationStart = FiniteDelta + UE_KINDA_SMALL_NUMBER;
	if (TimeToFutureAnimationStart < MinTimeToFutureAnimationStart)
	{
		UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided TimeToFutureAnimationStart (%f) is too small. Clamping it to minimum value of %f"), TimeToFutureAnimationStart, MinTimeToFutureAnimationStart);
		TimeToFutureAnimationStart = MinTimeToFutureAnimationStart;
	}

	if (Searchable)
	{
		// ExtendedPoseHistory will hold future poses to match AssetSamplerBase (at FutureAnimationStartTime) TimeToFutureAnimationStart seconds in the future
		FExtendedPoseHistory ExtendedPoseHistory;
		if (AnimInstance)
		{
			if (IAnimClassInterface* AnimBlueprintClass = IAnimClassInterface::GetFromClass(AnimInstance->GetClass()))
			{
				if (const FAnimSubsystem_Tag* TagSubsystem = AnimBlueprintClass->FindSubsystem<FAnimSubsystem_Tag>())
				{
					if (const FAnimNode_PoseSearchHistoryCollector_Base* PoseHistoryNode = TagSubsystem->FindNodeByTag<FAnimNode_PoseSearchHistoryCollector_Base>(PoseHistoryName, AnimInstance))
					{
						ExtendedPoseHistory.Init(&PoseHistoryNode->GetPoseHistory());
					}
				}
			}

			if (!ExtendedPoseHistory.IsInitialized())
			{
				if (FutureAnimation)
				{
					UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - Couldn't find pose history with name '%s'. FutureAnimation search will not be performed"), *PoseHistoryName.ToString());
				}
				else
				{
					UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - Couldn't find pose history with name '%s'"), *PoseHistoryName.ToString());
				}
			}
			else if (FutureAnimation)
			{
				const FBoneContainer& BoneContainer = AnimInstance->GetRequiredBonesOnAnyThread();

				FBlendSpaceSampler BlendSpaceSampler;
				FSequenceBaseSampler SequenceBaseSampler;
				FAnimMontageSampler AnimMontageSampler;
				const FAssetSamplerBase* AssetSamplerBase = nullptr;
				if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(FutureAnimation))
				{
					FBlendSpaceSampler::FInput Input;
					Input.BoneContainer = BoneContainer;
					Input.BlendSpace = BlendSpace;
					// @todo... add BlendParameters
					//Input.BlendParameters = BlendParameters;
					BlendSpaceSampler.Init(Input);
					AssetSamplerBase = &BlendSpaceSampler;
				}
				else if (const UAnimMontage* AnimMontage = Cast<UAnimMontage>(FutureAnimation))
				{
					FAnimMontageSampler::FInput Input;
					Input.AnimMontage = AnimMontage;
					AnimMontageSampler.Init(Input);
					AssetSamplerBase = &AnimMontageSampler;
				}
				else if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(FutureAnimation))
				{
					FSequenceBaseSampler::FInput Input;
					Input.SequenceBase = SequenceBase;
					SequenceBaseSampler.Init(Input);
					AssetSamplerBase = &SequenceBaseSampler;
				}
				else
				{
					UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - unsupported FutureAnimation '%s'"), *GetNameSafe(FutureAnimation));
				}

				if (AssetSamplerBase)
				{
					FCompactPose Pose;
					FBlendedCurve UnusedCurve;
					FStackAttributeContainer UnusedAtrribute;
					FAnimationPoseData AnimPoseData = { Pose, UnusedCurve, UnusedAtrribute };

					UnusedCurve.InitFrom(BoneContainer);
					Pose.SetBoneContainer(&BoneContainer);

					// extracting 2 poses to be able to calculate velocities
					for (int i = 0; i < 2; ++i)
					{
						const float ExtractionTime = FutureAnimationStartTime + (i - 1) * FiniteDelta;
						const float FutureAnimationTime = TimeToFutureAnimationStart + (i - 1) * FiniteDelta;

						FDeltaTimeRecord DeltaTimeRecord;
						DeltaTimeRecord.Set(ExtractionTime - FiniteDelta, FiniteDelta);
						FAnimExtractContext ExtractionCtx(double(ExtractionTime), false, DeltaTimeRecord, false);

						AssetSamplerBase->ExtractPose(ExtractionCtx, AnimPoseData);

						FCSPose<FCompactPose> ComponentSpacePose;
						ComponentSpacePose.InitPose(Pose);

						const FTrajectorySample TrajectorySample = Trajectory.GetSampleAtTime(ExtractionTime);
						const FTransform& ComponentTransform = AnimInstance->GetOwningComponent()->GetComponentTransform();
						const FTransform FutureComponentTransform = TrajectorySample.Transform * ComponentTransform;

						ExtendedPoseHistory.AddFuturePose(FutureAnimationTime, ComponentSpacePose, FutureComponentTransform);
					}

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
					if (CVarAnimMotionMatchDrawHistoryEnable.GetValueOnAnyThread())
					{
						ExtendedPoseHistory.DebugDraw(UAnimInstanceProxyProvider::GetAnimInstanceProxy(AnimInstance));
					}
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
				}
			}
		}

		// @todo: finish set up SearchContext by exposing or calculating additional members
		FSearchContext SearchContext(&Trajectory, ExtendedPoseHistory.IsInitialized() ? &ExtendedPoseHistory : nullptr, TimeToFutureAnimationStart);

		FSearchResult SearchResult = Searchable->Search(SearchContext);
		if (SearchResult.IsValid())
		{
			const FPoseSearchIndexAsset* SearchIndexAsset = SearchResult.GetSearchIndexAsset();
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = SearchResult.Database->GetAnimationAssetBase(*SearchIndexAsset))
			{
				SelectedAnimation = DatabaseAsset->GetAnimationAsset();
				SelectedTime = SearchResult.AssetTime;
				bLoop = DatabaseAsset->IsLooping();
				bIsMirrored = SearchIndexAsset->bMirrored;
				BlendParameters = SearchIndexAsset->BlendParameters;
			}
		}

		if (AnimInstance)
		{
#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
			if (SearchResult.IsValid())
			{
				if (CVarAnimMotionMatchDrawMatchEnable.GetValueOnAnyThread())
				{
					UE::PoseSearch::FDebugDrawParams DrawParams(&UAnimInstanceProxyProvider::GetAnimInstanceProxy(AnimInstance), SearchResult.Database.Get());
					DrawParams.DrawFeatureVector(SearchResult.PoseIdx);
				}

				if (CVarAnimMotionMatchDrawQueryEnable.GetValueOnAnyThread())
				{
					UE::PoseSearch::FDebugDrawParams DrawParams(&UAnimInstanceProxyProvider::GetAnimInstanceProxy(AnimInstance), SearchResult.Database.Get(), EDebugDrawFlags::DrawQuery);
					DrawParams.DrawFeatureVector(SearchResult.ComposedQuery.GetValues());
				}
			}
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG

#if UE_POSE_SEARCH_TRACE_ENABLED
			TraceMotionMatchingState(Searchable, SearchContext, SearchResult, FSearchResult(), 0.f, FTransform::Identity, AnimInstance, DebugSessionUniqueIdentifier, AnimInstance->GetDeltaSeconds(), true);
#endif // UE_POSE_SEARCH_TRACE_ENABLED
		}
	}
}

#undef LOCTEXT_NAMESPACE
