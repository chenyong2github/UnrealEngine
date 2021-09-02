// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataInformation.h"
#include "SDerivedDataStatusBar.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DerivedDataEditor"

double FDerivedDataInformation::GetCacheActivitySizeBytes(bool bGet, bool bLocal)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	int64 TotalBytes = 0;

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if ((Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local) != bLocal)
			continue;

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();

		for (const auto& KVP : Usage->Stats)
		{
			const FDerivedDataCacheUsageStats& Stats = KVP.Value;

			if (bGet)
			{
				TotalBytes += Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
			}
			else
			{
				TotalBytes += Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
			}
		}
	}

	return TotalBytes;
}


double FDerivedDataInformation::GetCacheActivityTimeSeconds(bool bGet, bool bLocal)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	int64 TotalCycles = 0;

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if ((Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local) != bLocal)
			continue;

		TSharedRef<FDerivedDataCacheStatsNode> Usage = Backend->GatherUsageStats();

		for (const auto& KVP : Usage->Stats)
		{
			const FDerivedDataCacheUsageStats& Stats = KVP.Value;

			if (bGet)
			{
				TotalCycles +=
					(Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));

				TotalCycles +=
					(Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
			}
			else
			{
				TotalCycles +=
					(Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
						Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
			}
		}
	}

	return (double)TotalCycles * FPlatformTime::GetSecondsPerCycle();
}

bool FDerivedDataInformation::GetHasLocalCache()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if (Backend->GetSpeedClass() == FDerivedDataBackendInterface::ESpeedClass::Local)
			return true;
	}

	return false;
}


bool FDerivedDataInformation::GetHasRemoteCache()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TArray<TSharedRef<const FDerivedDataCacheStatsNode>> LeafUsageStats;
	RootUsage->ForEachDescendant([&LeafUsageStats](TSharedRef<const FDerivedDataCacheStatsNode> Node) {
		if (Node->Children.Num() == 0)
		{
			LeafUsageStats.Add(Node);
		}
		});

	for (int32 Index = 0; Index < LeafUsageStats.Num(); Index++)
	{
		const FDerivedDataBackendInterface* Backend = LeafUsageStats[Index]->GetBackendInterface();

		if (Backend->GetSpeedClass() != FDerivedDataBackendInterface::ESpeedClass::Local)
			return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE