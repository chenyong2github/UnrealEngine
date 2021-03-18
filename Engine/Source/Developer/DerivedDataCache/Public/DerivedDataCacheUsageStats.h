// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ProfilingDebugging/CookStats.h"

/**
 * Usage stats for the derived data cache nodes. At the end of the app or commandlet, the DDC
 * can be asked to gather usage stats for each of the nodes in the DDC graph, which are accumulated
 * into a TMap of Name->Stats. The Stats portion is this class.
 * 
 * The class exposes various high-level routines to time important aspects of the DDC, mostly
 * focusing on performance of GetCachedData, PutCachedData, and CachedDataProbablyExists. This class
 * will track time taken, calls made, hits, misses, bytes processed, and do it for two buckets:
 * 1) the main thread and 2) all other threads.  The reason is because any time spent in the DDC on the
 * main thread is considered meaningful, as DDC access is generally expected to be async from helper threads.
 * 
 * The class goes through a bit of trouble to use thread-safe access calls for task-thread usage, and 
 * simple, fast accumulators for game-thread usage, since it's guaranteed to not be written to concurrently.
 * The class also limits itself to checking the thread once at construction.
 * 
 * Usage would be something like this in a concrete FDerivedDataBackendInterface implementation:
 *   class MyBackend : public FDerivedDataBackendInterface
 *   {
 *       FDerivedDataCacheUsageStats UsageStats;
 *   public:
 *       <override CachedDataProbablyExists>
 *       { 
 *           auto Timer = UsageStats.TimeExists();
 *           ...
 *       }
 *       <override GetCachedData>
 *       {    
 *           auto Timer = UsageStats.TimeGet();
 *           ...
 *           <if it's a cache hit> Timer.AddHit(DataSize);
 *           // Misses are automatically tracked
 *       }
 *       <override PutCachedData>
 *       {
 *           auto Timer = UsageStats.TimePut();
 *           ...
 *           <if the data will really be Put> Timer.AddHit(DataSize);
 *           // Misses are automatically tracked
 *       }
 *       <override GatherUsageStats>
 *       {
 *           // Add this node's UsageStats to the usage map. Your Key name should be UNIQUE to the entire graph (so use the file name, or pointer to this if you have to).
 *           UsageStatsMap.Add(FString::Printf(TEXT("%s: <Some unique name for this node instance>"), *GraphPath), UsageStats);
 *       }
*   }
 */
class FDerivedDataCacheUsageStats
{
#if ENABLE_COOK_STATS
public:

	/** Call this at the top of the CachedDataProbablyExists override. auto Timer = TimeProbablyExists(); */
	FCookStats::FScopedStatsCounter TimeProbablyExists()
	{
		return FCookStats::FScopedStatsCounter(ExistsStats);
	}

	/** Call this at the top of the GetCachedData override. auto Timer = TimeGet(); Use AddHit on the returned type to track a cache hit. */
	FCookStats::FScopedStatsCounter TimeGet()
	{
		return FCookStats::FScopedStatsCounter(GetStats);
	}

	/** Call this at the top of the PutCachedData override. auto Timer = TimePut(); Use AddHit on the returned type to track a cache hit. */
	FCookStats::FScopedStatsCounter TimePut()
	{
		return FCookStats::FScopedStatsCounter(PutStats);
	}

	/** Call this at the top of the PutCachedData override. auto Timer = TimePut(); Use AddHit on the returned type to track a cache hit. */
	FCookStats::FScopedStatsCounter TimePrefetch()
	{
		return FCookStats::FScopedStatsCounter(PrefetchStats);
	}

	void LogStats(FCookStatsManager::AddStatFuncRef AddStat, const FString& StatName, const FString& NodeName) const
	{
		GetStats.LogStats(AddStat, StatName, NodeName, TEXT("Get"));
		PutStats.LogStats(AddStat, StatName, NodeName, TEXT("Put"));
		ExistsStats.LogStats(AddStat, StatName, NodeName, TEXT("Exists"));
		PrefetchStats.LogStats(AddStat, StatName, NodeName, TEXT("Prefetch"));
	}

	void Combine(const FDerivedDataCacheUsageStats& Other)
	{
		GetStats.Combine(Other.GetStats);
		PutStats.Combine(Other.PutStats);
		ExistsStats.Combine(Other.ExistsStats);
		PrefetchStats.Combine(Other.PrefetchStats);
	}

	// expose these publicly for low level access. These should really never be accessed directly except when finished accumulating them.
	FCookStats::CallStats GetStats;
	FCookStats::CallStats PutStats;
	FCookStats::CallStats ExistsStats;
	FCookStats::CallStats PrefetchStats;
#endif
};

class FDerivedDataBackendInterface;

/**
 *  Hierarchical usage stats for the DDC nodes.
 */
class FDerivedDataCacheStatsNode : public TSharedFromThis<FDerivedDataCacheStatsNode>
{
public:
	FDerivedDataCacheStatsNode(const FDerivedDataBackendInterface* InBackendInterface, const FString& InCacheName)
		: BackendInterface(InBackendInterface)
		, CacheName(InCacheName)
	{
	}

	const FDerivedDataBackendInterface* GetBackendInterface() const { return BackendInterface; }

	const FString& GetCacheName() const { return CacheName; }

	TMap<FString, FDerivedDataCacheUsageStats> ToLegacyUsageMap() const
	{
		TMap<FString, FDerivedDataCacheUsageStats> UsageStats;
		GatherLegacyUsageStats(UsageStats, TEXT(" 0"));
		return UsageStats;
	}

	void ForEachDescendant(TFunctionRef<void(TSharedRef<const FDerivedDataCacheStatsNode>)> Predicate) const
	{
		Predicate(SharedThis(this));

		for (const TSharedRef<FDerivedDataCacheStatsNode>& Child : Children)
		{
			Child->ForEachDescendant(Predicate);
		}
	}

public:
	void GatherLegacyUsageStats(TMap<FString, FDerivedDataCacheUsageStats>& UsageStatsMap, FString&& GraphPath) const
	{
		if (Stats.Num() == 1)
		{
			for (const auto& KVP : Stats)
			{
				COOK_STAT(UsageStatsMap.Add(FString::Printf(TEXT("%s: %s"), *GraphPath, *GetCacheName()), KVP.Value));
			}
		}
		else
		{
			for (const auto& KVP : Stats)
			{
				COOK_STAT(UsageStatsMap.Add(FString::Printf(TEXT("%s: %s.%s"), *GraphPath, *GetCacheName(), *KVP.Key), KVP.Value));
			}
		}

		int Ndx = 0;
		for (const TSharedRef<FDerivedDataCacheStatsNode>& Child : Children)
		{
			Child->GatherLegacyUsageStats(UsageStatsMap, GraphPath + FString::Printf(TEXT(".%2d"), Ndx++));
		}
	}

	TMap<FString, FDerivedDataCacheUsageStats> Stats;

	TArray<TSharedRef<FDerivedDataCacheStatsNode>> Children;

protected:
	const FDerivedDataBackendInterface* BackendInterface;
	FString CacheName;
};