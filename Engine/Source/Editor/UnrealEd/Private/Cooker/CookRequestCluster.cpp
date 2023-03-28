// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookRequestCluster.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Algo/TopologicalSort.h"
#include "Algo/Unique.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/CookPlatformManager.h"
#include "Cooker/CookProfiling.h"
#include "Cooker/CookRequests.h"
#include "Cooker/CookTypes.h"
#include "Cooker/PackageTracker.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "Engine/Level.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/RedirectCollector.h"
#include "Misc/StringBuilder.h"
#include "String/Find.h"
#include "UObject/CoreRedirects.h"
#include "UObject/SavePackage.h"

namespace UE::Cook
{

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS)
	: COTFS(InCOTFS)
	, PackageDatas(*InCOTFS.PackageDatas)
	, AssetRegistry(*IAssetRegistry::Get())
	, PackageTracker(*InCOTFS.PackageTracker)
	, BuildDefinitions(*InCOTFS.BuildDefinitions)
{
	if (!COTFS.IsCookOnTheFlyMode())
	{
		UE::Cook::FCookByTheBookOptions& Options = *COTFS.CookByTheBookOptions;
		bAllowHardDependencies = !Options.bSkipHardReferences;
		bAllowSoftDependencies = !Options.bSkipSoftReferences;
		bErrorOnEngineContentUse = Options.bErrorOnEngineContentUse;
		bAllowUncookedAssetReferences = Options.bAllowUncookedAssetReferences;
	}
	else
	{
		// Do not queue soft-dependencies during CookOnTheFly; wait for them to be requested
		// TODO: Report soft dependencies separately, and mark them as normal priority,
		// and mark all hard dependencies as high priority in cook on the fly.
		bAllowSoftDependencies = false;
	}
	bHybridIterativeEnabled = COTFS.bHybridIterativeEnabled;
	if (bErrorOnEngineContentUse)
	{
		DLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(DLCPath);
	}
	GConfig->GetBool(TEXT("CookSettings"), TEXT("PreQueueBuildDefinitions"), bPreQueueBuildDefinitions, GEditorIni);

	bFullBuild = false;
	bool bFirst = true;
	for (const ITargetPlatform* TargetPlatform : COTFS.PlatformManager->GetSessionPlatforms())
	{
		FPlatformData* PlatformData = COTFS.PlatformManager->GetPlatformData(TargetPlatform);
		if (bFirst)
		{
			bFullBuild = PlatformData->bFullBuild;
			bFirst = false;
		}
		else
		{
			if (PlatformData->bFullBuild != bFullBuild)
			{
				UE_LOG(LogCook, Warning, TEXT("Full build is requested for some platforms but not others, but this is not supported. All platforms will be built full."));
				bFullBuild = true;
			}
		}
	}
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, TArray<FFilePlatformRequest>&& InRequests, bool bInExternalRequestsAreUrgent)
	: FRequestCluster(InCOTFS)
{
	bExternalRequestsAreUrgent = bInExternalRequestsAreUrgent;
	ReserveInitialRequests(InRequests.Num());
	FilePlatformRequests = MoveTemp(InRequests);
	InRequests.Empty();
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, FPackageDataSet&& InRequests)
	: FRequestCluster(InCOTFS)
{
	ReserveInitialRequests(InRequests.Num());
	for (FPackageData* PackageData : InRequests)
	{
		ESuppressCookReason& Existing = OwnedPackageDatas.FindOrAdd(PackageData, ESuppressCookReason::Invalid);
		check(Existing == ESuppressCookReason::Invalid);
		Existing = ESuppressCookReason::NotSuppressed;
	}
	InRequests.Empty();
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& InCOTFS, TRingBuffer<FDiscoveryQueueElement>& DiscoveryQueue)
	: FRequestCluster(InCOTFS)
{
	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> BufferPlatforms;
	while (!DiscoveryQueue.IsEmpty())
	{
		FDiscoveryQueueElement* Discovery = &DiscoveryQueue.First();
		FPackageData& PackageData = *Discovery->PackageData;

		TConstArrayView<const ITargetPlatform*> NewReachablePlatforms =
			Discovery->ReachablePlatforms.GetPlatforms(COTFS, &Discovery->Instigator,
				TConstArrayView<const ITargetPlatform*>(), &BufferPlatforms);
		if (PackageData.HasReachablePlatforms(NewReachablePlatforms))
		{
			// If there are no new reachable platforms, add it to the cluster for cooking if it needs
			// it, otherwise let it remain where it is
			FDiscoveryQueueElement PoppedDiscovery = DiscoveryQueue.PopFrontValue();
			Discovery = &PoppedDiscovery;
			if (!PackageData.IsInProgress() && PackageData.GetPlatformsNeedingCookingNum() == 0)
			{
				PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove);
				OwnedPackageDatas.Add(&PackageData, ESuppressCookReason::NotSuppressed);
			}
			continue;
		}

		if (NewReachablePlatforms.Contains(CookerLoadingPlatformKey) &&
			!PackageData.FindOrAddPlatformData(CookerLoadingPlatformKey).IsReachable())
		{
			// We are adding the expectation that this package will be loaded for something during the cook. Doing so
			// is only expected from a few types of instigators, or from external package requests, or from cluster
			// exploration. If not expected, add a diagnostic message, and add it as a hidden dependency.
			if (Discovery->Instigator.Category != EInstigator::StartupPackage &&
				Discovery->Instigator.Category != EInstigator::GeneratedPackage)
			{
				// If there are other discovered packages we have already added to this cluster, then defer this one
				// until we have explored those; add this one to the next cluster. Exploring those earlier discoveries
				// might uncover this one and make it expected.
				if (!OwnedPackageDatas.IsEmpty())
				{
					break;
				}

				COTFS.OnDiscoveredPackageDebug(PackageData.GetPackageName(), Discovery->Instigator);
				FPackageData* InstigatorPackageData = Discovery->Instigator.Referencer.IsNone() ? nullptr
					: COTFS.PackageDatas->TryAddPackageDataByPackageName(Discovery->Instigator.Referencer);
				if (InstigatorPackageData)
				{
					COTFS.DiscoveredDependencies.FindOrAdd(InstigatorPackageData->GetPackageName())
						.Add(PackageData.GetPackageName());
				}
			}
		}
		// Add the new reachable platforms
		PackageData.AddReachablePlatforms(*this, NewReachablePlatforms, MoveTemp(Discovery->Instigator));

		// Pop it off the list; note that this invalidates the pointers we had into the DiscoveryQueueElement
		FDiscoveryQueueElement PoppedDiscovery = DiscoveryQueue.PopFrontValue();
		Discovery = &PoppedDiscovery;
		NewReachablePlatforms = TConstArrayView<const ITargetPlatform*>();

		// Send it to the Request state if it's not already there, remove it from its old container
		// and add it to this cluster.
		PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove);
		OwnedPackageDatas.Add(&PackageData, ESuppressCookReason::NotSuppressed);
	}
}

FName GInstigatorRequestCluster(TEXT("RequestCluster"));

void FRequestCluster::Process(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	bOutComplete = true;

	FetchPackageNames(CookerTimer, bOutComplete);
	if (!bOutComplete)
	{
		return;
	}
	PumpExploration(CookerTimer, bOutComplete);
	if (!bOutComplete)
	{
		return;
	}
	StartAsync(CookerTimer, bOutComplete);
}

