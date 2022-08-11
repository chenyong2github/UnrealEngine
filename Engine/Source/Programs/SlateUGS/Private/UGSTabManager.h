// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSTab.h"
#include "ScheduledSyncTimer.h"

namespace UGSCore
{
	struct FUserSettings;
}

class UGSTabManager
{
public:
	UGSTabManager();
	void ConstructTabs();

	void Tick();

	TSharedRef<SDockTab> SpawnTab(int Index, const FSpawnTabArgs& Args);
	void ActivateTab();
	FName GetTabId(int TabIndex) const;

private:
	void SetupScheduledSync();
	void StartScheduledSyncTimer();
	void StopScheduledSyncTimer();
	void ScheduleTimerElapsed();

	static constexpr int MaxTabs = 10;
	TStaticArray<UGSTab, MaxTabs> Tabs;

	TSharedPtr<UGSCore::FUserSettings> UserSettings;

	std::atomic<bool> bScheduledTimerElapsed;
	ScheduledSyncTimer SyncTimer;
};
