// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleUtils.h"
#include "InstallBundleManagerPrivatePCH.h"
#include "Misc/App.h"

#include "HAL/PlatformApplicationMisc.h"

#include "Containers/Ticker.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/JsonSerializerMacros.h"

namespace InstallBundleUtil
{
	INSTALLBUNDLEMANAGER_API FString GetAppVersion()
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

	namespace PersistentStats
	{
		const FString& LexToString(ETimingStatNames InType)
		{
			static const FString RealTotalTime(TEXT("RealTotalTime"));
			static const FString ActiveTotalTime(TEXT("ActiveTotalTime"));
			static const FString EstimatedTotalBGTime(TEXT("EstimatedTotalBGTime"));
			static const FString RealChunkDBDownloadTime(TEXT("RealChunkDBDownloadTime"));
			static const FString ActiveChunkDBDownloadTime(TEXT("ActiveChunkDBDownloadTime"));
			static const FString EstimatedBackgroundChunkDBDownloadTime(TEXT("EstimatedBackgroundChunkDBDownloadTime"));
			static const FString ActiveInstallTime(TEXT("ActiveInstallTime"));
			static const FString EstimatedBGInstallTime(TEXT("EstimatedBGInstallTime"));
			static const FString ActivePSOTime(TEXT("ActivePSOTime"));
			static const FString EstimatedBGPSOTime(TEXT("EstimatedBGPSOTime"));

			static const FString Unknown(TEXT("<Unknown PersistentStats::ETimingStatNames Value>"));

			switch (InType)
			{
			case ETimingStatNames::RealTotalTime:
				return RealTotalTime;
			case ETimingStatNames::ActiveTotalTime:
				return ActiveTotalTime;
			case ETimingStatNames::EstimatedTotalBGTime:
				return EstimatedTotalBGTime;
			case ETimingStatNames::RealChunkDBDownloadTime:
				return RealChunkDBDownloadTime;
			case ETimingStatNames::ActiveChunkDBDownloadTime:
				return ActiveChunkDBDownloadTime;
			case ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime:
				return EstimatedBackgroundChunkDBDownloadTime;
			case ETimingStatNames::ActiveInstallTime:
				return ActiveInstallTime;
			case ETimingStatNames::EstimatedBGInstallTime:
				return EstimatedBGInstallTime;
			case ETimingStatNames::ActivePSOTime:
				return ActivePSOTime;
			case ETimingStatNames::EstimatedBGPSOTime:
				return EstimatedBGPSOTime;
			default:
				break;
			}

			ensureAlwaysMsgf(false, TEXT("Missing PersistentStats::ETimingStatNames LexToString entry! Missing Entry as Int: %d"), (int)(InType));
			return Unknown;
		}
	
		const FString& LexToString(ECountStatNames InType)
		{
			static const FString NumResumedFromBackground(TEXT("NumResumedFromBackground"));
			static const FString NumResumedFromLaunch(TEXT("NumResumedFromLaunch"));
			static const FString NumBackgrounded(TEXT("NumBackgrounded"));

			static const FString Unknown(TEXT("<Unknown PersistentStats::ETimingStatNames Value>"));

			switch (InType)
			{
			case ECountStatNames::NumResumedFromBackground:
				return NumResumedFromBackground;
			case ECountStatNames::NumResumedFromLaunch:
				return NumResumedFromLaunch;
			case ECountStatNames::NumBackgrounded:
				return NumBackgrounded;
			default:
				break;
			}

			ensureAlwaysMsgf(false, TEXT("Missing PersistentStats::ECountStatNames LexToString entry! Missing Entry as Int: %d"), (int)(InType));
			return Unknown;
		}

		bool FPersistentStatsBase::LoadStatsFromDisk()
		{
			FString JSONStringOnDisk;
			if (FPaths::FileExists(GetFullPathForStatFile()))
			{
				FFileHelper::LoadFileToString(JSONStringOnDisk, *GetFullPathForStatFile());
			}

			if (!JSONStringOnDisk.IsEmpty())
			{
				return FromJson(JSONStringOnDisk);
			}

			return false;
		}