void FRequestCluster::FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bPackageNamesComplete)
	{
		return;
	}

	constexpr int32 TimerCheckPeriod = 100; // Do not incur the cost of checking the timer on every package
	int32 NextRequest = 0;
	for (; NextRequest < FilePlatformRequests.Num(); ++NextRequest)
	{
		if ((NextRequest+1) % TimerCheckPeriod == 0 && CookerTimer.IsTimeUp())
		{
			break;
		}

		FFilePlatformRequest& Request = FilePlatformRequests[NextRequest];
		FName OriginalName = Request.GetFilename();

		// The input filenames are normalized, but might be missing their extension, so allow PackageDatas
		// to correct the filename if the package is found with a different filename
		bool bExactMatchRequired = false;
		FPackageData* PackageData = PackageDatas.TryAddPackageDataByStandardFileName(OriginalName, bExactMatchRequired);
		if (!PackageData)
		{
			LogCookerMessage(FString::Printf(TEXT("Could not find package at file %s!"),
				*OriginalName.ToString()), EMessageSeverity::Error);
			UE_LOG(LogCook, Error, TEXT("Could not find package at file %s!"), *OriginalName.ToString());
			FCompletionCallback CompletionCallback(MoveTemp(Request.GetCompletionCallback()));
			if (CompletionCallback)
			{
				CompletionCallback(nullptr);
			}
			continue;
		}

		// If it has new reachable platforms we definitely need to explore it
		if (!PackageData->HasReachablePlatforms(Request.GetPlatforms()))
		{
			PackageData->AddReachablePlatforms(*this, Request.GetPlatforms(), MoveTemp(Request.GetInstigator()));
			PullIntoCluster(*PackageData);
			PackageData->AddUrgency(bExternalRequestsAreUrgent, false /* bAllowUpdateState */);
		}
		else
		{
			if (PackageData->IsInProgress())
			{
				// If it's already in progress with no new platforms, we don't need to add it to the cluster, but add
				// add on our urgency setting
				PackageData->AddUrgency(bExternalRequestsAreUrgent, true /* bAllowUpdateState */);
			}
			else if (PackageData->GetPlatformsNeedingCookingNum() > 0)
			{
				// If it's missing cookable platforms and not in progress we need to add it to the cluster for cooking
				PullIntoCluster(*PackageData);
				PackageData->AddUrgency(bExternalRequestsAreUrgent, true /* bAllowUpdateState */);
			}
		}
		// Add on our completion callback, or call it immediately if already done
		PackageData->AddCompletionCallback(Request.GetPlatforms(), MoveTemp(Request.GetCompletionCallback()));
	}
	if (NextRequest < FilePlatformRequests.Num())
	{
		FilePlatformRequests.RemoveAt(0, NextRequest);
		bOutComplete = false;
		return;
	}

	FilePlatformRequests.Empty();
	bPackageNamesComplete = true;
}

void FRequestCluster::ReserveInitialRequests(int32 RequestNum)
{
	OwnedPackageDatas.Reserve(FMath::Max(RequestNum, 1024));
}

void FRequestCluster::PullIntoCluster(FPackageData& PackageData)
{
	ESuppressCookReason& Existing = OwnedPackageDatas.FindOrAdd(&PackageData, ESuppressCookReason::Invalid);
	if (Existing == ESuppressCookReason::Invalid)
	{
		// Steal it from wherever it is and send it to Request State. It has already been added to this cluster
		if (PackageData.GetState() == EPackageState::Request)
		{
			COTFS.PackageDatas->GetRequestQueue().RemoveRequestExceptFromCluster(&PackageData, this);
		}
		else
		{
			PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove);
		}
		Existing = ESuppressCookReason::NotSuppressed;
	}
}

void FRequestCluster::StartAsync(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	using namespace UE::DerivedData;
	using namespace UE::EditorDomain;

	if (bStartAsyncComplete)
	{
		return;
	}

	FEditorDomain* EditorDomain = FEditorDomain::Get();
	if (EditorDomain)
	{
		bool bBatchDownloadEnabled = true;
		GConfig->GetBool(TEXT("EditorDomain"), TEXT("BatchDownloadEnabled"), bBatchDownloadEnabled, GEditorIni);
		if (bBatchDownloadEnabled)
		{
			// If the EditorDomain is active, then batch-download all packages to cook from remote cache into local
			TArray<FName> BatchDownload;
			BatchDownload.Reserve(OwnedPackageDatas.Num());
			for (TPair<FPackageData*, ESuppressCookReason>& Pair : OwnedPackageDatas)
			{
				if (Pair.Value == ESuppressCookReason::NotSuppressed)
				{
					BatchDownload.Add(Pair.Key->GetPackageName());
				}
			};
			EditorDomain->BatchDownload(BatchDownload);
		}
	}

	bStartAsyncComplete = true;
}

int32 FRequestCluster::NumPackageDatas() const
{
	return OwnedPackageDatas.Num();
}

void FRequestCluster::RemovePackageData(FPackageData* PackageData)
{
	if (OwnedPackageDatas.Remove(PackageData) == 0)
	{
		return;
	}

	if (GraphSearch)
	{
		GraphSearch->RemovePackageData(PackageData);
	}
}

void FRequestCluster::OnNewReachablePlatforms(FPackageData* PackageData)
{
	if (GraphSearch)
	{
		GraphSearch->OnNewReachablePlatforms(PackageData);
	}
}

void FRequestCluster::OnPlatformAddedToSession(const ITargetPlatform* TargetPlatform)
{
	if (GraphSearch)
	{
		FCookerTimer CookerTimer(FCookerTimer::Forever);
		bool bComplete;
		while (PumpExploration(CookerTimer, bComplete), !bComplete)
		{
			UE_LOG(LogCook, Display, TEXT("Waiting for RequestCluster to finish before adding platform to session."));
			FPlatformProcess::Sleep(.001f);
		}
	}
}

void FRequestCluster::OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
{
	if (GraphSearch)
	{
		FCookerTimer CookerTimer(FCookerTimer::Forever);
		bool bComplete;
		while (PumpExploration(CookerTimer, bComplete), !bComplete)
		{
			UE_LOG(LogCook, Display, TEXT("Waiting for RequestCluster to finish before removing platform from session."));
			FPlatformProcess::Sleep(.001f);
		}
	}
}

void FRequestCluster::RemapTargetPlatforms(TMap<ITargetPlatform*, ITargetPlatform*>& Remap)
{
	if (GraphSearch)
	{
		// The platforms have already been invalidated, which means we can't wait for GraphSearch to finish
		// Need to wait for all async operations to finish, then remap all the platforms
		checkNoEntry(); // Not yet implemented
	}
}

bool FRequestCluster::Contains(FPackageData* PackageData) const
{
	return OwnedPackageDatas.Contains(PackageData);
}

void FRequestCluster::ClearAndDetachOwnedPackageDatas(TArray<FPackageData*>& OutRequestsToLoad,
	TArray<TPair<FPackageData*, ESuppressCookReason>>& OutRequestsToDemote,
	TMap<FPackageData*, TArray<FPackageData*>>& OutRequestGraph)
{
	if (bStartAsyncComplete)
	{
		check(!GraphSearch);
		OutRequestsToLoad.Reset();
		OutRequestsToDemote.Reset();
		for (TPair<FPackageData*, ESuppressCookReason>& Pair : OwnedPackageDatas)
		{
			if (Pair.Value == ESuppressCookReason::NotSuppressed)
			{
				OutRequestsToLoad.Add(Pair.Key);
			}
			else
			{
				OutRequestsToDemote.Add(Pair);
			}
		}
		OutRequestGraph = MoveTemp(RequestGraph);
	}
	else
	{
		OutRequestsToLoad.Reset();
		for (TPair<FPackageData*, ESuppressCookReason>& Pair : OwnedPackageDatas)
		{
			OutRequestsToLoad.Add(Pair.Key);
		}
		OutRequestsToDemote.Reset();
		OutRequestGraph.Reset();
	}
	FilePlatformRequests.Empty();
	OwnedPackageDatas.Empty();
	GraphSearch.Reset();
	RequestGraph.Reset();
}

