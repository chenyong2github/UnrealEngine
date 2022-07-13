// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SEmptyTab.h"
#include "Widgets/SGameSyncTab.h"
#include "Widgets/SWorkspaceWindow.h"

#include "UGSCore/Workspace.h"

struct FUserWorkspaceSettings;
struct FUserProjectSettings;
struct FUserSettings;
class FDetectProjectSettingsTask;
class FPerforceMonitor;
class FEventMonitor;

class UGSTab
{
public:
	UGSTab();  // Todo: change all _Args to InArgs

	const TSharedRef<SDockTab> GetTabWidget(); // Todo: check const-correctness of this (is the pointer const or is what it's pointing to const)

	void SetTabArgs(FSpawnTabArgs InTabArgs);
	FSpawnTabArgs GetTabArgs() const;

	void Tick();

	// Slate callbacks
	bool OnWorkspaceChosen(const FString& Project);
	void OnSyncLatest();
	bool IsSyncing() const;
	FString GetSyncProgress() const;
	const TArray<FString>& GetSyncFilters() const; // Todo: is return type okay?
	const TArray<FString>& GetCombinedSyncFilter() const; // Todo: is return type okay?
private:

	void OnWorkspaceSyncComplete(
		TSharedRef<FWorkspaceUpdateContext, ESPMode::ThreadSafe> WorkspaceContext,
		EWorkspaceUpdateResult SyncResult,
		const FString& StatusMessage);

	// Allows the queuing of functions from threads to be run on the main thread
	void QueueMessageForMainThread(TFunction<void()> Function);

	bool ShouldIncludeInReviewedList(const TSet<int>& PromotedChangeNumbers, int ChangeNumber) const;
	void UpdateGameTabBuildList();

	// Core functions
	void SetupWorkspace();

	FCriticalSection CriticalSection;

	// Slate Data
	FSpawnTabArgs TabArgs;
	TSharedRef<SDockTab> TabWidget;
	TSharedRef<SEmptyTab> EmptyTabView;
	TSharedRef<SGameSyncTab> GameSyncTabView;

	// Core data
	FString ProjectFileName;
	TSharedPtr<FWorkspace> Workspace;
	TSharedPtr<FPerforceConnection> PerforceClient;
	TSharedPtr<FUserWorkspaceSettings> WorkspaceSettings;
	TSharedPtr<FUserProjectSettings> ProjectSettings;
	EWorkspaceUpdateOptions Options;
	TSharedPtr<FDetectProjectSettingsTask> DetectSettings;
	TArray<FString> CombinedSyncFilter;
	TSharedPtr<FUserSettings> UserSettings;

	// Monitoring threads
	TSharedPtr<FPerforceMonitor> PerforceMonitor;
	TSharedPtr<FEventMonitor> EventMonitor;

	// Queue for handling callbacks from multiple threads that need to be called on the main thread
	TArray<TFunction<void()>> MessageQueue;
	std::atomic<bool> bHasQueuedMessages;
};