		bool FPersistentStatsBase::SaveStatsToDisk()
		{
			bIsDirty = false;
			return FFileHelper::SaveStringToFile(ToJson(), *GetFullPathForStatFile());
		}

		void FPersistentStatsBase::ResetStats(const FString& NewAnalyticsSessionID)
		{
			TimingStatsMap.Reset();
			CountStatMap.Reset();
			AnalyticsSessionID = NewAnalyticsSessionID;

			bIsDirty = true;
		}

		bool FPersistentStatsBase::HasTimingStat(ETimingStatNames StatToCheck) const
		{
			const FPersistentTimerData* FoundStat = TimingStatsMap.Find(LexToString(StatToCheck));
			return (nullptr != FoundStat);
		}

		bool FPersistentStatsBase::HasCountStat(ECountStatNames StatToCheck) const
		{
			const int* FoundStat = CountStatMap.Find(LexToString(StatToCheck));
			return (nullptr != FoundStat);
		}

		const FPersistentTimerData* FPersistentStatsBase::GetTimingStatData(ETimingStatNames StatToGet) const
		{ 
			return TimingStatsMap.Find(LexToString(StatToGet));
		}

		const int* FPersistentStatsBase::GetCountStatData(ECountStatNames StatToGet) const
		{
			return CountStatMap.Find(LexToString(StatToGet));
		}

		void FPersistentStatsBase::IncrementCountStat(PersistentStats::ECountStatNames StatToUpdate)
		{
			int& StatCount = CountStatMap.FindOrAdd(LexToString(StatToUpdate));
			++StatCount;

			bIsDirty = true;
		}

		bool FPersistentStatsBase::IsTimingStatStarted(PersistentStats::ETimingStatNames StatToUpdate) const
		{
			bool HasStarted = false;

			if (HasTimingStat(StatToUpdate))
			{
				const FPersistentTimerData* FoundStat = GetTimingStatData(StatToUpdate);
				if (ensureAlwaysMsgf((nullptr != FoundStat), TEXT("Missing FInstallBundlePersistentTimingData but returned true from HasTimingStat For Stat:%s!"), *LexToString(StatToUpdate)))
				{
					HasStarted = (FoundStat->LastUpdateTime != 0.);
				}
			}

			return HasStarted;
		}

		void FPersistentStatsBase::StartTimingStat(PersistentStats::ETimingStatNames StatToUpdate)
		{
			FPersistentTimerData& FoundStat = TimingStatsMap.FindOrAdd(LexToString(StatToUpdate));
			FoundStat.LastUpdateTime = FPlatformTime::Seconds();

			bIsDirty = true;
		}

		void FPersistentStatsBase::StopTimingStat(PersistentStats::ETimingStatNames StatToUpdate, bool UpdateTimerOnStop /* = true */)
		{
			//Only want to actually update the timer if we have started it (otherwise the update won't do anything and will ensure)
			if (UpdateTimerOnStop && IsTimingStatStarted(StatToUpdate))
			{
				UpdateTimingStat(StatToUpdate);
			}

			FPersistentTimerData& FoundStat = TimingStatsMap.FindOrAdd(LexToString(StatToUpdate));
			FoundStat.LastUpdateTime = 0.;

			bIsDirty = true;
		}

		void FPersistentStatsBase::UpdateTimingStat(PersistentStats::ETimingStatNames StatToUpdate)
		{
			if (ensureAlwaysMsgf(IsTimingStatStarted(StatToUpdate), TEXT("Calling UpdateTimingStat on a stat that hasn't been started! %s"), *LexToString(StatToUpdate)))
			{
				FPersistentTimerData& FoundStat = TimingStatsMap.FindOrAdd(LexToString(StatToUpdate));

				const double CurrentTime = FPlatformTime::Seconds();
				const double TimeSinceUpdate = CurrentTime - FoundStat.LastUpdateTime;

				if (ensureAlwaysMsgf((TimeSinceUpdate > 0.f), TEXT("Invalid saved LastUpdateTime for Stat %s! Possible Logic Error!")))
				{
					FoundStat.CurrentValue += TimeSinceUpdate;
				}
				
				FoundStat.LastUpdateTime = CurrentTime;
				bIsDirty = true;
			}
		}

