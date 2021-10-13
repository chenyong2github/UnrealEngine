// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/RingBuffer.h"
#include "Cooker/CookTypes.h"
#include "TargetDomain/TargetDomainUtils.h"
#include "UObject/NameTypes.h"

class IAssetRegistry;
class ITargetPlatform;
class UCookOnTheFlyServer;
struct FPackageNameCache;
namespace UE::Cook { class FRequestQueue; }
namespace UE::Cook { struct FFilePlatformRequest; }
namespace UE::Cook { struct FPackageTracker; }

namespace UE::Cook
{

/**
 * A group of external requests sent to CookOnTheFlyServer's tick loop. Transitive dependencies are found and all of the
 * requested or dependent packagenames are added as requests together to the cooking state machine.
 */
class FRequestCluster
{
public:
	/** The cluster's data about a package from the external requests or discovered dependency. */
	struct FFileNameRequest
	{
		FName FileName;
		FCompletionCallback CompletionCallback;
		bool bUrgent;

		FFileNameRequest(FFilePlatformRequest&& FileRequest, bool bInUrgent);
	};
	explicit FRequestCluster(UCookOnTheFlyServer& COTFS, TArray<const ITargetPlatform*>&& Platforms);
	explicit FRequestCluster(UCookOnTheFlyServer& COTFS, TConstArrayView<const ITargetPlatform*> Platforms);
	FRequestCluster(FRequestCluster&&) = default;

	/**
	 * Calculate the information needed to create a PackageData, and transitive search dependencies for all requests.
	 * Called repeatedly (due to timeslicing) until bOutComplete is set to true.
	 */
	void Process(const FCookerTimer& CookerTimer, bool& bOutComplete);

	TConstArrayView<const ITargetPlatform*> GetPlatforms() const { return Platforms; }
	/** PackageData container interface: return the number of PackageDatas owned by this container. */
	int32 NumPackageDatas() const;
	/** PackageData container interface: remove the PackageData from this container. */
	void RemovePackageData(FPackageData* PackageData);
	/** PackageData container interface: whether the PackageData is owned by this container. */
	bool Contains(FPackageData* PackageData) const;
	/**
	 * Remove all PackageDatas owned by this container and return them.
	 * OutRequestsToLoad is the set of PackageDatas sorted by leaf to root load order.
	 * OutRequestToDemote is the set of Packages that are uncookable or have already been cooked.
	 * If called before Process sets bOutComplete=true, all packages are put in OutRequestToLoad and are unsorted.
	 */
	void ClearAndDetachOwnedPackageDatas(TArray<FPackageData*>& OutRequestsToLoad, TArray<FPackageData*>& OutRequestsToDemote);

	/**
	 * Create clusters(s) for all the given name or packagedata requests and append them to OutClusters.
	 * Multiple clusters are necessary if the list of platforms differs between requests.
	 */
	static void AddClusters(UCookOnTheFlyServer& COTFS, TArray<FFilePlatformRequest>&& InRequests,
		bool bRequestsAreUrgent, TRingBuffer<FRequestCluster>& OutClusters);
	static void AddClusters(UCookOnTheFlyServer& COTFS, FPackageDataSet& UnclusteredRequests,
		TRingBuffer<FRequestCluster>& OutClusters, FRequestQueue& QueueForReadyRequests);

private:
	struct FStackData
	{
		void Reset(FName InPackageName)
		{
			PackageName = InPackageName;
			Dependencies.Reset();
			NextDependency = 0;
		}
		TArray<FName> Dependencies;
		FName PackageName;
		int32 NextDependency;
	};
	enum class EVisitStatus
	{
		New,
		Visited,
		Skipped,
	};

	void Initialize(UCookOnTheFlyServer& COTFS);
	void FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void FetchDependencies(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void StartAsync(const FCookerTimer& CookerTimer, bool& bOutComplete);
	bool TryTakeOwnership(FPackageData& PackageData, bool bUrgent, FCompletionCallback&& CompletionCallback);
	void VisitPackageData(FPackageData* PackageData, TArray<FName>* OutDependencies, bool& bOutAlreadyCooked);
	bool IsRequestCookable(FName PackageName, FName& InOutFileName, FPackageData*& PackageData);
	static bool IsRequestCookable(FName PackageName, FName& InOutFileName, FPackageData*& PackageData,
		const FPackageNameCache& InPackageNameCache, FPackageDatas& InPackageDatas, FPackageTracker& InPackageTracker,
		FStringView InDLCPath, bool bInErrorOnEngineContentUse, TConstArrayView<const ITargetPlatform*> RequestPlatforms);

	// Graph Search functions
	EVisitStatus& AddVertex(FName PackageName, FPackageData* PackageData, bool& bOutPushedStack, int32& TimerCounter);
	bool IsStackEmpty() const;
	FStackData& TopStack();
	FStackData& PushStack(FName PackageName);
	void PopStack();

	TArray<FFileNameRequest> InRequests;
	TArray<FPackageData*> Requests;
	TArray<FPackageData*> RequestsToDemote;
	UE::TargetDomain::FCookAttachments OplogDataScratch;
	TSet<FName> NameSetScratch;
	TArray<const ITargetPlatform*> Platforms;
	TArray<ICookedPackageWriter*> PackageWriters;
	FPackageDataSet OwnedPackageDatas;
	FString DLCPath;
	FPackageDatas& PackageDatas;
	IAssetRegistry& AssetRegistry;
	const FPackageNameCache& PackageNameCache;
	FPackageTracker& PackageTracker;
	FBuildDefinitions& BuildDefinitions;
	int32 NextRequest = 0;
	bool bAllowHardDependencies = true;
	bool bAllowSoftDependencies = true;
	bool bPreexploreDependenciesEnabled = true;
	bool bHybridIterativeEnabled = true;
	bool bErrorOnEngineContentUse = false;
	bool bPackageNamesComplete = false;
	bool bDependenciesComplete = false;
	bool bStartAsyncComplete = false;
	bool bFullBuild = false;
	bool bPreQueueBuildDefinitions = true;


	/** Data used during the reentrant dependencies and topological sort operation of FRequestCluster. */
	/** All transitive PackageNames found from from all initial requests. Root to leaf order. */
	TRingBuffer<FPackageData*> TransitiveRequests;
	/** Transitive PackageNames from the current initial request not already in TransitiveRequests. Root to leaf order. */
	TRingBuffer<FPackageData*> Segment;
	/** Which PackageNames have already been reached (possibly still on the stack). */
	TMap<FName, EVisitStatus> Visited;
	/** Storage for requests that are currently iterating over their dependencies. */
	TArray<FStackData> StackStorage;
	/** Num elements in StackStorage. We avoid TArray::Add/Remove to avoid reallocating FStackData internals. */
	int32 StackNum = 0;
};

}