void FRequestCluster::PumpExploration(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bDependenciesComplete)
	{
		return;
	}

	if (!GraphSearch)
	{
		GraphSearch.Reset(new FGraphSearch(*this));

		if (!bAllowHardDependencies || COTFS.IsCookWorkerMode())
		{
			GraphSearch->VisitWithoutDependencies();
			GraphSearch.Reset();
			bDependenciesComplete = true;
			return;
		}
		GraphSearch->StartSearch();
	}

	constexpr double WaitTime = 0.50;
	bool bDone;
	while (GraphSearch->TickExploration(bDone), !bDone)
	{
		GraphSearch->WaitForAsyncQueue(WaitTime);
		if (CookerTimer.IsTimeUp())
		{
			bOutComplete = false;
			return;
		}
	}

	TArray<FPackageData*> SortedPackages;
	SortedPackages.Reserve(OwnedPackageDatas.Num());
	for (TPair<FPackageData*, ESuppressCookReason>& Pair : OwnedPackageDatas)
	{
		if (Pair.Value == ESuppressCookReason::NotSuppressed)
		{
			SortedPackages.Add(Pair.Key);
		}
	}

	// Sort the NewRequests in leaf to root order and replace the requests list with NewRequests
	TArray<FPackageData*> Empty;
	auto GetElementDependencies = [this, &Empty](FPackageData* PackageData) -> const TArray<FPackageData*>&
	{
		const TArray<FPackageData*>* VertexEdges = GraphSearch->GetGraphEdges().Find(PackageData);
		return VertexEdges ? *VertexEdges : Empty;
	};

	Algo::TopologicalSort(SortedPackages, GetElementDependencies, Algo::ETopologicalSort::AllowCycles);
	TMap<FPackageData*, int32> SortOrder;
	int32 Counter = 0;
	SortOrder.Reserve(SortedPackages.Num());
	for (FPackageData* PackageData : SortedPackages)
	{
		SortOrder.Add(PackageData, Counter++);
	}
	OwnedPackageDatas.KeySort([&SortOrder](const FPackageData& A, const FPackageData& B)
		{
			int32* CounterA = SortOrder.Find(&A);
			int32* CounterB = SortOrder.Find(&B);
			if ((CounterA != nullptr) != (CounterB != nullptr))
			{
				// Sort the demotes to occur last
				return CounterB == nullptr;
			}
			else if (CounterA)
			{
				return *CounterA < *CounterB;
			}
			else
			{
				return false; // demotes are unsorted
			}
		});

	RequestGraph = MoveTemp(GraphSearch->GetGraphEdges());
	GraphSearch.Reset();
	bDependenciesComplete = true;
}

FRequestCluster::FGraphSearch::FGraphSearch(FRequestCluster& InCluster)
	: Cluster(InCluster)
	, AsyncResultsReadyEvent(EEventMode::ManualReset)
{
	AsyncResultsReadyEvent->Trigger();
	bCookAttachmentsEnabled = !Cluster.bFullBuild && Cluster.bHybridIterativeEnabled;
	LastActivityTime = FPlatformTime::Seconds();
	VertexAllocator.SetMaxBlockSize(1024);
	VertexAllocator.SetMaxBlockSize(65536);
	VertexQueryAllocator.SetMinBlockSize(1024);
	VertexQueryAllocator.SetMaxBlockSize(1024);
	BatchAllocator.SetMaxBlockSize(16);
	BatchAllocator.SetMaxBlockSize(16);

	TConstArrayView<const ITargetPlatform*> SessionPlatforms = Cluster.COTFS.PlatformManager->GetSessionPlatforms();
	check(SessionPlatforms.Num() > 0);
	FetchPlatforms.SetNum(SessionPlatforms.Num() + 2);
	FetchPlatforms[PlatformAgnosticPlatformIndex].bIsPlatformAgnosticPlatform = true;
	FetchPlatforms[CookerLoadingPlatformIndex].Platform = CookerLoadingPlatformKey;
	FetchPlatforms[CookerLoadingPlatformIndex].bIsCookerLoadingPlatform = true;
	for (int32 SessionPlatformIndex = 0; SessionPlatformIndex < SessionPlatforms.Num(); ++SessionPlatformIndex)
	{
		FFetchPlatformData& FetchPlatform = FetchPlatforms[SessionPlatformIndex + 2];
		FetchPlatform.Platform = SessionPlatforms[SessionPlatformIndex];
		FetchPlatform.Writer = &Cluster.COTFS.FindOrCreatePackageWriter(FetchPlatform.Platform);
	}
	Algo::Sort(FetchPlatforms, [](const FFetchPlatformData& A, const FFetchPlatformData& B)
		{
			return A.Platform < B.Platform;
		});
	check(FetchPlatforms[PlatformAgnosticPlatformIndex].bIsPlatformAgnosticPlatform);
	check(FetchPlatforms[CookerLoadingPlatformIndex].bIsCookerLoadingPlatform);
}

void FRequestCluster::FGraphSearch::VisitWithoutDependencies()
{
	// PumpExploration is responsible for marking all requests as explored and cookable/uncoookable.
	// If we're skipping the dependencies search, handle that responsibility for the initial requests and return.
	for (TPair<FPackageData*, ESuppressCookReason>& Pair : Cluster.OwnedPackageDatas)
	{
		FVertexData Vertex;
		Vertex.PackageData = Pair.Key;
		VisitVertex(Vertex, true /* bSkipDependencies */);
	}
}

void FRequestCluster::FGraphSearch::StartSearch()
{
	Frontier.Reserve(Cluster.OwnedPackageDatas.Num());
	for (TPair<FPackageData*, ESuppressCookReason>& Pair : Cluster.OwnedPackageDatas)
	{
		FVertexData& Vertex = FindOrAddVertex(Pair.Key->GetPackageName(), *Pair.Key);
		check(Vertex.PackageData);
		// We're iterating over OwnedPackageDatas and the Vertex is already in the Cluster so we don't need to call AddToFrontier;
		// just add it directly.
		check(Pair.Value != ESuppressCookReason::Invalid);
		Frontier.Add(&Vertex);
	}
}

FRequestCluster::FGraphSearch::~FGraphSearch()
{
	for (;;)
	{
		bool bHadActivity = false;
		bool bAsyncBatchesEmpty = false;
		{
			FScopeLock ScopeLock(&Lock);
			bAsyncBatchesEmpty = AsyncQueueBatches.IsEmpty();
			if (!bAsyncBatchesEmpty)
			{
				// It is safe to Reset AsyncResultsReadyEvent and wait on it later because we are inside the lock and there is a
				// remaining batch, so it will be triggered after the Reset when that batch completes.
				AsyncResultsReadyEvent->Reset();
			}
		}
		for (;;)
		{
			TOptional<FVertexData*> Vertex = AsyncQueueResults.Dequeue();
			if (!Vertex)
			{
				break;
			}
			FreeQueryData((**Vertex).QueryData);
			(**Vertex).QueryData = nullptr;
			bHadActivity = true;
		}
		if (bAsyncBatchesEmpty)
		{
			break;
		}
		if (bHadActivity)
		{
			LastActivityTime = FPlatformTime::Seconds();
		}
		else
		{
			UpdateDisplay();
		}
		constexpr double WaitTime = 1.0;
		WaitForAsyncQueue(WaitTime);
	}
}

