// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/CookRequestCluster.h"

#include "Algo/Unique.h"
#include "Algo/Sort.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/CookProfiling.h"
#include "Cooker/CookRequests.h"
#include "Cooker/PackageNameCache.h"
#include "Cooker/PackageTracker.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "Misc/StringBuilder.h"
#include "TargetDomain/TargetDomainUtils.h"

namespace UE::Cook
{

FRequestCluster::FRequestCluster(UCookOnTheFlyServer& COTFS, TArray<const ITargetPlatform*>&& InPlatforms)
	: Platforms(MoveTemp(InPlatforms))
	, PackageDatas(*COTFS.PackageDatas)
	, AssetRegistry(*IAssetRegistry::Get())
	, PackageNameCache(COTFS.GetPackageNameCache())
	, PackageTracker(*COTFS.PackageTracker)
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
	auto FindOrAddClusterForPlatforms = [&AddedClusters, &COTFS](TArray<const ITargetPlatform*>&& Platforms)
	{
		for (FRequestCluster& Existing : AddedClusters)
		{
			if (Existing.GetPlatforms() == Platforms)
			{
				Platforms.Reset();
				return &Existing;
			}
		}
		return &AddedClusters.Emplace_GetRef(COTFS, MoveTemp(Platforms));
	};

	UE::Cook::FRequestCluster* MRUCluster = FindOrAddClusterForPlatforms(MoveTemp(InRequests[0].GetPlatforms()));
	// The usual case is all platforms are the same, so reserve the first Cluster's size assuming it will get all requests
	MRUCluster->Requests.Reserve(InRequests.Num());
	// Add the first Request to it. Since we've already taken away the Platforms from the first request, we have to handle it specially.
	MRUCluster->Requests.Add(FRequest(MoveTemp(InRequests[0]), bRequestsAreUrgent));

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

		MRUCluster->Requests.Add(FRequest(MoveTemp(Request), bRequestsAreUrgent));
	}

	for (UE::Cook::FRequestCluster& AddedCluster : AddedClusters)
	{
		AddedCluster.Initialize(COTFS);
		OutClusters.Add(MoveTemp(AddedCluster));
	}
}

FRequestCluster::FRequest::FRequest(FFilePlatformRequest&& FileRequest, bool bInIsUrgent)
	: PackageName(NAME_None)
	, FileName(FileRequest.GetFilename())
	, CompletionCallback(MoveTemp(FileRequest.GetCompletionCallback()))
	, bIsUrgent(bInIsUrgent)
{
}

FRequestCluster::FRequest::FRequest(FName InPackageName, FName InFileName, FCompletionCallback&& InCompletionCallback,
	bool bInIsUrgent)
	: PackageName(InPackageName)
	, FileName(InFileName)
	, CompletionCallback(MoveTemp(InCompletionCallback))
	, bIsUrgent(bInIsUrgent)
{
}

void FRequestCluster::Initialize(UCookOnTheFlyServer& COTFS)
{
	UCookOnTheFlyServer::FCookByTheBookOptions* Options = COTFS.CookByTheBookOptions;
	if (Options)
	{
		bAllowHardDependencies = !Options->bSkipHardReferences;
		// We always skip soft dependencies in cluster exploration if the cook commandline is set to skip soft references.
		// We also need to skip soft dependencies from assetregistry, and make other editor-only robustness fixes, 
		// if the project has problems with editor-only robustness and has turned bExploreSoftReferencesOnStart off
		bAllowSoftDependencies = !Options->bSkipSoftReferences && COTFS.bExploreSoftReferencesOnStart;
		// Build dependency soft references are more robust, and can be enabled whenever the cook mode allows soft references.
		bAllowSoftBuildDependencies = !Options->bSkipSoftReferences;
		bErrorOnEngineContentUse = Options->bErrorOnEngineContentUse;
	}
	bTargetDomainEnabled = COTFS.bTargetDomainEnabled;
	if (bErrorOnEngineContentUse)
	{
		DLCPath = FPaths::Combine(*COTFS.GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(DLCPath);
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
}

void FRequestCluster::FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bPackageNamesComplete)
	{
		return;
	}

	constexpr int32 TimerCheckPeriod = 100; // Do not incur the cost of checking the timer on every package
	int32 RequestsNum = Requests.Num();
	while (this->NextRequest < RequestsNum)
	{
		if (NextRequest % TimerCheckPeriod == 0 && CookerTimer.IsTimeUp())
		{
			break;
		}

		FRequest& Request = Requests[NextRequest];
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
			Requests.RemoveAt(NextRequest); // Preserve the order of incoming requests
			--RequestsNum;
		}
		else
		{
			Request.PackageName = *PackageName;
			++this->NextRequest;
		}
	}
	if (NextRequest < RequestsNum)
	{
		bOutComplete = false;
		return;
	}

	NextRequest = 0;
	bPackageNamesComplete = true;
}

