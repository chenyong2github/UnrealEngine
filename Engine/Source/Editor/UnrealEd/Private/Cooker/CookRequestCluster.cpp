// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookRequestCluster.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/CookPlatformManager.h"
#include "Cooker/CookProfiling.h"
#include "Cooker/CookRequests.h"
#include "Cooker/CookTypes.h"
#include "Cooker/PackageNameCache.h"
#include "Cooker/PackageTracker.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/RedirectCollector.h"
#include "Misc/StringBuilder.h"

namespace UE::Cook
{

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& COTFS, TArray<const ITargetPlatform*>&& InPlatforms)
	: Platforms(MoveTemp(InPlatforms))
	, PackageDatas(*COTFS.PackageDatas)
	, AssetRegistry(*IAssetRegistry::Get())
	, PackageNameCache(COTFS.GetPackageNameCache())
	, PackageTracker(*COTFS.PackageTracker)
	, BuildDefinitions(*COTFS.BuildDefinitions)
{
}

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& COTFS, TConstArrayView<const ITargetPlatform*> InPlatforms)
	: Platforms(InPlatforms)
	, PackageDatas(*COTFS.PackageDatas)
	, AssetRegistry(*IAssetRegistry::Get())
	, PackageNameCache(COTFS.GetPackageNameCache())
	, PackageTracker(*COTFS.PackageTracker)
	, BuildDefinitions(*COTFS.BuildDefinitions)
{
}

void FRequestCluster::AddClusters(UCookOnTheFlyServer& COTFS, TArray<FFilePlatformRequest>&& InRequests, bool bRequestsAreUrgent,
	TRingBuffer<FRequestCluster>& OutClusters)
{
	if (InRequests.Num() == 0)
	{
		return;
	}

	TArray<FRequestCluster, TInlineAllocator<1>> AddedClusters;
	auto FindOrAddClusterForPlatforms = [&AddedClusters, &COTFS](TArray<const ITargetPlatform*>&& InPlatforms)
	{
		for (FRequestCluster& Existing : AddedClusters)
		{
			if (Existing.GetPlatforms() == InPlatforms)
			{
				InPlatforms.Reset();
				return &Existing;
			}
		}
		return &AddedClusters.Emplace_GetRef(COTFS, MoveTemp(InPlatforms));
	};

	UE::Cook::FRequestCluster* MRUCluster = FindOrAddClusterForPlatforms(MoveTemp(InRequests[0].GetPlatforms()));
	// The usual case is all platforms are the same, so reserve the first Cluster's size assuming it will get all requests
	MRUCluster->InRequests.Reserve(InRequests.Num());
	// Add the first Request to it. Since we've already taken away the Platforms from the first request, we have to handle it specially.
	MRUCluster->InRequests.Add(FFileNameRequest(MoveTemp(InRequests[0]), bRequestsAreUrgent));

	for (FFilePlatformRequest& Request : TArrayView<FFilePlatformRequest>(InRequests).Slice(1, InRequests.Num() - 1))
	{
		if (Request.GetPlatforms() != MRUCluster->GetPlatforms())
		{
			// MRUCluster points to data inside AddedClusters, so we have to recalculate MRUCluster whenever we add
			MRUCluster = FindOrAddClusterForPlatforms(MoveTemp(Request.GetPlatforms()));
		}
		else
		{
			Request.GetPlatforms().Reset();
		}

		MRUCluster->InRequests.Add(FFileNameRequest(MoveTemp(Request), bRequestsAreUrgent));
	}

	for (UE::Cook::FRequestCluster& AddedCluster : AddedClusters)
	{
		AddedCluster.Initialize(COTFS);
		OutClusters.Add(MoveTemp(AddedCluster));
	}
}