void FRequestCluster::FGraphSearch::RemovePackageData(FPackageData* PackageData)
{
	check(PackageData);
	FVertexData** Vertex = Vertices.Find(PackageData->GetPackageName());
	if (Vertex)
	{
		(**Vertex).PackageData = nullptr;
	}

	GraphEdges.Remove(PackageData);
	for (TPair<FPackageData*, TArray<FPackageData*>>& Pair : GraphEdges)
	{
		Pair.Value.Remove(PackageData);
	}
}

void FRequestCluster::FGraphSearch::OnNewReachablePlatforms(FPackageData* PackageData)
{
	FVertexData** VertexPtr = Vertices.Find(PackageData->GetPackageName());
	if (!VertexPtr)
	{
		return;
	}
	// Already in OwnedPackageDatas, so just add to Frontier directly
	Frontier.Add(*VertexPtr);
}

void FRequestCluster::FGraphSearch::QueueEdgesFetch(FVertexData& Vertex, TConstArrayView<const ITargetPlatform*> Platforms)
{
	check(!Vertex.QueryData);
	FVertexQueryData& QueryData = *AllocateQueryData();
	Vertex.QueryData = &QueryData;
	
	QueryData.PackageName = Vertex.PackageData->GetPackageName();
	QueryData.Platforms.SetNum(FetchPlatforms.Num());

	// Store Platforms in QueryData->Platforms.bActive. All bActive values start false from constructor or from Reset
	bool bHasPlatformAgnostic = false;
	for (const ITargetPlatform* Platform : Platforms)
	{
		int32 Index = Algo::BinarySearchBy(FetchPlatforms, Platform, [](const FFetchPlatformData& D) { return D.Platform; });
		check(Index != INDEX_NONE);
		QueryData.Platforms[Index].bActive = true;
		if (Platform != CookerLoadingPlatformKey)
		{
			bHasPlatformAgnostic = true;
		}
	}
	if (bHasPlatformAgnostic)
	{
		QueryData.Platforms[PlatformAgnosticPlatformIndex].bActive = true;
	}
	int32 NumPendingPlatforms = Platforms.Num() + (bHasPlatformAgnostic ? 1 : 0);
	QueryData.PendingPlatforms.store(NumPendingPlatforms, std::memory_order_release);

	PreAsyncQueue.Add(&Vertex);
	CreateAvailableBatches(false /* bAllowIncompleteBatch */);
}

void FRequestCluster::FGraphSearch::WaitForAsyncQueue(double WaitTimeSeconds)
{
	uint32 WaitTime = (WaitTimeSeconds > 0.0) ? static_cast<uint32>(FMath::Floor(WaitTimeSeconds * 1000)) : MAX_uint32;
	AsyncResultsReadyEvent->Wait(WaitTime);
}

void FRequestCluster::FGraphSearch::TickExploration(bool& bOutDone)
{
	bool bHadActivity = false;
	for (;;)
	{
		TOptional<FVertexData*> Vertex = AsyncQueueResults.Dequeue();
		if (!Vertex.IsSet())
		{
			break;
		}
		ExploreVertexEdges(**Vertex);
		FreeQueryData((**Vertex).QueryData);
		(**Vertex).QueryData = nullptr;
		bHadActivity = true;
	}

	if (!Frontier.IsEmpty())
	{
		TArray<FVertexData*> BusyVertices;
		for (FVertexData* Vertex : Frontier)
		{
			if (Vertex->QueryData)
			{
				// Vertices that are already in the AsyncQueue can not be added again; we would clobber their QueryData. Postpone them.
				BusyVertices.Add(Vertex);
			}
			else
			{
				VisitVertex(*Vertex);
			}
		}
		bHadActivity |= BusyVertices.Num() != Frontier.Num();
		Frontier.Reset();
		Frontier.Append(BusyVertices);
	}

	if (bHadActivity)
	{
		LastActivityTime = FPlatformTime::Seconds();
		bOutDone = false;
		return;
	}

	bool bAsyncQueueEmpty;
	{
		FScopeLock ScopeLock(&Lock);
		if (!AsyncQueueResults.IsEmpty())
		{
			bAsyncQueueEmpty = false;
		}
		else
		{
			bAsyncQueueEmpty = AsyncQueueBatches.IsEmpty();
			// AsyncResultsReadyEvent can only be Reset when either the AsyncQueue is empty or it is non-empty and we
			// know the AsyncResultsReadyEvent will be triggered again "later".
			// The guaranteed place where it will be Triggered is when a batch completes. To guarantee that
			// place will be called "later", the batch completion trigger and this reset have to both
			// be done inside the lock.
			AsyncResultsReadyEvent->Reset();
		}
	}
	if (!bAsyncQueueEmpty)
	{
		// Waiting on the AsyncQueue; give a warning if we have been waiting for long with no AsyncQueueResults.
		UpdateDisplay();
		bOutDone = false;
		return;
	}

	// No more work coming in the future from the AsyncQueue, and we are out of work to do
	// without it. If we have any queued vertices in the PreAsyncQueue, send them now and continue
	// waiting. Otherwise we are done.
	if (!PreAsyncQueue.IsEmpty())
	{
		CreateAvailableBatches(true /* bAllowInCompleteBatch */);
		bOutDone = false;
		return;
	}

	// Frontier was reset above, and it cannot be modified between there and here.
	// If it were non-empty we would not be done.
	check(Frontier.IsEmpty());
	bOutDone = true;
}

void FRequestCluster::FGraphSearch::UpdateDisplay()
{
	constexpr double WarningTimeout = 10.0;
	if (FPlatformTime::Seconds() > LastActivityTime + WarningTimeout && bCookAttachmentsEnabled)
	{
		FScopeLock ScopeLock(&Lock);
		int32 NumVertices = 0;
		int32 NumBatches = AsyncQueueBatches.Num();
		for (FQueryVertexBatch* Batch : AsyncQueueBatches)
		{
			NumVertices += Batch->PendingVertices;
		}

		UE_LOG(LogCook, Warning, TEXT("FRequestCluster waited more than %.0lfs for previous build results from the oplog. ")
			TEXT("NumPendingBatches == %d, NumPendingVertices == %d. Continuing to wait..."),
			WarningTimeout, NumBatches, NumVertices);
		LastActivityTime = FPlatformTime::Seconds();
	}
}