		void FPersistentStatsBase::UpdateAllActiveTimers()
		{
			for (uint8 TimingStatNameIndex = 0; TimingStatNameIndex < (uint8)PersistentStats::ETimingStatNames::NumStatNames; ++TimingStatNameIndex)
			{
				PersistentStats::ETimingStatNames EnumForIndex = (PersistentStats::ETimingStatNames)TimingStatNameIndex;
				if (IsTimingStatStarted(EnumForIndex))
				{
					UpdateTimingStat(EnumForIndex);
				}
			}
		}

		void FPersistentStatsBase::StopAllActiveTimers()
		{
			for (uint8 TimingStatIndex = 0; TimingStatIndex < static_cast<uint8>(ETimingStatNames::NumStatNames); ++TimingStatIndex)
			{
				ETimingStatNames TimingStatAsEnum = static_cast<ETimingStatNames>(TimingStatIndex);
				if (IsTimingStatStarted(TimingStatAsEnum))
				{
					StopTimingStat(TimingStatAsEnum);
				}
			}
		}

		void FPersistentStatsBase::StatsBegin(const FString& ExpectedAnalyticsID, bool bForceResetData /* = false */)
		{
			bIsActive = true;
			
			if (LoadStatsFromDisk())
			{
				OnLoadingDataFromDisk();
			}

			//If our Analytics ID doesn't match our expected we need to reset the data as we have started a new persistent session
			if (bForceResetData || !AnalyticsSessionID.Equals(ExpectedAnalyticsID))
			{
				ResetStats(ExpectedAnalyticsID);
			}
			
			//Immediately save here so we don't risk reloading the same stale data
			//if we don't make it to an update
			SaveStatsToDisk();
		}

		void FPersistentStatsBase::StatsEnd(bool bStopAllActiveTimers /* = true */)
		{
			bIsActive = false;

			if (bStopAllActiveTimers)
			{
				StopAllActiveTimers();
			}

			//Immediately save here as we only look to update active dirty bundles, and since
			//this likely won't be changed anymore we might as well save it out now
			SaveStatsToDisk();
		}

		void FPersistentStatsBase::OnLoadingDataFromDisk()
		{
			HandleTimerStatsAfterDataLoad();
		}