void FRequestCluster::AddClusters(UCookOnTheFlyServer& COTFS, FPackageDataSet& UnclusteredRequests, TRingBuffer<FRequestCluster>& OutClusters,
	FRequestQueue& QueueForReadyRequests)
{
	if (UnclusteredRequests.Num() == 0)
	{
		return;
	}

	TArray<FRequestCluster, TInlineAllocator<1>> AddedClusters;
	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> RequestedPlatforms;
	auto FindOrAddClusterForPlatforms = [&AddedClusters, &COTFS, &RequestedPlatforms, &UnclusteredRequests]()
	{
		if (AddedClusters.Num() == 0)
		{
			FRequestCluster& Cluster = AddedClusters.Emplace_GetRef(COTFS, RequestedPlatforms);
			// The usual case is all platforms are the same, so reserve the first Cluster's size assuming it will get all requests
			Cluster.Requests.Reserve(UnclusteredRequests.Num());
			return &Cluster;
		}
		for (FRequestCluster& Existing : AddedClusters)
		{
			if (Existing.GetPlatforms() == RequestedPlatforms)
			{
				return &Existing;
			}
		}
		return &AddedClusters.Emplace_GetRef(COTFS, RequestedPlatforms);
	};

	bool bErrorOnEngineContentUse = false;
	UCookOnTheFlyServer::FCookByTheBookOptions* Options = COTFS.CookByTheBookOptions;
	FString DLCPath;
	if (Options)
	{
		bErrorOnEngineContentUse = Options->bErrorOnEngineContentUse;
		if (bErrorOnEngineContentUse)
		{
			DLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
			FPaths::MakeStandardFilename(DLCPath);
		}
	}

	UE::Cook::FRequestCluster* MRUCluster = nullptr;
	for (FPackageData* PackageData : UnclusteredRequests)
	{
		if (PackageData->AreAllRequestedPlatformsExplored())
		{
			QueueForReadyRequests.AddReadyRequest(PackageData);
			continue;
		}

		// For non-cookable packages that we skipped in an earlier cluster but loaded because
		// they are hard dependencies, avoid the work of creating a cluster just for them,
		// by checking non-cookable and sending the package to idle instead of adding to
		// a cluster
		FName FileName = PackageData->GetFileName();
		PackageData->GetRequestedPlatforms(RequestedPlatforms);
		if (!IsRequestCookable(PackageData->GetPackageName(), FileName, PackageData,
			COTFS.GetPackageNameCache(), *COTFS.PackageDatas, *COTFS.PackageTracker,
			DLCPath, bErrorOnEngineContentUse, RequestedPlatforms))
		{
			PackageData->SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
			continue;
		}

		if (!MRUCluster || RequestedPlatforms != MRUCluster->GetPlatforms())
		{
			// MRUCluster points to data inside AddedClusters, so we have to recalculate MRUCluster whenever we add
			MRUCluster = FindOrAddClusterForPlatforms();
		}
		CA_ASSUME(MRUCluster);

		MRUCluster->Requests.Add(PackageData);
	}

	for (UE::Cook::FRequestCluster& AddedCluster : AddedClusters)
	{
		AddedCluster.Initialize(COTFS);
		OutClusters.Add(MoveTemp(AddedCluster));
	}
}

FRequestCluster::FFileNameRequest::FFileNameRequest(FFilePlatformRequest&& FileRequest, bool bInUrgent)
	: FileName(FileRequest.GetFilename())
	, CompletionCallback(MoveTemp(FileRequest.GetCompletionCallback()))
	, bUrgent(bInUrgent)
{
}