void FRequestCluster::FGraphSearch::VisitVertex(FVertexData& Vertex, bool bSkipDependencies)
{
	// Only called from PumpExploration thread

	// The PackageData will not exist if the package does not exist on disk or
	// the PackageData was removed from the FRequestCluster due to changes in the PackageData's
	// state elsewhere in the cooker.
	if (!Vertex.PackageData)
	{
		return;
	}

	TArray<const ITargetPlatform*, TInlineAllocator<1>> ExplorePlatforms;
	FPackagePlatformData* CookerLoadingPlatform = nullptr;
	const ITargetPlatform* FirstReachableSessionPlatform = nullptr;
	ESuppressCookReason SuppressCookReason = ESuppressCookReason::Invalid;
	bool bAllReachablesUncookable = true;
	for (TPair<const ITargetPlatform*, FPackagePlatformData>& Pair : Vertex.PackageData->GetPlatformDatasConstKeysMutableValues())
	{
		FPackagePlatformData& PlatformData = Pair.Value;
		if (Pair.Key == CookerLoadingPlatformKey)
		{
			CookerLoadingPlatform = &Pair.Value;
		}
		else if (PlatformData.IsReachable())
		{
			if (!FirstReachableSessionPlatform)
			{
				FirstReachableSessionPlatform = Pair.Key;
			}
			if (!PlatformData.IsVisitedByCluster())
			{
				VisitVertexForPlatform(Vertex, Pair.Key, PlatformData, SuppressCookReason);
				if (!bSkipDependencies && PlatformData.IsExplorable())
				{
					ExplorePlatforms.Add(Pair.Key);
				}
			}
			if (PlatformData.IsCookable())
			{
				bAllReachablesUncookable = false;
				SuppressCookReason = ESuppressCookReason::NotSuppressed;
			}
		}
	}
	bool bAnyCookable = (FirstReachableSessionPlatform == nullptr) | !bAllReachablesUncookable;
	if (bAnyCookable != Vertex.bAnyCookable)
	{
		if (!bAnyCookable)
		{
			if (SuppressCookReason == ESuppressCookReason::Invalid)
			{
				// We need the SuppressCookReason for reporting. If we didn't calculate it this Visit and
				// we don't have it stored in this->OwnedPackageDatas, then we must have calculated it in
				// a previous cluster, but we don't store it anywhere. Recalculate it from the
				// FirstReachableSessionPlatform. FirstReachableSessionPlatform must be non-null, otherwise
				// bAnyCookable would be true.
				check(FirstReachableSessionPlatform);
				bool bCookable;
				bool bExplorable;
				Cluster.IsRequestCookable(FirstReachableSessionPlatform, Vertex.PackageData->GetPackageName(),
					*Vertex.PackageData, SuppressCookReason, bCookable, bExplorable);
				check(!bCookable); // We don't support bCookable changing for a given package and platform
				check(SuppressCookReason != ESuppressCookReason::Invalid);
			}
		}
		else
		{
			check(SuppressCookReason == ESuppressCookReason::NotSuppressed);
		}
		Cluster.OwnedPackageDatas.FindOrAdd(Vertex.PackageData) = SuppressCookReason;
		Vertex.bAnyCookable = bAnyCookable;
	}

	// If any target platform is cookable, then we need to mark the CookerLoadingPlatform as reachable because we will need
	// to load the package to cook it
	if (bAnyCookable)
	{
		if (!CookerLoadingPlatform)
		{
			CookerLoadingPlatform = &Vertex.PackageData->FindOrAddPlatformData(CookerLoadingPlatformKey);
		}
		CookerLoadingPlatform->SetReachable(true);
	}
	if (CookerLoadingPlatform && CookerLoadingPlatform->IsReachable() && !CookerLoadingPlatform->IsVisitedByCluster())
	{
		CookerLoadingPlatform->SetCookable(true);
		CookerLoadingPlatform->SetExplorable(true);
		CookerLoadingPlatform->SetVisitedByCluster(true);
		if (!bSkipDependencies)
		{
			ExplorePlatforms.Add(CookerLoadingPlatformKey);
		}
	}

	if (!ExplorePlatforms.IsEmpty())
	{
		check(!bSkipDependencies);
		QueueEdgesFetch(Vertex, ExplorePlatforms);
	}
}

void FRequestCluster::FGraphSearch::VisitVertexForPlatform(FVertexData& Vertex, const ITargetPlatform* Platform,
	FPackagePlatformData& PlatformData, ESuppressCookReason& AccumulatedSuppressCookReason)
{
	FPackageData& PackageData = *Vertex.PackageData;
	ESuppressCookReason SuppressCookReason = ESuppressCookReason::Invalid;
	bool bCookable;
	bool bExplorable;
	Cluster.IsRequestCookable(Platform, Vertex.PackageData->GetPackageName(), PackageData, SuppressCookReason,
		bCookable, bExplorable);
	PlatformData.SetCookable(bCookable);
	PlatformData.SetExplorable(bExplorable);
	if (bCookable)
	{
		AccumulatedSuppressCookReason = ESuppressCookReason::NotSuppressed;
	}
	else
	{
		check(SuppressCookReason != ESuppressCookReason::Invalid && SuppressCookReason != ESuppressCookReason::NotSuppressed);
		if (AccumulatedSuppressCookReason == ESuppressCookReason::Invalid)
		{
			AccumulatedSuppressCookReason = SuppressCookReason;
		}
	}
	PlatformData.SetVisitedByCluster(true);
}