		void FPersistentStatsBase::HandleTimerStatsAfterDataLoad()
		{
			//Go through all timing stats and handle each one accordingly
			//All Active Timers we should stop these timers without updating them so that they don't accrue time from backgrounding
			//All BG timers Should be stopped, but update their timers on stopping so they do accrue time from backgrounding

			/*
					Handle Active (Foreground) Timers
			*/

			if (IsTimingStatStarted(ETimingStatNames::ActiveTotalTime))
			{
				StopTimingStat(ETimingStatNames::ActiveTotalTime, false);
			}

			if (IsTimingStatStarted(ETimingStatNames::ActiveChunkDBDownloadTime))
			{
				StopTimingStat(ETimingStatNames::ActiveChunkDBDownloadTime, false);
			}

			if (IsTimingStatStarted(ETimingStatNames::ActiveInstallTime))
			{
				StopTimingStat(ETimingStatNames::ActiveInstallTime, false);
			}

			if (IsTimingStatStarted(ETimingStatNames::ActivePSOTime))
			{
				StopTimingStat(ETimingStatNames::ActivePSOTime, false);
			}

			/*
					Handle Real Total Timers
			*/
			
			// RealTotalTime should always be updated, it is an attempted tracking of all time (BG and FG) so treat it like a BG timer in this case to accrue background time
			// We stop it here instead of just updating it as we don't want to accrue time until it is started again (just account for the BG time)
			if (IsTimingStatStarted(ETimingStatNames::RealTotalTime))
			{
				StopTimingStat(ETimingStatNames::RealTotalTime, true);
			}
			
			if (IsTimingStatStarted(ETimingStatNames::RealChunkDBDownloadTime))
			{
				StopTimingStat(ETimingStatNames::RealChunkDBDownloadTime, true);
			}

			/*
					Handle Background Timers
			*/
			
			if (IsTimingStatStarted(ETimingStatNames::EstimatedTotalBGTime))
			{
				StopTimingStat(ETimingStatNames::EstimatedTotalBGTime, true);
			}

			if (IsTimingStatStarted(ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime))
			{
				StopTimingStat(ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime, true);
			}

			if (IsTimingStatStarted(ETimingStatNames::EstimatedBGInstallTime))
			{
				StopTimingStat(ETimingStatNames::EstimatedBGInstallTime, true);
			}

			if (IsTimingStatStarted(ETimingStatNames::EstimatedBGPSOTime))
			{
				StopTimingStat(ETimingStatNames::EstimatedBGPSOTime, true);
			}
		}

		void FSessionPersistentStats::AddRequiredBundles(const TArray<FString>& RequiredBundlesToAdd)
		{
			for (const FString& BundleName : RequiredBundlesToAdd)
			{
				RequiredBundles.AddUnique(BundleName);
			}
			bIsDirty = true;
		}

		void FSessionPersistentStats::AddRequiredBundles(const TArray<FName>& RequiredBundlesToAdd)
		{
			for (FName BundleName : RequiredBundlesToAdd)
			{
				RequiredBundles.AddUnique(BundleName.ToString());
			}
			bIsDirty = true;
		}

		void FSessionPersistentStats::GetRequiredBundles(TArray<FString>& OutRequiredBundles)
		{
			OutRequiredBundles.Empty();
			for (const FString& BundleName : RequiredBundles)
			{
				OutRequiredBundles.Add(BundleName);
			}
		}
	
		void FSessionPersistentStats::ResetRequiredBundles(const TArray<FString>& NewRequiredBundles /* = TArray<FString>() */)
		{
			RequiredBundles.Empty();
			AddRequiredBundles(NewRequiredBundles);
			bIsDirty = true;
		}

		const FString FBundlePersistentStats::GetFullPathForStatFile() const
		{
			return FPaths::Combine(FPlatformMisc::GamePersistentDownloadDir(), TEXT("PersistentStats"), TEXT("BundleStats"), (BundleName + TEXT(".json")));
		}

		const FString FSessionPersistentStats::GetFullPathForStatFile() const
		{
			return FPaths::Combine(FPlatformMisc::GamePersistentDownloadDir(), TEXT("PersistentStats"), TEXT("ContentRequestStats"), (SessionName + TEXT(".json")));
		}

		void FPersistentStatContainerBase::InitializeBase()
		{
			//Load Settings from Config
			{
				GConfig->GetBool(TEXT("InstallBundleManager.PersistentStatSettings"), TEXT("ShouldAutoUpdateInTick"), bShouldAutoUpdateInTick, GEngineIni);
				GConfig->GetBool(TEXT("InstallBundleManager.PersistentStatSettings"), TEXT("ShouldAutoUpdateBackgroundStats"), bShouldAutoUpdateBackgroundStats, GEngineIni);
				GConfig->GetBool(TEXT("InstallBundleManager.PersistentStatSettings"), TEXT("bShouldSaveDirtyStatsOnTick"), bShouldSaveDirtyStatsOnTick, GEngineIni);
				GConfig->GetBool(TEXT("InstallBundleManager.PersistentStatSettings"), TEXT("bShouldSaveStatsEveryUpdate"), bShouldSaveStatsEveryUpdate, GEngineIni);

				GConfig->GetFloat(TEXT("InstallBundleManager.PersistentStatSettings"), TEXT("AutoUpdateRate"), TimerStat_ResetTimerValue, GEngineIni);
				ResetTimerUpdate();
			}

			//Setup Delegates (Needs to happen after config to have AutoUpdate settings loaded)
			{
				if ((bShouldAutoUpdateInTick || bShouldSaveDirtyStatsOnTick) && !TickHandle.IsValid())
				{
					TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPersistentStatContainerBase::Tick));
				}