void FRequestCluster::FetchDependencies(const FCookerTimer& CookerTimer, bool& bOutComplete)
{
	if (bDependenciesComplete)
	{
		return;
	}

	if (!bAllowHardDependencies)
	{
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
	TRingBuffer<FRequestClusterGraphData::FNamePair>& TransitiveRequests(DependencyGraphData.TransitiveRequests);
	TRingBuffer<FRequestClusterGraphData::FNamePair>& Segment(DependencyGraphData.Segment);
	TSet<FName>& Visited(DependencyGraphData.Visited);
	TArray<FRequestClusterGraphData::FStackData>& StackStorage(DependencyGraphData.Stack);
	int32& StackNum(DependencyGraphData.StackNum);
	int32 NumRequests = Requests.Num();
	int32 TimerCounter = 0;
	auto IsStackEmpty = [&StackNum]()
	{
		return StackNum == 0;
	};
	auto TopStack = [&StackStorage, &StackNum]() -> FRequestClusterGraphData::FStackData&
	{
		return StackStorage[StackNum-1];
	};
	auto PushStack = [&StackStorage, &StackNum](FName PackageName) -> FRequestClusterGraphData::FStackData&
	{
		if (StackNum == StackStorage.Num())
		{
			StackStorage.Emplace();
		}
		FRequestClusterGraphData::FStackData& Top = StackStorage[StackNum++];
		Top.Reset(PackageName);
		return Top;
	};
	auto PopStack = [&StackNum]
	{
		--StackNum;
	};

	auto AddVertex = [this, &Segment, &Visited, &PushStack, &TimerCounter]
		(FName PackageName, FName FileName, bool bInitial, bool& bOutPushedStack)
	{
		bool bAlreadyInSet;
		Visited.Add(PackageName, &bAlreadyInSet);
		if (bAlreadyInSet)
		{
			bOutPushedStack = false;
			return;
		}
		++TimerCounter;

		// Add the package to the transitive output if its cookable, and always add the initial vertices
		bool bCookable = IsRequestCookable(PackageName, FileName);
		if (bInitial || bCookable)
		{
			bOutPushedStack = true;
			Segment.Add(FRequestClusterGraphData::FNamePair{ PackageName, FileName });
			FRequestClusterGraphData::FStackData& StackData = PushStack(PackageName);

			bool bExploreDependencies = bCookable;
			if (bCookable && !bAllowSoftDependencies)
			{
				// TODO: Editor-only packages may be loaded at editor startup, which makes them skip the request state
				// and go straight to load and save, where they are culled and returned to idle.
				// If we add dependencies from these startup packages, in some projects we add transitive dependency
				// packages that are not otherwise cooked. To prevent breaking projects we are temporarily skipping the
				// dependency search from these packages.
				bool bInProgress = false;
				FPackageData* PackageData = PackageDatas.FindPackageDataByPackageName(PackageName);
				if (PackageData && (PackageData->IsInProgress() || FindPackage(nullptr, *PackageName.ToString())))
				{
					bExploreDependencies = false;
				}
			}
			if (bExploreDependencies)
			{
				GetDependencies(PackageName, StackData.Dependencies);
			}
		}
		else
		{
			bOutPushedStack = false;
		}
	};

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

			FRequestClusterGraphData::FStackData& CurrentTop(TopStack());
			bool bPushedStack = false;
			while (CurrentTop.NextDependency < CurrentTop.Dependencies.Num())
			{
				AddVertex(CurrentTop.Dependencies[CurrentTop.NextDependency++], NAME_None, false /* bInitial */, bPushedStack);
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

		while (NextRequest < NumRequests)
		{
			FRequest& Request = Requests[NextRequest++];
			FRequestClusterGraphData::FNamePair NamePair{ Request.PackageName, Request.FileName };
			bool bPushedStack = false;
			AddVertex(Request.PackageName, Request.FileName, true /* bInitial */, bPushedStack);
			if (bPushedStack)
			{
				break;
			}
		}
		if (IsStackEmpty())
		{
			check(NextRequest == NumRequests);
			break;
		}
	}

	// Map PackageNames to Request for the initial requests, so we can preserve their data (e.g. bIsUrgent) in the output
	TMap<FName, FRequest*> InitialRequests;
	for (FRequest& Request : Requests)
	{
		InitialRequests.Add(Request.PackageName, &Request);
	}

	// Convert all NamePairs found to an FRequest and push them all into the output in LeafToRoot order
	TArray<FRequest> OutRequests;
	OutRequests.Reserve(TransitiveRequests.Num());
	for (int32 LeafToRootIndex = TransitiveRequests.Num() - 1; LeafToRootIndex >= 0; --LeafToRootIndex)
	{
		FRequestClusterGraphData::FNamePair& NamePair = TransitiveRequests[LeafToRootIndex];
		FRequest** InitialRequest = InitialRequests.Find(NamePair.PackageName);
		if (InitialRequest)
		{
			OutRequests.Add(MoveTemp(**InitialRequest));
		}
		else
		{
			OutRequests.Add(FRequest{ NamePair.PackageName, NamePair.FileName, FCompletionCallback(), false /* bIsUrgent */ });
			COOK_STAT(++DetailedCookStats::NumPreloadedDependencies);
		}
	}
	Swap(OutRequests, Requests);

	TransitiveRequests.Empty();
	StackStorage.Empty();
	StackNum = 0;
	Segment.Empty();
	Visited.Empty();
	NextRequest = 0;
	bDependenciesComplete = true;
}

void FRequestCluster::GetDependencies(FName PackageName, TArray<FName>& OutDependencies)
{
	TArray<FName>& BuildDependencies(OutDependencies);
	TArray<FName>& RuntimeDependencies(this->RuntimeScratch);
	BuildDependencies.Reset();
	RuntimeDependencies.Reset();

	// TODO EditorOnly References: We only fetch Game dependencies, because the cooker explicitly loads all of
	// the dependencies that we report. And if we explicitly load an EditorOnly dependency, that causes
	// StaticLoadObjectInternal to SetLoadedByEditorPropertiesOnly(false), which then treats the editor-only package
	// as needed in-game.
	UE::AssetRegistry::EDependencyQuery DependencyQuery = UE::AssetRegistry::EDependencyQuery::Game;
	if (!bAllowSoftDependencies)
	{
		DependencyQuery |= UE::AssetRegistry::EDependencyQuery::Hard;
	}
	AssetRegistry.GetDependencies(PackageName, BuildDependencies,
		UE::AssetRegistry::EDependencyCategory::Package, DependencyQuery);
	if (bTargetDomainEnabled)
	{
		for (const ITargetPlatform* TargetPlatform : Platforms)
		{
			UE::TargetDomain::TryFetchDependencies(PackageName, TargetPlatform, BuildDependencies,
				RuntimeDependencies);
		}
	}
	if (bAllowSoftBuildDependencies)
	{
		BuildDependencies.Append(RuntimeDependencies);
	}

	// Sort the list of BuildDependencies to check for uniqueness and make them deterministic
	Algo::Sort(BuildDependencies, FNameLexicalLess());
	BuildDependencies.SetNum(Algo::Unique(BuildDependencies));
}

bool FRequestCluster::IsRequestCookable(FName PackageName, FName& InOutFileName)
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
		InOutFileName = PackageNameCache.GetCachedStandardFileName(PackageName);
		if (InOutFileName.IsNone())
		{
			// Package does not exist on disk
			return false;
		}
	}

	if (bErrorOnEngineContentUse && !DLCPath.IsEmpty())
	{
		InOutFileName.ToString(NameBuffer);
		if (!FStringView(NameBuffer).StartsWith(DLCPath))
		{
			return false;
		}
	}

	if (PackageTracker.NeverCookPackageList.Contains(InOutFileName))
	{
		return false;
	}

	return true;
}

} // namespace UE::Cook