void FRequestCluster::Initialize(UCookOnTheFlyServer& COTFS)
{
	UCookOnTheFlyServer::FCookByTheBookOptions* Options = COTFS.CookByTheBookOptions;
	if (Options)
	{
		bAllowHardDependencies = !Options->bSkipHardReferences;
		bAllowSoftDependencies = !Options->bSkipSoftReferences;
		bErrorOnEngineContentUse = Options->bErrorOnEngineContentUse;
	}
	else
	{
		// Do not queue soft-dependencies during CookOnTheFly; wait for them to be requested
		// TODO: Report soft dependencies separately, and mark them as normal priority,
		// and mark all hard dependencies as high priority in cook on the fly.
		bAllowSoftDependencies = false;
	}
	bHybridIterativeEnabled = COTFS.bHybridIterativeEnabled;
	bPreexploreDependenciesEnabled = COTFS.bPreexploreDependenciesEnabled;
	if (bErrorOnEngineContentUse)
	{
		DLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(DLCPath);
	}
	GConfig->GetBool(TEXT("CookSettings"), TEXT("PreQueueBuildDefinitions"), bPreQueueBuildDefinitions, GEditorIni);


	PackageWriters.Reserve(Platforms.Num());
	bFullBuild = false;
	bool bFirst = true;
	for (const ITargetPlatform* TargetPlatform : Platforms)
	{
		PackageWriters.Add(&COTFS.FindOrCreatePackageWriter(TargetPlatform));
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

	bool bRemoveNulls = false;
	for (FPackageData*& PackageData : Requests)
	{
		bool bAlreadyExists = false;
		OwnedPackageDatas.Add(PackageData, &bAlreadyExists);
		if (bAlreadyExists)
		{
			bRemoveNulls = true;
			PackageData = nullptr;
		}
	}
	if (bRemoveNulls)
	{
		Requests.Remove(nullptr);
	}
}

void FRequestCluster::Process(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	bOutComplete = true;

	FetchPackageNames(CookerTimer, bOutComplete);
	if (!bOutComplete)
	{
		return;
	}
	FetchDependencies(CookerTimer, bOutComplete);
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
	int32 InRequestsNum = InRequests.Num();
	if (this->NextRequest == 0)
	{
		Requests.Reserve(Requests.Num() + InRequestsNum);
	}
	for (;NextRequest < InRequestsNum; ++NextRequest)
	{
		if (NextRequest % TimerCheckPeriod == 0 && CookerTimer.IsTimeUp())
		{
			break;
		}

		FFileNameRequest& Request = InRequests[NextRequest];
		FName OriginalName = Request.FileName;
#if DEBUG_COOKONTHEFLY
		UE_LOG(LogCook, Display, TEXT("Processing request for package %s"), *OriginalName.ToString());
#endif
		const FName* PackageName = PackageNameCache.GetCachedPackageNameFromStandardFileName(OriginalName,
			/* bExactMatchRequired */ false, &Request.FileName);
		if (!PackageName)
		{
			LogCookerMessage(FString::Printf(TEXT("Could not find package at file %s!"),
				*OriginalName.ToString()), EMessageSeverity::Error);
			UE_LOG(LogCook, Error, TEXT("Could not find package at file %s!"), *OriginalName.ToString());
			UE::Cook::FCompletionCallback CompletionCallback(MoveTemp(Request.CompletionCallback));
			if (CompletionCallback)
			{
				CompletionCallback(nullptr);
			}
		}
		else
		{
			FPackageData& PackageData = PackageDatas.FindOrAddPackageData(*PackageName, Request.FileName);
			if (TryTakeOwnership(PackageData, Request.bUrgent, MoveTemp(Request.CompletionCallback)))
			{
				bool bAlreadyExists;
				OwnedPackageDatas.Add(&PackageData, &bAlreadyExists);
				if (!bAlreadyExists)
				{
					Requests.Add(&PackageData);
				}
			}
		}
	}
	if (NextRequest < InRequestsNum)
	{
		bOutComplete = false;
		return;
	}

	InRequests.Empty();
	NextRequest = 0;
	bPackageNamesComplete = true;
}

bool FRequestCluster::TryTakeOwnership(FPackageData& PackageData, bool bUrgent, UE::Cook::FCompletionCallback && CompletionCallback)
{
	if (!PackageData.IsInProgress())
	{
		check(GetPlatforms().Num() != 0); // This is required for SetRequestData
		if (PackageData.HasAllExploredPlatforms(GetPlatforms()))
		{
			// Leave it in idle - it's already been processed by a cluster
			if (CompletionCallback)
			{
				CompletionCallback(&PackageData);
			}
			return false;
		}
		else
		{
			PackageData.SetRequestData(GetPlatforms(), bUrgent, MoveTemp(CompletionCallback));
			PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove);
			return true;
		}
	}
	else
	{
		if (PackageData.HasAllExploredPlatforms(GetPlatforms()))
		{
			// Leave it where it is - it's already been processed by a cluster - but update with our request data
			// This might demote it back to Request, and add it to Normal or Urgent request, but that will not
			// impact this RequestCluster
			PackageData.UpdateRequestData(GetPlatforms(), bUrgent, MoveTemp(CompletionCallback));
			return false;
		}
		else
		{
			if (!OwnedPackageDatas.Contains(&PackageData))
			{
				// Steal it from wherever it is and add it to this cluster
				// This might steal it from another RequestCluster or from the UnclusteredRequests if it's in request
				// Doing that steal is wasteful but okay; one of the RequestClusters will win it and keep it
				PackageData.SendToState(EPackageState::Request, ESendFlags::QueueRemove);
				PackageData.UpdateRequestData(GetPlatforms(), bUrgent, MoveTemp(CompletionCallback), false /* bAllowUpdateUrgency */);
			}
			return true;
		}
	}
}

