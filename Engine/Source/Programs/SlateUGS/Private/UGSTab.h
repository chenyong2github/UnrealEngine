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
class UGSTabManager;

enum SyncCategoryType
{
	CurrentWorkspace,
	AllWorkspaces
};

class UGSTab
{
public:
	UGSTab();
	void Initialize();

	const TSharedRef<SDockTab> GetTabWidget();
	FSpawnTabArgs GetTabArgs() const;

	void SetTabManager(UGSTabManager* InTabManager);
	void SetTabArgs(FSpawnTabArgs InTabArgs);

	void Tick();

	// Slate callbacks
	bool OnWorkspaceChosen(const FString& Project);
	void OnSyncChangelist(int Changelist);
	void OnSyncLatest();
	void OnSyncFilterWindowSaved(
		const TArray<FString>& SyncViewCurrent,
		const TArray<FGuid>& SyncExcludedCategoriesCurrent,
		const TArray<FString>& SyncViewAll,
		const TArray<FGuid>& SyncExcludedCategoriesAll);

	// Accessors
	bool IsSyncing() const;
	FString GetSyncProgress() const;
	const TArray<FString>& GetSyncFilters() const;
	const TArray<FString>& GetCombinedSyncFilter() const;
	TArray<FWorkspaceSyncCategory> GetSyncCategories(SyncCategoryType CategoryType) const;
	TArray<FString> GetSyncViews(SyncCategoryType CategoryType) const;
	UGSTabManager* GetTabManager();
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
	UGSTabManager* TabManager = nullptr;
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
