// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SEmptyTab.h"
#include "Widgets/SGameSyncTab.h"
#include "Widgets/SWorkspaceWindow.h"

#include "UGSCore/Workspace.h"
#include "UGSCore/DetectProjectSettingsTask.h"
#include "UGSCore/UserSettings.h"

class UGSTab
{
public:
	UGSTab();  // Todo: change all _Args to InArgs

	const TSharedRef<SDockTab> GetTabWidget(); // Todo: check const-correctness of this (is the pointer const or is what it's pointing to const)

	void SetTabArgs(FSpawnTabArgs InTabArgs);
	FSpawnTabArgs GetTabArgs() const;

	// Slate callbacks
	bool OnWorkspaceChosen(const FString& Project);
	void OnSyncLatest();
	bool IsSyncing() const;
private:

	// Core functions
	void SetupWorkspace();

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
};
