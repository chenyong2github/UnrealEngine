// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataInformation.h"
#include "SDerivedDataStatusBar.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "Settings/EditorProjectSettings.h"
#include "Settings/EditorSettings.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DerivedDataEditor"

double FDerivedDataInformation::LastGetTime = 0;
double FDerivedDataInformation::LastPutTime = 0;
bool FDerivedDataInformation::bIsDownloading = false;
bool FDerivedDataInformation::bIsUploading = false;
FText FDerivedDataInformation::RemoteCacheWarningMessage;
ERemoteCacheState FDerivedDataInformation::RemoteCacheState= ERemoteCacheState::Unavailable;

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

		if (Backend->IsRemote() == bLocal)
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

		if (Backend->IsRemote() == bLocal)
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

		if (Backend->IsRemote())
			return true;
	}

	return false;
}

void FDerivedDataInformation::UpdateRemoteCacheState()
{
	RemoteCacheState = ERemoteCacheState::Unavailable;

	if ( GetHasRemoteCache() )
	{
		const double OldLastGetTime = LastGetTime;
		const double OldLastPutTime = LastPutTime;

		LastGetTime = FDerivedDataInformation::GetCacheActivityTimeSeconds(true, false);
		LastPutTime = FDerivedDataInformation::GetCacheActivityTimeSeconds(false, false);

		if (OldLastGetTime != 0.0 && OldLastPutTime != 0.0)
		{
			bIsDownloading = OldLastGetTime != LastGetTime;
			bIsUploading = OldLastPutTime != LastPutTime;
		}

		if (bIsUploading || bIsDownloading)
		{
			RemoteCacheState = ERemoteCacheState::Busy;
		}
		else
		{
			RemoteCacheState = ERemoteCacheState::Idle;
		}
	}

	const UDDCProjectSettings* DDCProjectSettings = GetDefault<UDDCProjectSettings>();

	if (DDCProjectSettings->EnableWarnings)
	{
		const UEditorSettings* EditorSettings = GetDefault<UEditorSettings>();

		if (DDCProjectSettings->RecommendEveryoneSetupAGlobalLocalDDCPath && EditorSettings->GlobalLocalDDCPath.Path.IsEmpty())
		{
			RemoteCacheState = ERemoteCacheState::Warning;
			RemoteCacheWarningMessage = FText(LOCTEXT("GlobalLocalDDCPathWarning", "It is recommended that you set up a valid Global Local DDC Path"));
		}
		else if (DDCProjectSettings->RecommendEveryoneEnableS3DDC && EditorSettings->bEnableS3DDC == false)
		{
			RemoteCacheState = ERemoteCacheState::Warning;
			RemoteCacheWarningMessage = FText(LOCTEXT("AWSS3CacheEnabledWarning", "It is recommended that you enable the AWS S3 Cache"));
		}
		else if (DDCProjectSettings->RecommendEveryoneSetupAGlobalS3DDCPath && EditorSettings->GlobalS3DDCPath.Path.IsEmpty())
		{
			RemoteCacheState = ERemoteCacheState::Warning;
			RemoteCacheWarningMessage = FText(LOCTEXT("S3GloblaLocalPathdWarning", "It is recommended that you set up a valid Global Local S3 DDC Path"));
		}
		
	}
}


FText FDerivedDataInformation::GetRemoteCacheStateAsText()
{
	switch (FDerivedDataInformation::GetRemoteCacheState())
	{
	case ERemoteCacheState::Idle:
	{
		return FText(LOCTEXT("DDCStateIdle","Idle"));
		break;
	}

	case ERemoteCacheState::Busy:
	{
		return FText(LOCTEXT("DDCStateBusy", "Busy"));
		break;
	}

	case ERemoteCacheState::Unavailable:
	{
		return FText(LOCTEXT("DDCStateUnavailable", "Unavailable"));
		break;
	}

	default:
	case ERemoteCacheState::Warning:
	{
		return FText(LOCTEXT("DDCStateWarning", "Warning"));
		break;
	}
	}
}


#undef LOCTEXT_NAMESPACE