void FRequestCluster::FGraphSearch::ExploreVertexEdges(FVertexData& Vertex)
{
	// Only called from PumpExploration thread
	using namespace UE::AssetRegistry;
	using namespace UE::TargetDomain;

	// The PackageData will not exist if the package does not exist on disk or
	// the PackageData was removed from the FRequestCluster due to changes in the PackageData's
	// state elsewhere in the cooker.
	if (!Vertex.PackageData)
	{
		return;
	}

	TArray<FName>& HardGameDependencies(Scratch.HardGameDependencies);
	TArray<FName>& SoftGameDependencies(Scratch.SoftGameDependencies);
	TSet<FName>& HardDependenciesSet(Scratch.HardDependenciesSet);
	HardGameDependencies.Reset();
	SoftGameDependencies.Reset();
	HardDependenciesSet.Reset();
	FPackageData& PackageData = *Vertex.PackageData;
	FName PackageName = PackageData.GetPackageName();
	bool bFetchAnyTargetPlatform = Vertex.QueryData->Platforms[PlatformAgnosticPlatformIndex].bActive;
	TArray<FName>* DiscoveredDependencies = Cluster.COTFS.DiscoveredDependencies.Find(PackageName);
	if (bFetchAnyTargetPlatform)
	{
		EDependencyQuery FlagsForHardDependencyQuery;
		if (Cluster.COTFS.bCanSkipEditorReferencedPackagesWhenCooking)
		{
			FlagsForHardDependencyQuery = EDependencyQuery::Game | EDependencyQuery::Hard;
		}
		else
		{
			// We're not allowed to skip editoronly imports, so include all hard dependencies
			FlagsForHardDependencyQuery = EDependencyQuery::Hard;
		}
		Cluster.AssetRegistry.GetDependencies(PackageName, HardGameDependencies, EDependencyCategory::Package,
			FlagsForHardDependencyQuery);
		HardDependenciesSet.Append(HardGameDependencies);
		if (DiscoveredDependencies)
		{
			HardDependenciesSet.Append(*DiscoveredDependencies);
		}
		if (Cluster.bAllowSoftDependencies)
		{
			// bCanSkipEditorReferencedPackagesWhenCooking does not affect soft dependencies; skip editoronly soft dependencies
			Cluster.AssetRegistry.GetDependencies(PackageName, SoftGameDependencies, EDependencyCategory::Package,
				EDependencyQuery::Game | EDependencyQuery::Soft);

			// Even if we're following soft references in general, we need to check with the SoftObjectPath registry
			// for any startup packages that marked their softobjectpaths as excluded, and not follow those
			TSet<FName>& SkippedPackages(Scratch.SkippedPackages);
			if (GRedirectCollector.RemoveAndCopySoftObjectPathExclusions(PackageName, SkippedPackages))
			{
				SoftGameDependencies.RemoveAll([&SkippedPackages](FName SoftDependency)
					{
						return SkippedPackages.Contains(SoftDependency);
					});
			}

			// LocalizationReferences are a source of SoftGameDependencies that are not present in the AssetRegistry
			SoftGameDependencies.Append(GetLocalizationReferences(PackageName, Cluster.COTFS));
		}
	}

	int32 LocalNumFetchPlatforms = NumFetchPlatforms();
	TMap<FName, FScratchPlatformDependencyBits>& PlatformDependencyMap(Scratch.PlatformDependencyMap);
	PlatformDependencyMap.Reset();
	auto AddPlatformDependency = [&PlatformDependencyMap, LocalNumFetchPlatforms](FName DependencyName, int32 PlatformIndex, bool bHardDependency)
	{
		FScratchPlatformDependencyBits& PlatformDependencyBits = PlatformDependencyMap.FindOrAdd(DependencyName);
		if (PlatformDependencyBits.HasPlatformByIndex.Num() != LocalNumFetchPlatforms)
		{
			PlatformDependencyBits.HasPlatformByIndex.Init(false, LocalNumFetchPlatforms);
			PlatformDependencyBits.bHardDependency = false;
		}
		PlatformDependencyBits.HasPlatformByIndex[PlatformIndex] = true;
		if (bHardDependency)
		{
			PlatformDependencyBits.bHardDependency = true;
		}

	};
	auto AddPlatformDependencyRange = [&AddPlatformDependency](TConstArrayView<FName> Range, int32 PlatformIndex, bool bHardDependency)
	{
		for (FName DependencyName : Range)
		{
			AddPlatformDependency(DependencyName, PlatformIndex, bHardDependency);
		}
	};

	FQueryPlatformData& PlatformAgnosticQueryPlatformData = Vertex.QueryData->Platforms[PlatformAgnosticPlatformIndex];
	for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
	{
		FQueryPlatformData& QueryPlatformData = Vertex.QueryData->Platforms[PlatformIndex];
		if (!QueryPlatformData.bActive || PlatformIndex == PlatformAgnosticPlatformIndex)
		{
			continue;
		}

		if (PlatformIndex == CookerLoadingPlatformIndex)
		{
			TArray<FName>& CookerLoadingDependencies(Scratch.CookerLoadingDependencies);
			CookerLoadingDependencies.Reset();

			Cluster.AssetRegistry.GetDependencies(PackageName, CookerLoadingDependencies, EDependencyCategory::Package,
				EDependencyQuery::Hard);
			CookerLoadingDependencies.Reset();

			// ITERATIVECOOK_TODO: Build dependencies need to be stored and used to mark package loads as expected
			// But we can't use them to explore packages that will be loaded during cook because they might not be;
			// some build dependencies might be a conservative list but unused by the asset, or unused on targetplatform
			// Adding BuildDependencies also sets up many circular dependencies, because maps declare their external
			// actors as build dependencies and the external actors declare the map as a build or hard dependency.
			// Topological sort done at the end of the Cluster has poor performance when there are 100k+ circular dependencies.
			constexpr bool bAddBuildDependenciesToGraph = false;
			if (bAddBuildDependenciesToGraph)
			{
				Cluster.AssetRegistry.GetDependencies(PackageName, CookerLoadingDependencies, EDependencyCategory::Package,
					EDependencyQuery::Build);
			}
			// CookerLoadingPlatform does not cause SetInstigator so it does not modify bHardDependency
			AddPlatformDependencyRange(CookerLoadingDependencies, PlatformIndex, false /* bHardDependency */);
		}
		else
		{
			FFetchPlatformData& FetchPlatformData = FetchPlatforms[PlatformIndex];
			const ITargetPlatform* TargetPlatform = FetchPlatformData.Platform;

			AddPlatformDependencyRange(HardGameDependencies, PlatformIndex, true /* bHardDependency */);
			AddPlatformDependencyRange(SoftGameDependencies, PlatformIndex, false /* bHardDependency */);

			const FCookAttachments& PlatformAttachments = QueryPlatformData.CookAttachments;
			bool bFoundBuildDefinitions = false;
			if (IsCookAttachmentsValid(PackageName, PlatformAttachments))
			{
				ICookedPackageWriter* PackageWriter = FetchPlatformData.Writer;
				if (!Cluster.bFullBuild && Cluster.bHybridIterativeEnabled)
				{
					if (IsIterativeEnabled(PackageName))
					{
						if (PlatformIndex == FirstSessionPlatformIndex)
						{
							COOK_STAT(++DetailedCookStats::NumPackagesIterativelySkipped);
						}
						PackageData.SetPlatformCooked(TargetPlatform, ECookResult::Succeeded);
						PackageWriter->MarkPackagesUpToDate({ PackageName });
						// Declare the package to the EDLCookInfo verification so we don't warn about missing exports from it
						UE::SavePackageUtilities::EDLCookInfoAddIterativelySkippedPackage(PackageName);
					}
					AddPlatformDependencyRange(PlatformAttachments.BuildDependencies, PlatformIndex,
						true /* bHardDependency */);
					if (Cluster.bAllowSoftDependencies)
					{
						AddPlatformDependencyRange(PlatformAttachments.RuntimeOnlyDependencies, PlatformIndex,
							true /* bHardDependency */);
					}

					if (Cluster.bPreQueueBuildDefinitions)
					{
						bFoundBuildDefinitions = true;
						Cluster.BuildDefinitions.AddBuildDefinitionList(PackageName, TargetPlatform,
							PlatformAttachments.BuildDefinitionList);
					}
				}
			}
			if (Cluster.bPreQueueBuildDefinitions && !bFoundBuildDefinitions)
			{
				if (PlatformAgnosticQueryPlatformData.bActive &&
					IsCookAttachmentsValid(PackageName, PlatformAgnosticQueryPlatformData.CookAttachments))
				{
					Cluster.BuildDefinitions.AddBuildDefinitionList(PackageName, TargetPlatform,
						PlatformAgnosticQueryPlatformData.CookAttachments.BuildDefinitionList);
				}
			}
		}
		if (DiscoveredDependencies)
		{
			AddPlatformDependencyRange(*DiscoveredDependencies, PlatformIndex, true /* bHardDependency */);
		}
	}
	if (PlatformDependencyMap.IsEmpty())
	{
		return;
	}

	TArray<FPackageData*>* Edges = nullptr;
	for (TPair<FName, FScratchPlatformDependencyBits>& PlatformDependencyPair : PlatformDependencyMap)
	{
		FName DependencyName = PlatformDependencyPair.Key;
		TBitArray<>& HasPlatformByIndex = PlatformDependencyPair.Value.HasPlatformByIndex;
		bool bHardDependency = PlatformDependencyPair.Value.bHardDependency;

		// Process any CoreRedirects before checking whether the package exists
		FName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, DependencyName)).PackageName;
		DependencyName = Redirected;

		FVertexData& DependencyVertex = FindOrAddVertex(DependencyName);
		if (!DependencyVertex.PackageData)
		{
			continue;
		}
		FPackageData& DependencyPackageData(*DependencyVertex.PackageData);
		bool bAddToFrontier = false;

		for (int32 PlatformIndex = 0; PlatformIndex < LocalNumFetchPlatforms; ++PlatformIndex)
		{
			if (!HasPlatformByIndex[PlatformIndex])
			{
				continue;
			}
			FFetchPlatformData& FetchPlatformData = FetchPlatforms[PlatformIndex];
			const ITargetPlatform* TargetPlatform = FetchPlatformData.Platform;
			FPackagePlatformData& PlatformData = DependencyPackageData.FindOrAddPlatformData(TargetPlatform);

			if (PlatformIndex == CookerLoadingPlatformIndex)
			{
				if (!Edges)
				{
					Edges = &GraphEdges.FindOrAdd(&PackageData);
					Edges->Reset(PlatformDependencyMap.Num());
				}
				Edges->Add(&DependencyPackageData);
			}

			if (!PlatformData.IsReachable())
			{
				PlatformData.SetReachable(true);
				if (!DependencyPackageData.HasInstigator() && TargetPlatform != CookerLoadingPlatformKey)
				{
					EInstigator InstigatorType = bHardDependency ? EInstigator::HardDependency : EInstigator::SoftDependency;
					DependencyPackageData.SetInstigator(Cluster, FInstigator(InstigatorType, PackageName));
				}
			}
			if (!PlatformData.IsVisitedByCluster())
			{
				bAddToFrontier = true;
			}
		}
		if (bAddToFrontier)
		{
			AddToFrontier(DependencyVertex);
		}
	}
}

