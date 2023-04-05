// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatching.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Trace/PoseSearchTraceLogger.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_MotionMatching)

#define LOCTEXT_NAMESPACE "AnimNode_MotionMatching"

#if ENABLE_ANIM_DEBUG
static TAutoConsoleVariable<int32> CVarAnimNodeMotionMatchingDrawQuery(TEXT("a.AnimNode.MotionMatching.DebugDrawQuery"), 0, TEXT("Draw input query"));
static TAutoConsoleVariable<int32> CVarAnimNodeMotionMatchingDrawCurResult(TEXT("a.AnimNode.MotionMatching.DebugDrawCurResult"), 0, TEXT("Draw current result"));
#endif

/////////////////////////////////////////////////////
// FAnimNode_MotionMatching

void FAnimNode_MotionMatching::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	GetEvaluateGraphExposedInputs().Execute(Context);

	BlendStackNode.Initialize_AnyThread(Context);

	Source.SetLinkNode(&BlendStackNode);
	Source.Initialize(Context);
}

void FAnimNode_MotionMatching::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Source.Evaluate(Output);

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

	using namespace UE::PoseSearch;

	GetEvaluateGraphExposedInputs().Execute(Context);

	const bool bNeedsReset =
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
#if WITH_EDITOR
		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(MotionMatchingState.CurrentSearchResult.Database.Get(), ERequestAsyncBuildFlag::ContinueRequest))
		{
			const UPoseSearchDatabase* CurrentResultDatabase = MotionMatchingState.CurrentSearchResult.Database.Get();
			const FPoseSearchIndex& SearchIndex = CurrentResultDatabase->GetSearchIndex();
			if (!SearchIndex.IsValidPoseIndex(MotionMatchingState.CurrentSearchResult.PrevPoseIdx) ||
				!SearchIndex.IsValidPoseIndex(MotionMatchingState.CurrentSearchResult.NextPoseIdx) ||
				CurrentResultDatabase->Schema != MotionMatchingState.CurrentSearchResult.ComposedQuery.GetSchema())
			{
				// MotionMatchingState is out of sync with CurrentResultDatabase: we need to reset the MM state. This could happen if PIE is paused, and we edit the database,
				// so FAnimNode_MotionMatching::UpdateAssetPlayer is never called and FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex never returns false here
				MotionMatchingState.Reset();
			}
		}
		else
		{
			// we're still indexing MotionMatchingState.CurrentSearchResult.Database, so we Reset the MotionMatchingState
			MotionMatchingState.Reset();
		}
#endif // WITH_EDITOR

		// We adjust the motion matching state asset time to the current player node's asset time. This is done 
		// because the player node may have ticked more or less time than we expected due to variable dt or the 
		// dynamic playback rate adjustment and as such the motion matching state does not update by itself
		MotionMatchingState.AdjustAssetTime(BlendStackNode.GetAccumulatedTime());
	}
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());

	// If the Database property hasn't been overridden, set it as the only database to search.
	if (!bOverrideDatabaseInput && Database)
	{
		DatabasesToSearch.Reset(1);
		DatabasesToSearch.Add(Database);
	}

	// Execute core motion matching algorithm
	UPoseSearchLibrary::UpdateMotionMatchingState(
		Context,
		DatabasesToSearch,
		Trajectory,
		Settings,
		MotionMatchingState,
		bForceInterrupt | bForceInterruptNextUpdate
	);

	// If a new pose is requested, blend into the new asset via BlendStackNode
	if (MotionMatchingState.bJumpedToPose)
	{
		const FPoseSearchIndexAsset* SearchIndexAsset = MotionMatchingState.CurrentSearchResult.GetSearchIndexAsset();
		const UPoseSearchDatabase* CurrentResultDatabase = MotionMatchingState.CurrentSearchResult.Database.Get();
		if (SearchIndexAsset && CurrentResultDatabase && CurrentResultDatabase->Schema)
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = CurrentResultDatabase->GetAnimationAssetBase(*SearchIndexAsset))
			{
				BlendStackNode.BlendTo(DatabaseAsset->GetAnimationAsset(), MotionMatchingState.CurrentSearchResult.AssetTime,
					DatabaseAsset->IsLooping(), SearchIndexAsset->bMirrored, CurrentResultDatabase->Schema->MirrorDataTable.Get(),
					Settings.MaxActiveBlends, Settings.BlendTime, Settings.BlendProfile, Settings.BlendOption, SearchIndexAsset->BlendParameters, MotionMatchingState.WantedPlayRate);
			}
		}
	}
	BlendStackNode.UpdatePlayRate(MotionMatchingState.WantedPlayRate);

	Source.Update(Context);