void FRequestCluster::FetchDependencies(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bDependenciesComplete)
	{
		return;
	}

	if (!bAllowHardDependencies)
	{
		// FetchDependencies is responsible for marking all InitialRequests as explored. If we're skipping
		// the dependencies search, just handle that responsibility and return.
		for (FPackageData* PackageData : Requests)
		{
			bool bAlreadyCooked;
			VisitPackageData(PackageData, nullptr /* OutDependencies */, bAlreadyCooked);
		}
		bDependenciesComplete = true;
		return;
	}

	// Do a depth first search over each initial Request. Use DFS rather than breadth first so that we can
	// (a) Cheaply sort the transitive set of requests from root to leaf
	// (b) Coarsely visit all the references of an Asset, which are likely related, before moving on to a new Asset
	//
	// For each new initial request, move all of the new Assets found in its transitive graph to earlier than the previous
	// requests, so that they will occur earlier than their dependencies from the previous request
	//
	// For the output order, we will reverse our visit order (which is in root to leaf order) so that the cooker loads
	// leaf packages first.
	// 
	// When initial request assets are unrelated, we want to load those assets in the original order. Since we are
	// (a) moving later requests before earlier requests but then (b) reversing the list, the reversals cancel out
	// and we should iterate the initial requests from front to back.
	if (NextRequest == 0)
	{
		check(Requests.Num() == OwnedPackageDatas.Num());
	}
	int32 TimerCounter = 0;
	constexpr int32 TimerCheckPeriod = 10; // Do not incur the cost of checking the timer on every package
	for (;;)
	{
		while (!IsStackEmpty())
		{
			if (TimerCounter >= TimerCheckPeriod)
			{
				if (CookerTimer.IsTimeUp())
				{
					bOutComplete = false;
					return;
				}
				TimerCounter = 0;
			}

			FStackData& CurrentTop(TopStack());
			bool bPushedStack = false;
			while (CurrentTop.NextDependency < CurrentTop.Dependencies.Num())
			{
				AddVertex(CurrentTop.Dependencies[CurrentTop.NextDependency++], nullptr, bPushedStack, TimerCounter);
				if (bPushedStack)
				{
					// CurrentTop is now invalid
					break;
				}
			}
			if (bPushedStack)
			{
				continue;
			}
			check(CurrentTop.NextDependency == CurrentTop.Dependencies.Num());
			PopStack();
		}

		TransitiveRequests.Reserve(TransitiveRequests.Num() + Segment.Num());
		while (!Segment.IsEmpty())
		{
			// Pop Segment from back to front so that when we PushFront onto TransitiveRequests they will maintain their same relative order
			TransitiveRequests.AddFront(Segment.PopValue());
		}

		while (NextRequest < Requests.Num())
		{
			FPackageData* PackageData = Requests[NextRequest++];
			bool bPushedStack = false;
			FName PackageName = PackageData->GetPackageName();
			EVisitStatus& Status = AddVertex(PackageName, PackageData, bPushedStack, TimerCounter);
			if (bPushedStack)
			{
				check(Status == EVisitStatus::Visited && !IsStackEmpty());
				break;
			}
			else
			{
				if (Status != EVisitStatus::Visited)
				{
					// If an initial request was found from transitive dependencies of another initial request, and was not
					// cookable, it was skipped. We need to reprocess it with the information that it is an initial vertex.
					check(Status == EVisitStatus::Skipped);
					Visited.Remove(PackageData->GetPackageName());
					Status = AddVertex(PackageName, PackageData, bPushedStack, TimerCounter);
					check(Status == EVisitStatus::Visited);
				}
				while (!Segment.IsEmpty())
				{
					// Pop Segment from back to front so that when we PushFront onto TransitiveRequests they will maintain their same relative order
					TransitiveRequests.AddFront(Segment.PopValue());
				}
			}
		}
		if (IsStackEmpty())
		{
			check(NextRequest == Requests.Num());
			break;
		}
	}
	check(Segment.IsEmpty());

	// Reverse the TransitiveRequests array to put it in LeafToRoot order, and push it into the output Requests
	COOK_STAT(DetailedCookStats::NumPreloadedDependencies += FMath::Max(0,TransitiveRequests.Num() - Requests.Num()));
	Requests.Reset(TransitiveRequests.Num());
	for (int32 Index = TransitiveRequests.Num() - 1; Index >= 0; --Index)
	{
		Requests.Add(TransitiveRequests[Index]);
	}

	TransitiveRequests.Empty();
	StackStorage.Empty();
	StackNum = 0;
	Segment.Empty();
	Visited.Empty();
	NextRequest = 0;
	bDependenciesComplete = true;
}