				if (bShouldAutoUpdateBackgroundStats)
				{
					if (!OnApp_EnteringForegroundHandle.IsValid())
					{
						OnApp_EnteringForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FPersistentStatContainerBase::OnApp_EnteringForeground);
					}
					if (!OnApp_EnteringBackgroundHandle.IsValid())
					{
						OnApp_EnteringBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FPersistentStatContainerBase::OnApp_EnteringBackground);
					}
				}
			}
		}

		void FPersistentStatContainerBase::ShutdownBase()
		{
			if (TickHandle.IsValid())
			{
				FTicker::GetCoreTicker().RemoveTicker(TickHandle);
				TickHandle.Reset();
			}

			if (OnApp_EnteringForegroundHandle.IsValid())
			{
				FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(OnApp_EnteringForegroundHandle);
				OnApp_EnteringForegroundHandle.Reset();
			}
			if (OnApp_EnteringBackgroundHandle.IsValid())
			{
				FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(OnApp_EnteringBackgroundHandle);
				OnApp_EnteringBackgroundHandle.Reset();
			}
		}

		bool FPersistentStatContainerBase::Tick(float dt)
		{
			if (bShouldAutoUpdateInTick)
			{
				//Only update all active timers every TimerStat_ResetTimerValue seconds
				TimerStat_UpdateTimer -= dt;
				if (TimerStat_UpdateTimer < 0.0f)
				{
					ResetTimerUpdate();
					UpdateAllBundlesActiveTimers();
					UpdateAllSessionActiveTimers();
				}
			}

			if (bShouldSaveDirtyStatsOnTick)
			{
				//Go ahead and update all dirty stats every tick
				//This way we don't save them multiple times in a single tick
				//for updating multiple stats
				SaveAllDirtyStatsToDisk();
			}
			//go ahead and always Tick once we start
			return true;
		}

		void FPersistentStatContainerBase::OnDataUpdatedForStat(FPersistentStatsBase& UpdatedBundleStat)
		{
			if (bShouldSaveStatsEveryUpdate)
			{
				UpdatedBundleStat.SaveStatsToDisk();
			}
		}

		void FPersistentStatContainerBase::ResetTimerUpdate()
		{
			TimerStat_UpdateTimer = TimerStat_ResetTimerValue;
		}

		void FPersistentStatContainerBase::SaveAllDirtyStatsToDisk()
		{
			TArray<FName> AllBundleStatNames;
			PerBundlePersistentStatMap.GetKeys(AllBundleStatNames);
			for (FName& BundleName : AllBundleStatNames)
			{
				FBundlePersistentStats* BundleStats = PerBundlePersistentStatMap.Find(BundleName);
				check(BundleStats);
				if (BundleStats->IsDirty())
				{
					BundleStats->SaveStatsToDisk();
				}
			}

			TArray<FString> AllSessionStatNames;
			SessionPersistentStatMap.GetKeys(AllSessionStatNames);
			for (const FString& SessionName : AllSessionStatNames)
			{
				FSessionPersistentStats* SessionStats = SessionPersistentStatMap.Find(SessionName);
				check(SessionStats);
				if (SessionStats->IsDirty())
				{
					SessionStats->SaveStatsToDisk();
				}
			}			
		}



		void FPersistentStatContainerBase::StartBundlePersistentStatTracking(FName BundleName, const FString& ExpectedAnalyticsID /* = FString() */, bool bForceResetStatData /* = false */)
		{
			//Use the base expected analytics ID if one was not passed in
			const FString ExpectedAnalyticsToUse = ExpectedAnalyticsID.IsEmpty() ? FPersistentStatsBase::GetBaseExpectedAnalyticsID() : ExpectedAnalyticsID;

			FBundlePersistentStats& FoundBundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
			FoundBundleStats.StatsBegin(ExpectedAnalyticsToUse, bForceResetStatData);

			OnDataUpdatedForStat(FoundBundleStats);
		}

		void FPersistentStatContainerBase::StartSessionPersistentStatTracking(const FString& SessionName, const TArray<FName>& RequiredBundles /* = TArray<FName>() */, const FString& ExpectedAnalyticsID /* = FString() */, bool bForceResetStatData /* = false */)
		{
			//Use the base expected analytics ID if one was not passed in
			const FString ExpectedAnalyticsToUse = ExpectedAnalyticsID.IsEmpty() ? FPersistentStatsBase::GetBaseExpectedAnalyticsID() : ExpectedAnalyticsID;

			FSessionPersistentStats& FoundSessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
			FoundSessionStats.StatsBegin(ExpectedAnalyticsToUse, bForceResetStatData);

			//Also append starting required bundles as we may have new ones from the ones already in data
			FoundSessionStats.AddRequiredBundles(RequiredBundles);

			OnDataUpdatedForStat(FoundSessionStats);
		}
		
		void FPersistentStatContainerBase::StopBundlePersistentStatTracking(FName BundleName, bool bStopAllActiveTimers /* = true */)
		{
			FBundlePersistentStats* FoundBundleStats = PerBundlePersistentStatMap.Find(BundleName);
			if (nullptr != FoundBundleStats)
			{
				FoundBundleStats->StatsEnd(bStopAllActiveTimers);
				OnDataUpdatedForStat(*FoundBundleStats);
			}
		}

		void FPersistentStatContainerBase::StopSessionPersistentStatTracking(FString SessionName, bool bStopAllActiveTimers /* = true */)
		{
			FSessionPersistentStats* FoundSessionStats = SessionPersistentStatMap.Find(SessionName);
			if (nullptr != FoundSessionStats)
			{
				FoundSessionStats->StatsEnd(bStopAllActiveTimers);
				OnDataUpdatedForStat(*FoundSessionStats);
			}
		}

		void FPersistentStatContainerBase::StartBundlePersistentStatTimer(FName BundleName, ETimingStatNames TimerToStart)
		{
			FBundlePersistentStats& FoundBundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
			FoundBundleStats.StartTimingStat(TimerToStart);

			OnDataUpdatedForStat(FoundBundleStats);
		}

		void FPersistentStatContainerBase::StartSessionPersistentStatTimer(FString SessionName, ETimingStatNames TimerToStart)
		{
			FSessionPersistentStats& FoundSessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
			FoundSessionStats.StartTimingStat(TimerToStart);

			OnDataUpdatedForStat(FoundSessionStats);
		}

		void FPersistentStatContainerBase::StopBundlePersistentStatTimer(FName BundleName, ETimingStatNames TimerToStop)
		{
			FBundlePersistentStats& FoundBundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
			FoundBundleStats.StopTimingStat(TimerToStop);

			OnDataUpdatedForStat(FoundBundleStats);
		}

		void FPersistentStatContainerBase::StopSessionPersistentStatTimer(FString SessionName, ETimingStatNames TimerToStop)
		{
			FSessionPersistentStats& FoundSessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
			FoundSessionStats.StopTimingStat(TimerToStop);

			OnDataUpdatedForStat(FoundSessionStats);
		}

		void FPersistentStatContainerBase::UpdateBundlePersistentStatTimer(FName BundleName, ETimingStatNames TimerToUpdate)
		{
			FBundlePersistentStats& FoundBundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
			FoundBundleStats.UpdateTimingStat(TimerToUpdate);

			OnDataUpdatedForStat(FoundBundleStats);
		}

		void FPersistentStatContainerBase::UpdateSessionPersistentStatTimer(FString SessionName, ETimingStatNames TimerToUpdate)
		{
			FSessionPersistentStats& FoundSessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
			FoundSessionStats.UpdateTimingStat(TimerToUpdate);

			OnDataUpdatedForStat(FoundSessionStats);
		}

		void FPersistentStatContainerBase::IncrementBundlePersistentCounter(FName BundleName, ECountStatNames CounterToUpdate)
		{
			FBundlePersistentStats& FoundBundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
			FoundBundleStats.IncrementCountStat(CounterToUpdate);

			OnDataUpdatedForStat(FoundBundleStats);
		}

		void FPersistentStatContainerBase::IncrementSessionPersistentCounter(FString SessionName, ECountStatNames CounterToUpdate)
		{
			FSessionPersistentStats& FoundSessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
			FoundSessionStats.IncrementCountStat(CounterToUpdate);

			OnDataUpdatedForStat(FoundSessionStats);
		}

		void FPersistentStatContainerBase::OnApp_EnteringBackground()
		{
			OnBackground_HandleBundleStats();
			OnBackground_HandleSessionStats();
		}

		void FPersistentStatContainerBase::OnApp_EnteringForeground()
		{
			OnForeground_HandleBundleStats();
			OnForeground_HandleSessionStats();
		}

		void FPersistentStatContainerBase::OnBackground_HandleBundleStats()
		{
			for (TPair<FName, FBundlePersistentStats>& BundlePair : PerBundlePersistentStatMap)
			{
				//Only bother updating bundles listed as active
				if (BundlePair.Value.IsActive())
				{
					UpdateStatsForBackground(BundlePair.Value);
				}
			}
		}

		void FPersistentStatContainerBase::OnForeground_HandleBundleStats()
		{
			for (TPair<FName, FBundlePersistentStats>& BundlePair : PerBundlePersistentStatMap)
			{
				//Only bother updating bundles listed as active
				if (BundlePair.Value.IsActive())
				{
					UpdateStatsForForeground(BundlePair.Value);
				}
			}
		}

		void FPersistentStatContainerBase::OnBackground_HandleSessionStats()
		{
			for (TPair<FString, FSessionPersistentStats>& SessionPair : SessionPersistentStatMap)
			{
				//Only bother updating sessions listed as active
				if (SessionPair.Value.IsActive())
				{
					UpdateStatsForBackground(SessionPair.Value);
				}
			}
		}

		void FPersistentStatContainerBase::OnForeground_HandleSessionStats()
		{
			for (TPair<FString, FSessionPersistentStats>& SessionPair : SessionPersistentStatMap)
			{
				//Only bother updating sessions listed as active
				if (SessionPair.Value.IsActive())
				{
					UpdateStatsForForeground(SessionPair.Value);
				}
			}
		}

		void FPersistentStatContainerBase::UpdateStatsForBackground(FPersistentStatsBase& StatToUpdate)
		{
			StatToUpdate.IncrementCountStat(ECountStatNames::NumBackgrounded);

			//Always handle ActiveTotalTime as this isn't dependent on what stage of the process we are in
			if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::ActiveTotalTime))
			{
				StatToUpdate.StartTimingStat(ETimingStatNames::EstimatedTotalBGTime);
				StatToUpdate.StopTimingStat(ETimingStatNames::ActiveTotalTime);
			}

			//Besides the ActiveTotalTime above, we should only be in 1 of the following states at a time, so only handle the appropriate swap
			if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::ActiveChunkDBDownloadTime))
			{
				StatToUpdate.StartTimingStat(ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime);
				StatToUpdate.StopTimingStat(ETimingStatNames::ActiveChunkDBDownloadTime);
			}
			else if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::ActiveInstallTime))
			{
				StatToUpdate.StartTimingStat(ETimingStatNames::EstimatedBGInstallTime);
				StatToUpdate.StopTimingStat(ETimingStatNames::ActiveInstallTime);
			}
			else if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::ActivePSOTime))
			{
				StatToUpdate.StartTimingStat(ETimingStatNames::EstimatedBGPSOTime);
				StatToUpdate.StopTimingStat(ETimingStatNames::ActivePSOTime);
			}

			OnDataUpdatedForStat(StatToUpdate);
		}

		void FPersistentStatContainerBase::UpdateStatsForForeground(FPersistentStatsBase& StatToUpdate)
		{
			StatToUpdate.IncrementCountStat(ECountStatNames::NumResumedFromBackground);

			//Always handle ActiveTotalTime as this isn't dependent on what stage of the process we are in
			if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::EstimatedTotalBGTime))
			{
				StatToUpdate.StopTimingStat(ETimingStatNames::EstimatedTotalBGTime);
				StatToUpdate.StartTimingStat(ETimingStatNames::ActiveTotalTime);
			}

			//Besides the ActiveTotalTime above, we should only be in 1 of the following states at a time, so only handle the appropriate swap
			if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime))
			{
				StatToUpdate.StopTimingStat(ETimingStatNames::EstimatedBackgroundChunkDBDownloadTime);
				StatToUpdate.StartTimingStat(ETimingStatNames::ActiveChunkDBDownloadTime);
			}
			else if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::EstimatedBGInstallTime))
			{
				StatToUpdate.StopTimingStat(ETimingStatNames::EstimatedBGInstallTime);
				StatToUpdate.StartTimingStat(ETimingStatNames::ActiveInstallTime);
			}
			else if (StatToUpdate.IsTimingStatStarted(ETimingStatNames::EstimatedBGPSOTime))
			{
				StatToUpdate.StopTimingStat(ETimingStatNames::EstimatedBGPSOTime);
				StatToUpdate.StartTimingStat(ETimingStatNames::ActivePSOTime);
			}

			OnDataUpdatedForStat(StatToUpdate);
		}

		void FPersistentStatContainerBase::UpdateAllBundlesActiveTimers()
		{
			TArray<FName> BundleNames;
			PerBundlePersistentStatMap.GetKeys(BundleNames);
			
			for (FName BundleName : BundleNames)
			{
				InstallBundleUtil::PersistentStats::FBundlePersistentStats& BundleStats = PerBundlePersistentStatMap.FindOrAdd(BundleName, FBundlePersistentStats(BundleName));
				BundleStats.UpdateAllActiveTimers();

				OnDataUpdatedForStat(BundleStats);
			}
		}

		void FPersistentStatContainerBase::UpdateAllSessionActiveTimers()
		{
			TArray<FString> SessionNames;
			SessionPersistentStatMap.GetKeys(SessionNames);

			for (const FString& SessionName : SessionNames)
			{
				InstallBundleUtil::PersistentStats::FSessionPersistentStats& SessionStats = SessionPersistentStatMap.FindOrAdd(SessionName, FSessionPersistentStats(SessionName));
				SessionStats.UpdateAllActiveTimers();

				OnDataUpdatedForStat(SessionStats);
			}
		}

		const FString FPersistentStatsBase::GetBaseExpectedAnalyticsID()
		{
			const FString BaseExpectedAnalyticsID = FPlatformMisc::GetDeviceId() + TEXT("_") + FApp::GetBuildVersion();
			return BaseExpectedAnalyticsID;
		}

		const FBundlePersistentStats* FPersistentStatContainerBase::GetBundleStat(FName BundleName) const
		{
			return PerBundlePersistentStatMap.Find(BundleName);
		}

		const FSessionPersistentStats* FPersistentStatContainerBase::GetSessionStat(const FString& SessionName) const
		{
			return SessionPersistentStatMap.Find(SessionName);
		}
	}
}