#if WITH_EDITORONLY_DATA
	const bool bDebugDrawQuery = CVarAnimNodeMotionMatchingDrawQuery.GetValueOnAnyThread() > 0;
	const bool bDebugDrawCurResult = CVarAnimNodeMotionMatchingDrawCurResult.GetValueOnAnyThread() > 0;
	if (bDebugDrawQuery || bDebugDrawCurResult)
	{
		const UE::PoseSearch::FSearchResult& CurResult = MotionMatchingState.CurrentSearchResult;

#if WITH_EDITOR
		// in case we're still indexing MotionMatchingState.CurrentSearchResult.Database we Reset the MotionMatchingState
		if (!FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(CurResult.Database.Get(), ERequestAsyncBuildFlag::ContinueRequest))
		{
		}
		else
#endif // WITH_EDITOR
		{
			if (bDebugDrawCurResult)
			{
				UE::PoseSearch::FDebugDrawParams DrawParams(Context.AnimInstanceProxy, CurResult.Database.Get());
				DrawParams.DrawFeatureVector(CurResult.PoseIdx);
			}

			if (bDebugDrawQuery)
			{
				UE::PoseSearch::FDebugDrawParams DrawParams(Context.AnimInstanceProxy, CurResult.Database.Get(), EDebugDrawFlags::DrawQuery);
				DrawParams.DrawFeatureVector(CurResult.ComposedQuery.GetValues());
			}

			// @todo: add ContinuingPoseCost / BruteForcePoseCost / PoseCost graphs to rewind debugger 
			//if (DrawParams.Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Compare)
			//{
			//	FDebugFloatHistory& C = MotionMatchingState.SearchCostHistoryContinuing;
			//	FDebugFloatHistory& B = MotionMatchingState.SearchCostHistoryBruteForce;
			//	FDebugFloatHistory& K = MotionMatchingState.SearchCostHistoryKDTree;

			//	C.AddSample(CurResult.ContinuingPoseCost.IsValid() ? CurResult.ContinuingPoseCost.GetTotalCost() : C.MaxValue);
			//	B.AddSample(CurResult.BruteForcePoseCost.IsValid() ? CurResult.BruteForcePoseCost.GetTotalCost() : B.MaxValue);
			//	K.AddSample(CurResult.PoseCost.IsValid() ? CurResult.PoseCost.GetTotalCost() : K.MaxValue);

			//	// making SearchCostHistoryKDTree and SearchCostHistoryBruteForce min max consistent
			//	const float MinValue = FMath::Min(C.MinValue, FMath::Min(B.MinValue, K.MinValue));
			//	const float MaxValue = FMath::Max(C.MaxValue, FMath::Max(B.MaxValue, K.MaxValue));

			//	C.MinValue = MinValue;
			//	C.MaxValue = MaxValue;

			//	B.MinValue = MinValue;
			//	B.MaxValue = MaxValue;

			//	K.MinValue = MinValue;
			//	K.MaxValue = MaxValue;

			//	const FVector2D DrawSize(150.f, 100.f);
			//	const FTransform OffsetTransform(FRotator(0.f, 0.f, 0.f), FVector(-50.f, -75.f, 100.f));
			//	const FTransform DrawTransform = OffsetTransform * DrawParams.GetRootTransform();

			//	DrawDebugFloatHistory(*DrawParams.World, K, OffsetTransform * DrawParams.GetRootTransform(), DrawSize, FColor(255, 192, 203, 160)); // pink
			//	DrawDebugFloatHistory(*DrawParams.World, B, OffsetTransform * DrawParams.GetRootTransform(), DrawSize, FColor(0, 0, 255, 160)); // blue
			//	DrawDebugFloatHistory(*DrawParams.World, C, OffsetTransform * DrawParams.GetRootTransform(), DrawSize, FColor(160, 160, 160, 160)); // gray
			//}
		}
	} 
