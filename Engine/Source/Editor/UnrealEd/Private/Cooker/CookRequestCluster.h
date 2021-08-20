// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/RingBuffer.h"
#include "Cooker/CookTypes.h"
#include "UObject/NameTypes.h"

class IAssetRegistry;
class ITargetPlatform;
class UCookOnTheFlyServer;
struct FPackageNameCache;
namespace UE::Cook { struct FFilePlatformRequest; }
namespace UE::Cook { struct FPackageTracker; }

namespace UE::Cook
{

/** Data used during the reentrant dependencies and topological sort operation of FRequestCluster. */
struct FRequestClusterGraphData
{
	struct FNamePair
	{
		FName PackageName;
		FName FileName;
	};
	/** All transitive PackageNames found from from all initial requests. Root to leaf order. */
	TRingBuffer<FNamePair> TransitiveRequests;
	/** Transitive PackageNames from the current initial request not already in TransitiveRequests. Root to leaf order. */
	TRingBuffer<FNamePair> Segment;
	/** Which PackageNames have already been reached (possibly still on the stack). */
	TSet<FName> Visited;
	/** Requests that are currently iterating over their dependencies. */
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
	TArray<FStackData> Stack;
	/** We do not remove FStackData from the stack, because we want to avoid reallocating Dependencies arrays. */
	int32 StackNum = 0;
};

/**
 * A group of external requests sent to CookOnTheFlyServer's tick loop. Transitive dependencies are found and all of the
 * requested or dependent packagenames are added as requests together to the cooking state machine.
 */
class FRequestCluster
{
public:
	/** The cluster's data about a package from the external requests or discovered dependency. */
	struct FRequest
	{
		FName PackageName;
		FName FileName;
		FCompletionCallback CompletionCallback;
		bool bIsUrgent;

		FRequest(FName InPackageName, FName InFileName, FCompletionCallback&& InCompletionCallback, bool bInIsUrgent);
		FRequest(FFilePlatformRequest&& FileRequest, bool bInIsUrgent);
	};
	explicit FRequestCluster(UCookOnTheFlyServer& COTFS, TArray<const ITargetPlatform*>&& Platforms);
	FRequestCluster(FRequestCluster&&) = default;

	/**
	 * Calculate the information needed to create a PackageData, and transitive search dependencies for all requests.
	 * Called repeatedly (due to timeslicing) until bOutComplete is set to true.
	 */
	void Process(const FCookerTimer& CookerTimer, bool& bOutComplete);

	TConstArrayView<const ITargetPlatform*> GetPlatforms() const { return Platforms; }
	TArray<FRequest>& GetRequests() { return Requests; }

	/**
	 * Create clusters(s) for all the given requests and append them to OutClusters.
	 * Multiple clusters are necessary if the list of platforms differs between requests.
	 */
	static void AddClusters(UCookOnTheFlyServer& COTFS, TArray<FFilePlatformRequest>&& InRequests, bool bRequestsAreUrgent,
		TRingBuffer<FRequestCluster>& OutClusters);

private:
	void Initialize(UCookOnTheFlyServer& COTFS);
	void FetchPackageNames(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void FetchDependencies(const FCookerTimer& CookerTimer, bool& bOutComplete);
	void GetDependencies(FName PackageName, TArray<FName>& OutDependencies);
	bool IsRequestCookable(FName PackageName, FName& InOutFileName);

	TArray<FRequest> Requests;
	TArray<FName> RuntimeScratch;
	TArray<const ITargetPlatform*> Platforms;
	TArray<ICookedPackageWriter*> PackageWriters;
	FRequestClusterGraphData DependencyGraphData;
	FString DLCPath;
	FPackageDatas& PackageDatas;
	IAssetRegistry& AssetRegistry;
	const FPackageNameCache& PackageNameCache;
	FPackageTracker& PackageTracker;
	int32 NextRequest = 0;
	bool bAllowHardDependencies = true;
	bool bAllowSoftDependencies = true;
	bool bAllowSoftBuildDependencies = true;
	bool bTargetDomainEnabled = true;
	bool bErrorOnEngineContentUse = false;
	bool bPackageNamesComplete = false;
	bool bDependenciesComplete = false;
};

}