void FRequestCluster::FVertexQueryData::Reset()
{
	for (FQueryPlatformData& PlatformData : Platforms)
	{
		PlatformData.CookAttachments.Reset();
		PlatformData.bActive = false;
	}
}

FRequestCluster::FVertexData* FRequestCluster::FGraphSearch::AllocateVertex()
{
	return VertexAllocator.NewElement();
}

FRequestCluster::FVertexQueryData* FRequestCluster::FGraphSearch::AllocateQueryData()
{
	// VertexQueryAllocator uses DeferredDestruction, so this might be a resused Batch, but we don't need to Reset it
	// during allocation because Batches are Reset during Free.
	return VertexQueryAllocator.NewElement();
}

void FRequestCluster::FGraphSearch::FreeQueryData(FVertexQueryData* QueryData)
{
	QueryData->Reset();
	VertexQueryAllocator.Free(QueryData);
}

FRequestCluster::FVertexData&
FRequestCluster::FGraphSearch::FindOrAddVertex(FName PackageName)
{
	// Only called from PumpExploration thread
	FVertexData*& ExistingVertex = Vertices.FindOrAdd(PackageName);
	if (ExistingVertex)
	{
		return *ExistingVertex;
	}

	ExistingVertex = AllocateVertex();
	TStringBuilder<256> NameBuffer;
	PackageName.ToString(NameBuffer);
	ExistingVertex->PackageData = nullptr;
	if (!FPackageName::IsScriptPackage(NameBuffer))
	{
		ExistingVertex->PackageData = Cluster.COTFS.PackageDatas->TryAddPackageDataByPackageName(PackageName);
	}
	return *ExistingVertex;
}

FRequestCluster::FVertexData&
FRequestCluster::FGraphSearch::FindOrAddVertex(FName PackageName, FPackageData& PackageData)
{
	// Only called from PumpExploration thread
	FVertexData*& ExistingVertex = Vertices.FindOrAdd(PackageName);
	if (ExistingVertex)
	{
		check(ExistingVertex->PackageData == &PackageData);
		return *ExistingVertex;
	}

	ExistingVertex = AllocateVertex();
	ExistingVertex->PackageData = &PackageData;
	return *ExistingVertex;
}

void FRequestCluster::FGraphSearch::AddToFrontier(FVertexData& Vertex)
{
	if (Vertex.PackageData)
	{
		Cluster.PullIntoCluster(*Vertex.PackageData);
	}
	Frontier.Add(&Vertex);
}

void FRequestCluster::FGraphSearch::CreateAvailableBatches(bool bAllowIncompleteBatch)
{
	constexpr int32 BatchSize = 1000;
	if (PreAsyncQueue.IsEmpty() || (!bAllowIncompleteBatch && PreAsyncQueue.Num() < BatchSize))
	{
		return;
	}

	TArray<FQueryVertexBatch*> NewBatches;
	NewBatches.Reserve((PreAsyncQueue.Num() + BatchSize - 1) / BatchSize);
	{
		FScopeLock ScopeLock(&Lock);
		while (PreAsyncQueue.Num() >= BatchSize)
		{
			NewBatches.Add(CreateBatchOfPoppedVertices(BatchSize));
		}
		if (PreAsyncQueue.Num() > 0 && bAllowIncompleteBatch)
		{
			NewBatches.Add(CreateBatchOfPoppedVertices(PreAsyncQueue.Num()));
		}
	}
	for (FQueryVertexBatch* NewBatch : NewBatches)
	{
		NewBatch->Send();
	}
}

FRequestCluster::FQueryVertexBatch* FRequestCluster::FGraphSearch::AllocateBatch()
{
	// Called from inside this->Lock
	// BatchAllocator uses DeferredDestruction, so this might be a resused Batch, but we don't need to Reset it during
	// allocation because Batches are Reset during Free.
	return BatchAllocator.NewElement(*this);
}

void FRequestCluster::FGraphSearch::FreeBatch(FQueryVertexBatch* Batch)
{
	// Called from inside this->Lock
	Batch->Reset();
	BatchAllocator.Free(Batch);
}

FRequestCluster::FQueryVertexBatch* FRequestCluster::FGraphSearch::CreateBatchOfPoppedVertices(int32 BatchSize)
{
	// Called from inside this->Lock
	check(BatchSize <= PreAsyncQueue.Num());
	FQueryVertexBatch* BatchData = AllocateBatch();
	BatchData->Vertices.Reserve(BatchSize);
	for (int32 BatchIndex = 0; BatchIndex < BatchSize; ++BatchIndex)
	{
		FVertexData* Vertex = PreAsyncQueue.PopFrontValue();
		FVertexData*& ExistingVert = BatchData->Vertices.FindOrAdd(Vertex->QueryData->PackageName);
		check(!ExistingVert); // We should not have any duplicate names in PreAsyncQueue
		ExistingVert = Vertex;
	}
	AsyncQueueBatches.Add(BatchData);
	return BatchData;
}

void FRequestCluster::FGraphSearch::OnBatchCompleted(FQueryVertexBatch* Batch)
{
	FScopeLock ScopeLock(&Lock);
	AsyncQueueBatches.Remove(Batch);
	FreeBatch(Batch);
	AsyncResultsReadyEvent->Trigger();
}

void FRequestCluster::FGraphSearch::OnVertexCompleted()
{
	// The trigger occurs outside of the lock, and might get clobbered and incorrectly ignored by a call from the
	// consumer thread if the consumer tried to consume and found the vertices empty before our caller added a vertex
	// but then pauses and calls AsyncResultsReadyEvent->Reset after this AsyncResultsReadyEvent->Trigger.
	// This clobbering will not cause a deadlock, because eventually DestroyBatch will be called which triggers it
	// inside the lock. Doing the per-vertex trigger outside the lock is good for performance.
	AsyncResultsReadyEvent->Trigger();
}

FRequestCluster::FQueryVertexBatch::FQueryVertexBatch(FGraphSearch& InGraphSearch)
	: ThreadSafeOnlyVars(InGraphSearch)
{
	PlatformDatas.SetNum(InGraphSearch.FetchPlatforms.Num());
}

void FRequestCluster::FQueryVertexBatch::Reset()
{
	for (FPlatformData& PlatformData : PlatformDatas)
	{
		PlatformData.PackageNames.Reset();
	}
	Vertices.Reset();
}

