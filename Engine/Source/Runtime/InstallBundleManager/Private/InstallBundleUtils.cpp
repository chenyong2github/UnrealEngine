// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleUtils.h"
#include "InstallBundleManagerPrivatePCH.h"
#include "Misc/App.h"

#include "HAL/PlatformApplicationMisc.h"

namespace InstallBundleUtil
{
	FString GetAppVersion()
	{
		return FString::Printf(TEXT("%s-%s"), FApp::GetBuildVersion(), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
	}

	bool HasInternetConnection(ENetworkConnectionType ConnectionType)
	{
		return ConnectionType != ENetworkConnectionType::AirplaneMode
			&& ConnectionType != ENetworkConnectionType::None;
	}

	const TCHAR* GetInstallBundlePauseReason(EInstallBundlePauseFlags Flags)
	{
		// Return the most appropriate reason given the flags

		if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::UserPaused))
			return TEXT("UserPaused");

		if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::NoInternetConnection))
			return TEXT("NoInternetConnection");

		if (EnumHasAnyFlags(Flags, EInstallBundlePauseFlags::OnCellularNetwork))
			return TEXT("OnCellularNetwork");

		return TEXT("");
	}

	FName FInstallBundleManagerKeepAwake::Tag(TEXT("InstallBundleManagerKeepAwake"));
	FName FInstallBundleManagerKeepAwake::TagWithRendering(TEXT("InstallBundleManagerKeepAwakeWithRendering"));

	bool FInstallBundleManagerScreenSaverControl::bDidDisableScreensaver = false;
	int FInstallBundleManagerScreenSaverControl::DisableCount = 0;

	void FInstallBundleManagerScreenSaverControl::IncDisable()
	{
		if (!bDidDisableScreensaver && FPlatformApplicationMisc::IsScreensaverEnabled())
		{
			bDidDisableScreensaver = FPlatformApplicationMisc::ControlScreensaver(FGenericPlatformApplicationMisc::EScreenSaverAction::Disable);
		}

		++DisableCount;
	}

	void FInstallBundleManagerScreenSaverControl::DecDisable()
	{
		--DisableCount;
		if (DisableCount == 0 && bDidDisableScreensaver)
		{
			FPlatformApplicationMisc::ControlScreensaver(FGenericPlatformApplicationMisc::EScreenSaverAction::Enable);
			bDidDisableScreensaver = false;
		}
	}

	void StartInstallBundleAsyncIOTask(TArray<TUniquePtr<FInstallBundleTask>>& Tasks, TUniqueFunction<void()> WorkFunc, TUniqueFunction<void()> OnComplete)
	{
		TUniquePtr<FInstallBundleTask> Task = MakeUnique<FInstallBundleTask>(MoveTemp(WorkFunc), MoveTemp(OnComplete));
		Task->StartBackgroundTask(GIOThreadPool);
		Tasks.Add(MoveTemp(Task));
	}

	void FinishInstallBundleAsyncIOTasks(TArray<TUniquePtr<FInstallBundleTask>>& Tasks)
	{
		TArray<TUniquePtr<FInstallBundleTask>> FinishedTasks;
		for (int32 i = 0; i < Tasks.Num();)
		{
			TUniquePtr<FInstallBundleTask>& Task = Tasks[i];
			check(Task);
			if (Task->IsDone())
			{
				FinishedTasks.Add(MoveTemp(Task));
				Tasks.RemoveAtSwap(i, 1, false);
			}
			else
			{
				++i;
			}
		}
		for (TUniquePtr<FInstallBundleTask>& Task : FinishedTasks)
		{
			Task->GetTask().CallOnComplete();
		}
	}

	void CleanupInstallBundleAsyncIOTasks(TArray<TUniquePtr<FInstallBundleTask>>& Tasks)
	{
		for (TUniquePtr<FInstallBundleTask>& Task : Tasks)
		{
			check(Task);
			if (!Task->Cancel())
			{
				Task->EnsureCompletion(false);
			}
		}
	}

	void FContentRequestStatsMap::StatsBegin(FName BundleName)
	{
		FContentRequestStats& Stats = StatsMap.FindOrAdd(BundleName);
		if (ensureAlwaysMsgf(Stats.bOpen, TEXT("StatsBegin - Stat closed for %s"), *BundleName.ToString()) == false)
		{
			Stats = FContentRequestStats();
		}

		Stats.StartTime = FPlatformTime::Seconds();
	}

	void FContentRequestStatsMap::StatsEnd(FName BundleName)
	{
		FContentRequestStats& Stats = StatsMap.FindOrAdd(BundleName);

		if (ensureAlwaysMsgf(Stats.bOpen, TEXT("StatsEnd - Stat closed for %s"), *BundleName.ToString()))
		{
			Stats.EndTime = FPlatformTime::Seconds();
			Stats.bOpen = false;
		}
	}

	void FContentRequestStatsMap::StatsBegin(FName BundleName, const TCHAR* State)
	{
		FContentRequestStats& Stats = StatsMap.FindOrAdd(BundleName);
		if (ensureAlwaysMsgf(Stats.bOpen, TEXT("StatsBegin - Stat closed for %s - %s"), *BundleName.ToString(), State) == false)
		{
			Stats = FContentRequestStats();
			Stats.StartTime = FPlatformTime::Seconds();
		}

		FContentRequestStateStats& StateStats = Stats.StateStats.FindOrAdd(State);
		if (ensureAlwaysMsgf(StateStats.bOpen, TEXT("StatsBegin - StateStat closed for %s - %s"), *BundleName.ToString(), State) == false)
		{
			StateStats = FContentRequestStateStats();
		}

		StateStats.StartTime = FPlatformTime::Seconds();
	}

	void FContentRequestStatsMap::StatsEnd(FName BundleName, const TCHAR* State, uint64 DataSize /*= 0*/)
	{
		FContentRequestStats& Stats = StatsMap.FindOrAdd(BundleName);
		if (ensureAlwaysMsgf(Stats.bOpen, TEXT("StatsEnd - Stat closed for %s - %s"), *BundleName.ToString(), State) == false)
		{
			Stats = FContentRequestStats();
			Stats.StartTime = FPlatformTime::Seconds();
		}

		FContentRequestStateStats& StateStats = Stats.StateStats.FindOrAdd(State);
		if(ensureAlwaysMsgf(StateStats.bOpen, TEXT("StatsEnd - StateStat closed for %s - %s"), *BundleName.ToString(), State))
		{
			StateStats.EndTime = FPlatformTime::Seconds();
			StateStats.DataSize = DataSize;
			StateStats.bOpen = false;
		}
	}
}
