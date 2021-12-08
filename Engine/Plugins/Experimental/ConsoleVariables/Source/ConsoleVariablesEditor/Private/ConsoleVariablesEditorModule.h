// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorCommandInfo.h"
#include "MultiUser/ConsoleVariableSync.h"

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleInterface.h"
#include "Widgets/Docking/SDockTab.h"

struct FAssetData;

class FConsoleVariablesEditorMainPanel;
class FConsoleVariablesEditorToolkit;
class ISettingsSection;

class FConsoleVariablesEditorModule : public IModuleInterface
{
public:
	
	static FConsoleVariablesEditorModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	void OpenConsoleVariablesDialogWithAssetSelected(const FAssetData& InAssetData);

	/** Find all console variables and cache their startup values */
	void QueryAndBeginTrackingConsoleVariables();

	/** Find a tracked console variable by the command string with optional case sensitivity. */
	TWeakPtr<FConsoleVariablesEditorCommandInfo> FindCommandInfoByName(const FString& NameToSearch, ESearchCase::Type InSearchCase = ESearchCase::IgnoreCase);

	/** Find a tracked console variable by its variable reference. */
	TWeakPtr<FConsoleVariablesEditorCommandInfo> FindCommandInfoByConsoleVariableReference(IConsoleVariable* InVariableReference);
	
	[[nodiscard]] TObjectPtr<UConsoleVariablesAsset> GetEditingAsset() const;
	void SetEditingAsset(const TObjectPtr<UConsoleVariablesAsset> InEditingAsset);

	void SendMultiUserConsoleVariableChange(const FString& InVariableName, const FString& InValueAsString);
	
private:

	void OnFEngineLoopInitComplete();

	void RegisterMenuItem();
	void RegisterProjectSettings();

	void OnConsoleVariableChanged(IConsoleVariable* ChangedVariable);

	TObjectPtr<UConsoleVariablesAsset> AllocateTransientPreset();

	TSharedRef<SDockTab> SpawnMainPanelTab(const FSpawnTabArgs& Args);

	void OpenConsoleVariablesEditor();
	
	static const FName ConsoleVariablesToolkitPanelTabId;

	/* Lives for as long as the module is loaded. */
	TSharedPtr<FConsoleVariablesEditorMainPanel> MainPanel;

	// Transient preset that's being edited so we don't affect the reference asset unless we save it
	TObjectPtr<UConsoleVariablesAsset> EditingAsset = nullptr;

	/** All tracked variables and their default, startup, and current values */
	TArray<TSharedPtr<FConsoleVariablesEditorCommandInfo>> ConsoleVariablesMasterReference;
};