void FRequestCluster::StartAsync(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	using namespace UE::EditorDomain;

	if (bStartAsyncComplete)
	{
		return;
	}

	if (FEditorDomain::Get())
	{
		// Disable BatchDownload until cache storage implements fetch-head requests in response to a Get with SkipData
		bool bBatchDownloadEnabled = true;
		GConfig->GetBool(TEXT("EditorDomain"), TEXT("BatchDownloadEnabled"), bBatchDownloadEnabled, GEditorIni);
		if (bBatchDownloadEnabled)
		{
			// If the EditorDomain is active, then batch-download all packages to cook from remote cache into local
			TArray<UE::DerivedData::FCacheKey> CacheKeys;
			FString ErrorMessage;
			CacheKeys.Reserve(Requests.Num());
			for (FPackageData* PackageData : Requests)
			{
				FPackageDigest PackageDigest;
				EDomainUse EditorDomainUse;
				EPackageDigestResult Result = GetPackageDigest(AssetRegistry, PackageData->GetPackageName(),
					PackageDigest, EditorDomainUse, ErrorMessage);
				if (Result == EPackageDigestResult::Success && EnumHasAnyFlags(EditorDomainUse, EDomainUse::LoadEnabled))
				{
					CacheKeys.Add(GetEditorDomainPackageKey(PackageDigest));
				}
			}

			if (CacheKeys.Num() > 0)
			{
				UE::DerivedData::ICache& Cache = UE::DerivedData::GetCache();
				UE::DerivedData::ECachePolicy CachePolicy = UE::DerivedData::ECachePolicy::Default | UE::DerivedData::ECachePolicy::SkipData;
				UE::DerivedData::FRequestOwner Owner(UE::DerivedData::EPriority::Highest);
				Owner.KeepAlive();
				Cache.Get(CacheKeys, TEXT("RequestClusterEditorDomainDownload"_SV), CachePolicy, Owner,
					[](UE::DerivedData::FCacheGetCompleteParams&& Params) {});
			}
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

	Requests.Remove(PackageData);
	TransitiveRequests.Remove(PackageData);
	Segment.Remove(PackageData);
}

bool FRequestCluster::Contains(FPackageData* PackageData) const
{
	return OwnedPackageDatas.Contains(PackageData);
}

void FRequestCluster::ClearAndDetachOwnedPackageDatas(TArray<FPackageData*>& OutRequestsToLoad, TArray<FPackageData*>& OutRequestsToDemote)
{
	if (bStartAsyncComplete)
	{
		check(TransitiveRequests.Num() == 0 && Segment.Num() == 0);
		OutRequestsToLoad = MoveTemp(Requests);
		OutRequestsToDemote = MoveTemp(RequestsToDemote);
		check(OutRequestsToLoad.Num() + OutRequestsToDemote.Num() == OwnedPackageDatas.Num())
	}
	else
	{
		OutRequestsToLoad = OwnedPackageDatas.Array();
		OutRequestsToDemote.Reset();
	}
	InRequests.Empty();
	Requests.Empty();
	RequestsToDemote.Empty();
	OwnedPackageDatas.Empty();
	TransitiveRequests.Empty();
	Segment.Empty();
	Visited.Empty();
	StackStorage.Empty();
	StackNum = 0;
	NextRequest = 0;
}

FRequestCluster::EVisitStatus& FRequestCluster::AddVertex(FName PackageName, FPackageData* PackageData,
	bool& bOutPushedStack, int32& TimerCounter)
{
	EVisitStatus& Status = Visited.FindOrAdd(PackageName, EVisitStatus::New);
	if (Status != EVisitStatus::New)
	{
		bOutPushedStack = false;
		return Status;
	}
	++TimerCounter;

	// If it was a direct request we already own it and we can't skip exploring it, but we still check whether
	// it is cookable to see if it should add dependencies and move to load or ignore dependencies and return to idle.
	bool bInitialRequest = false;
	FName FileName = NAME_None;
	if (PackageData)
	{
		bInitialRequest = true;
		FileName = PackageData->GetFileName();
	}
	bool bCookable = IsRequestCookable(PackageName, FileName, PackageData);
	// If not an intial request, take ownership of the packagedata if cookable and available
	if (!bInitialRequest)
	{
		if (!bCookable)
		{
			Status = EVisitStatus::Skipped;
			bOutPushedStack = false;
			return Status;
		}
		if (!PackageData) // IsRequestCookable may have looked up the PackageData
		{
			PackageData = &PackageDatas.FindOrAddPackageData(PackageName, FileName);
		}
		if (!TryTakeOwnership(*PackageData, false /* bUrgent */, FCompletionCallback()))
		{
			Status = EVisitStatus::Skipped;
			bOutPushedStack = false;
			return Status;
		}
		// Note it may already be in OwnedPackageDatas because it might be present later in the Requests array.
		// That's okay, we still need to add it to Segment now.
		OwnedPackageDatas.Add(PackageData);
	}

	bool bExploreDependencies = bCookable;
	if (bCookable && !bPreexploreDependenciesEnabled)
	{
		// TODO: Editor-only packages may be loaded at editor startup, which makes them skip the request state
		// and go straight to load and save, where they are culled and returned to idle.
		// If we add dependencies from these startup packages, in some projects we add transitive dependency
		// packages that are not otherwise cooked. To prevent breaking projects we are temporarily skipping the
		// dependency search from these packages.
		bool bInProgress = false;
		if (FindPackage(nullptr, *PackageName.ToString()))
		{
			bExploreDependencies = false;
		}
	}
	FStackData* StackData = nullptr;
	bool bAlreadyCooked;
	if (bExploreDependencies)
	{
		bOutPushedStack = true;
		StackData = &PushStack(PackageName);
	}
	VisitPackageData(PackageData, StackData ? &StackData->Dependencies : nullptr, bAlreadyCooked);

	if (bCookable && !bAlreadyCooked)
	{
		Segment.Add(PackageData);
	}
	else
	{
		RequestsToDemote.Add(PackageData);
	}
	Status = EVisitStatus::Visited;
	bOutPushedStack = StackData != nullptr;
	return Status;
}

bool FRequestCluster::IsStackEmpty() const
{
	return StackNum == 0;
};

FRequestCluster::FStackData& FRequestCluster::TopStack()
{
	return StackStorage[StackNum - 1];
};

FRequestCluster::FStackData& FRequestCluster::PushStack(FName PackageName)
{
	if (StackNum == StackStorage.Num())
	{
		StackStorage.Emplace();
	}
	FStackData& Top = StackStorage[StackNum++];
	Top.Reset(PackageName);
	return Top;
};

void FRequestCluster::PopStack()
{
	--StackNum;
};

void FRequestCluster::VisitPackageData(FPackageData* PackageData, TArray<FName>* OutDependencies, bool& bOutAlreadyCooked)
{
	using namespace UE::AssetRegistry;

	if (OutDependencies)
	{
		OutDependencies->Reset();
		FName PackageName = PackageData->GetPackageName();

		// TODO EditorOnly References: We only fetch Game dependencies, because the cooker explicitly loads all of
		// the dependencies that we report. And if we explicitly load an EditorOnly dependency, that causes
		// StaticLoadObjectInternal to SetLoadedByEditorPropertiesOnly(false), which then treats the editor-only package
		// as needed in-game.
		EDependencyQuery DependencyQuery = EDependencyQuery::Game;
		// We always skip assetregistry soft dependencies if the cook commandline is set to skip soft references.
		// We also need to skip them if the project has problems with editor-only robustness and has turned
		// ExploreDependencies off
		if (!bAllowSoftDependencies || !bPreexploreDependenciesEnabled)
		{
			DependencyQuery |= EDependencyQuery::Hard;
			AssetRegistry.GetDependencies(PackageName, *OutDependencies, EDependencyCategory::Package,
				DependencyQuery);
		}
		else
		{
			// Even if we're following soft references in general, we need to check with the SoftObjectPath registry
			// for any startup packages that marked their softobjectpaths as excluded, and not follow those
			TSet<FName>& SkippedPackages(this->NameSetScratch);
			if (GRedirectCollector.RemoveAndCopySoftObjectPathExclusions(PackageName, SkippedPackages))
			{
				// Get hard dependencies separately; always add them
				AssetRegistry.GetDependencies(PackageName, *OutDependencies, EDependencyCategory::Package,
					DependencyQuery | EDependencyQuery::Hard);
				// Get the soft dependencies, and add them only if they're not in SkippedPackages
				TArray<FName> SoftDependencies;
				AssetRegistry.GetDependencies(PackageName, SoftDependencies, EDependencyCategory::Package,
					DependencyQuery | EDependencyQuery::Soft);
				for (FName SoftDependency : SoftDependencies)
				{
					if (!SkippedPackages.Contains(SoftDependency))
					{
						OutDependencies->Add(SoftDependency);
					}
				}
			}
			else
			{
				// No skipped soft object paths from this package; add all dependencies, hard or soft
				AssetRegistry.GetDependencies(PackageName, *OutDependencies, EDependencyCategory::Package,
					DependencyQuery);
			}
		}

		bool bIncrementIterativeCounter = false;
		bool bFoundBuildDefinitions = false;
		for (int32 PlatIndex = 0; PlatIndex < Platforms.Num(); ++PlatIndex)
		{
			const ITargetPlatform* TargetPlatform = Platforms[PlatIndex];
			ICookedPackageWriter* PackageWriter = PackageWriters[PlatIndex];
			OplogDataScratch.Reset();

			if (!bFullBuild && bHybridIterativeEnabled)
			{
				if (UE::TargetDomain::TryFetchCookAttachments(PackageWriter, PackageName,
					TargetPlatform, OplogDataScratch, nullptr /* OutErrorMessage */))
				{
					if (UE::TargetDomain::IsIterativeEnabled(PackageName))
					{
						bIncrementIterativeCounter = true;
						PackageData->SetPlatformCooked(TargetPlatform, true);
						PackageWriter->MarkPackagesUpToDate({ PackageName });
					}
					OutDependencies->Append(OplogDataScratch.BuildDependencies);
					if (bAllowSoftDependencies)
					{
						OutDependencies->Append(OplogDataScratch.RuntimeOnlyDependencies);
					}

					if (bPreQueueBuildDefinitions)
					{
						bFoundBuildDefinitions = true;
						BuildDefinitions.AddBuildDefinitionList(PackageName, TargetPlatform,
							OplogDataScratch.BuildDefinitionList);
					}
				}
			}
		}
		if (bPreQueueBuildDefinitions && !bFoundBuildDefinitions)
		{
			OplogDataScratch.Reset();
			if (UE::TargetDomain::TryFetchCookAttachments(nullptr, PackageName, nullptr, OplogDataScratch, nullptr))
			{
				BuildDefinitions.AddBuildDefinitionList(PackageName, nullptr, OplogDataScratch.BuildDefinitionList);
			}
		}
		if (bIncrementIterativeCounter)
		{
			COOK_STAT(++DetailedCookStats::NumPackagesIterativelySkipped);
		}

		// Sort the list of BuildDependencies to check for uniqueness and make them deterministic
		Algo::Sort(*OutDependencies, FNameLexicalLess());
		OutDependencies->SetNum(Algo::Unique(*OutDependencies));
	}

	for (const ITargetPlatform* TargetPlatform : Platforms)
	{
		PackageData->FindOrAddPlatformData(TargetPlatform).bExplored = true;
	}
	bOutAlreadyCooked = PackageData->AreAllRequestedPlatformsCooked(true /* bAllowFailedCooks */);
}

bool FRequestCluster::IsRequestCookable(FName PackageName, FName& InOutFileName, FPackageData*& PackageData)
{
	return IsRequestCookable(PackageName, InOutFileName, PackageData, PackageNameCache, PackageDatas, PackageTracker,
		DLCPath, bErrorOnEngineContentUse, GetPlatforms());
}

bool FRequestCluster::IsRequestCookable(FName PackageName, FName& InOutFileName, FPackageData*& PackageData,
	const FPackageNameCache& InPackageNameCache, FPackageDatas& InPackageDatas, FPackageTracker& InPackageTracker,
	FStringView InDLCPath, bool bInErrorOnEngineContentUse, TConstArrayView<const ITargetPlatform*> RequestPlatforms)
{
	TStringBuilder<256> NameBuffer;
	// We need to reject packagenames from adding themselves or their transitive dependencies using all the same rules that
	// UCookOnTheFlyServer::ProcessRequest uses. Packages that are rejected from cook do not add their dependencies to the cook.
	PackageName.ToString(NameBuffer);
	if (FPackageName::IsScriptPackage(NameBuffer))
	{
		return false;
	}

	if (InOutFileName.IsNone())
	{
		InOutFileName = InPackageNameCache.GetCachedStandardFileName(PackageName);
		if (InOutFileName.IsNone())
		{
			// Package does not exist on disk
			return false;
		}
	}

	if (InPackageTracker.NeverCookPackageList.Contains(InOutFileName))
	{
		UE_LOG(LogCook, Verbose, TEXT("Package %s is referenced but is in the never cook package list, discarding request"), *NameBuffer);
		return false;
	}

	if (bInErrorOnEngineContentUse && !InDLCPath.IsEmpty())
	{
		InOutFileName.ToString(NameBuffer);
		if (!FStringView(NameBuffer).StartsWith(InDLCPath))
		{
			if (!PackageData)
			{
				PackageData = &InPackageDatas.FindOrAddPackageData(PackageName, InOutFileName);
			}
			if (!PackageData->HasAllCookedPlatforms(RequestPlatforms, true /* bIncludeFailed */))
			{
				UE_LOG(LogCook, Error, TEXT("Uncooked Engine or Game content %s is being referenced by DLC!"), *NameBuffer);
			}
			return false;
		}
	}

	return true;
}

} // namespace UE::Cook