void FRequestCluster::FQueryVertexBatch::Send()
{
	for (const TPair<FName, FVertexData*>& Pair : Vertices)
	{
		FVertexData* Vertex = Pair.Value;
		TArray<FQueryPlatformData>& QueryPlatforms = Vertex->QueryData->Platforms;
		bool bAtLeastOnePlatform = false;
		for (int32 PlatformIndex = 0; PlatformIndex < PlatformDatas.Num(); ++PlatformIndex)
		{
			if (QueryPlatforms[PlatformIndex].bActive)
			{
				PlatformDatas[PlatformIndex].PackageNames.Add(Pair.Key);
			}
			bAtLeastOnePlatform = true;
		}
		// We only check for the vertex's completion when the vertex receives a callback from the completion of a
		// platform. Therefore we do not support Vertices in the batch that have no platforms.
		check(bAtLeastOnePlatform);
	}
	PendingVertices.store(Vertices.Num(), std::memory_order_release);

	for (int32 PlatformIndex = 0; PlatformIndex < PlatformDatas.Num(); ++PlatformIndex)
	{
		FPlatformData& PlatformData = PlatformDatas[PlatformIndex];
		if (PlatformData.PackageNames.IsEmpty())
		{
			continue;
		}
		FFetchPlatformData& FetchPlatformData = ThreadSafeOnlyVars.FetchPlatforms[PlatformIndex];

		if (ThreadSafeOnlyVars.bCookAttachmentsEnabled // Only FetchCookAttachments if our cookmode supports it. Otherwise keep them all empty
			&& !FetchPlatformData.bIsCookerLoadingPlatform // The CookerLoadingPlatform has no stored CookAttachments; always use empty
			)
		{
			TUniqueFunction<void(FName PackageName, UE::TargetDomain::FCookAttachments&& Result)> Callback =
				[this, PlatformIndex](FName PackageName, UE::TargetDomain::FCookAttachments&& Attachments)
			{
				RecordCacheResults(PackageName, PlatformIndex, MoveTemp(Attachments));
			};
			UE::TargetDomain::FetchCookAttachments(PlatformData.PackageNames, FetchPlatformData.Platform,
				FetchPlatformData.Writer, MoveTemp(Callback));
		}
		else
		{
			// When we do not need to asynchronously fetch, we record empty cache results from an AsyncTask.
			// Using an AsyncTask keeps the threading flow similar to the FetchCookAttachments case
			AsyncTask(ENamedThreads::AnyThread,
				[this, PlatformIndex]()
			{
				FPlatformData& PlatformData = PlatformDatas[PlatformIndex];
				// Don't use a ranged-for, as we are not allowed to access this or this->PackageNames after the
				// last index, and ranged-for != at the end of the final loop iteration can read from PackageNames
				int32 NumPackageNames = PlatformData.PackageNames.Num();
				FName* PackageNamesData = PlatformData.PackageNames.GetData();
				for (int32 PackageNameIndex = 0; PackageNameIndex < NumPackageNames; ++PackageNameIndex)
				{
					FName PackageName = PackageNamesData[PackageNameIndex];
					UE::TargetDomain::FCookAttachments Attachments;
					RecordCacheResults(PackageName, PlatformIndex, MoveTemp(Attachments));
				}
			});
		}
	}
}

void FRequestCluster::FQueryVertexBatch::RecordCacheResults(FName PackageName, int32 PlatformIndex,
	UE::TargetDomain::FCookAttachments&& CookAttachments)
{
	FVertexData* Vertex = Vertices.FindChecked(PackageName);
	check(Vertex->QueryData);
	FVertexQueryData& QueryData = *Vertex->QueryData;
	QueryData.Platforms[PlatformIndex].CookAttachments = MoveTemp(CookAttachments);
	if (QueryData.PendingPlatforms.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		ThreadSafeOnlyVars.AsyncQueueResults.Enqueue(Vertex);
		bool bBatchComplete = PendingVertices.fetch_sub(1, std::memory_order_relaxed) == 1;
		if (!bBatchComplete)
		{
			ThreadSafeOnlyVars.OnVertexCompleted();
		}
		else
		{
			ThreadSafeOnlyVars.OnBatchCompleted(this);
			// *this is no longer accessible
		}
	}
}

TMap<FPackageData*, TArray<FPackageData*>>& FRequestCluster::FGraphSearch::GetGraphEdges()
{
	return GraphEdges;
}

void FRequestCluster::IsRequestCookable(const ITargetPlatform* Platform, FName PackageName, FPackageData& PackageData,
	ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable)
{
	return IsRequestCookable(Platform, PackageName, PackageData, PackageDatas, PackageTracker,
		DLCPath, bErrorOnEngineContentUse, bAllowUncookedAssetReferences,
		COTFS.bCanSkipEditorReferencedPackagesWhenCooking,
		OutReason, bOutCookable, bOutExplorable);
}

void FRequestCluster::IsRequestCookable(const ITargetPlatform* Platform, FName PackageName, FPackageData& PackageData,
	FPackageDatas& InPackageDatas, FPackageTracker& InPackageTracker,
	FStringView InDLCPath, bool bInErrorOnEngineContentUse, bool bInAllowUncookedAssetReferences,
	bool bCanSkipEditorReferencedPackagesWhenCooking,
	ESuppressCookReason& OutReason, bool& bOutCookable, bool& bOutExplorable)
{
	check(Platform != CookerLoadingPlatformKey); // IsRequestCookable should not be called for The CookerLoadingPlatform; it has different rules

	TStringBuilder<256> NameBuffer;
	// We need to reject packagenames from adding themselves or their transitive dependencies using all the same rules that
	// UCookOnTheFlyServer::ProcessRequest uses. Packages that are rejected from cook do not add their dependencies to the cook.
	PackageName.ToString(NameBuffer);
	if (FPackageName::IsScriptPackage(NameBuffer))
	{
		OutReason = ESuppressCookReason::ScriptPackage;
		bOutCookable = false;
		bOutExplorable = false;
		return;
	}

	FName FileName = PackageData.GetFileName();
	if (InPackageTracker.NeverCookPackageList.Contains(FileName))
	{
		if (INDEX_NONE != UE::String::FindFirst(NameBuffer, ULevel::GetExternalActorsFolderName(), ESearchCase::IgnoreCase))
		{
			// EXTERNALACTOR_TODO: Add a separate category for ExternalActors rather than putting them in
			// NeverCookPackageList and checking naming convention here.
			OutReason = ESuppressCookReason::NeverCook;
			bOutCookable = false;
			bOutExplorable = true;
			if (bCanSkipEditorReferencedPackagesWhenCooking)
			{
				bOutExplorable = true;
			}
			else
			{
				// ONLYEDITORONLY_TODO
				// Workaround to avoid changing behavior from legacy graph search. In legacy graph search ExternalActors
				// were uncookable and unexplorable, and so packages referenced through externalactors referenced by e.g. GameFeatureData
				// would not be cooked.
				bOutExplorable = false;
			}
		}
		else
		{
			UE_LOG(LogCook, Verbose, TEXT("Package %s is referenced but is in the never cook package list, discarding request"), *NameBuffer);
			OutReason = ESuppressCookReason::NeverCook;
			bOutCookable = false;
			bOutExplorable = false;
		}
		return;
	}

	if (bInErrorOnEngineContentUse && !InDLCPath.IsEmpty())
	{
		FileName.ToString(NameBuffer);
		if (!FStringView(NameBuffer).StartsWith(InDLCPath))
		{
			if (!PackageData.HasCookedPlatform(Platform, true /* bIncludeFailed */))
			{
				// AllowUncookedAssetReferences should only be used when the DLC plugin to cook is going to be mounted where uncooked packages are available.
				// This will allow a DLC plugin to be recooked continually and mounted in an uncooked editor which is useful for CI.
				if (!bInAllowUncookedAssetReferences)
				{
					UE_LOG(LogCook, Error, TEXT("Uncooked Engine or Game content %s is being referenced by DLC!"), *NameBuffer);
				}
			}
			OutReason = ESuppressCookReason::NotInCurrentPlugin;
			bOutCookable = false;
			bOutExplorable = false;
			return;
		}
	}

	OutReason = ESuppressCookReason::NotSuppressed;
	bOutCookable = true;
	bOutExplorable = true;
}

TConstArrayView<FName> FRequestCluster::GetLocalizationReferences(FName PackageName, UCookOnTheFlyServer& InCOTFS)
{
	if (!FPackageName::IsLocalizedPackage(WriteToString<256>(PackageName)))
	{
		TArray<FName>* Result = InCOTFS.CookByTheBookOptions->SourceToLocalizedPackageVariants.Find(PackageName);
		if (Result)
		{
			return TConstArrayView<FName>(*Result);
		}
	}
	return TConstArrayView<FName>();
}

} // namespace UE::Cook