#endif

	bForceInterruptNextUpdate = false;
}

void FAnimNode_MotionMatching::GatherDebugData(FNodeDebugData& DebugData)
{
	Source.GatherDebugData(DebugData);
}

void FAnimNode_MotionMatching::SetDatabaseToSearch(UPoseSearchDatabase* InDatabase, bool bForceInterruptIfNew)
{
	if (DatabasesToSearch.Num() == 1 && DatabasesToSearch[0] == InDatabase)
	{
		UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::SetDatabaseToSearch - Database(%s) is already set."), *GetNameSafe(InDatabase));
	}
	else
	{
		DatabasesToSearch.Reset();
		bOverrideDatabaseInput = false;
		if (InDatabase)
		{
			DatabasesToSearch.Add(InDatabase);
			bOverrideDatabaseInput = true;
		}

		bForceInterruptNextUpdate |= bForceInterruptIfNew;

		UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::SetDatabaseToSearch - Setting to Database(%s), bForceInterruptIfNew(%d)."), *GetNameSafe(InDatabase), bForceInterruptIfNew);
	}
}

void FAnimNode_MotionMatching::SetDatabasesToSearch(const TArray<UPoseSearchDatabase*>& InDatabases, bool bForceInterruptIfNew)
{
	// Check if InDatabases and DatabasesToSearch are the same.
	bool bDatabasesAlreadySet = true;
	if (DatabasesToSearch.Num() != InDatabases.Num())
	{
		bDatabasesAlreadySet = false;
	}
	else
	{
		for (int32 Index = 0; Index < InDatabases.Num(); ++Index)
		{
			if (DatabasesToSearch[Index] != InDatabases[Index])
			{
				bDatabasesAlreadySet = false;
				break;
			}
		}
	}

	if (bDatabasesAlreadySet)
	{
		UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::SetDatabasesToSearch - Databases(#%d) already set."), InDatabases.Num());
	}
	else
	{
		DatabasesToSearch.Reset();
		bOverrideDatabaseInput = false;
		if (!InDatabases.IsEmpty())
		{
			DatabasesToSearch.Append(InDatabases);
			bOverrideDatabaseInput = true;
		}

		bForceInterruptNextUpdate |= bForceInterruptIfNew;

		UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::SetDatabaseToSearch - Setting to Databases(#%d), bForceInterruptIfNew(%d)."), InDatabases.Num(), bForceInterruptIfNew);
	}
}

void FAnimNode_MotionMatching::ResetDatabasesToSearch(bool bInForceInterrupt)
{
	DatabasesToSearch.Reset();
	bOverrideDatabaseInput = false;
	bForceInterruptNextUpdate = bInForceInterrupt;

	UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::ResetDatabasesToSearch - Resetting databases, bInForceInterrupt(%d)."), bInForceInterrupt);
}

void FAnimNode_MotionMatching::ForceInterruptNextUpdate()
{
	bForceInterruptNextUpdate = true;

	UE_LOG(LogPoseSearch, Verbose, TEXT("FAnimNode_MotionMatching::ForceInterruptNextUpdate - Forcing interrupt."));
}

// FAnimNode_AssetPlayerBase interface
float FAnimNode_MotionMatching::GetAccumulatedTime() const
{
	return BlendStackNode.GetAccumulatedTime();
}

UAnimationAsset* FAnimNode_MotionMatching::GetAnimAsset() const
{
	return BlendStackNode.GetAnimAsset();
}

float FAnimNode_MotionMatching::GetCurrentAssetLength() const
{
	return BlendStackNode.GetCurrentAssetLength();
}

float FAnimNode_MotionMatching::GetCurrentAssetTime() const
{
	return BlendStackNode.GetCurrentAssetLength();
}

float FAnimNode_MotionMatching::GetCurrentAssetTimePlayRateAdjusted() const
{
	return BlendStackNode.GetCurrentAssetTimePlayRateAdjusted();
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
