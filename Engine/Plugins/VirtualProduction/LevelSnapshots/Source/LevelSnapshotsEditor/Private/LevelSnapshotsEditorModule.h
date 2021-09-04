// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWidget.h"

class ULevelSnapshotsEditorProjectSettings;
class ULevelSnapshotsEditorDataManagementSettings;
class FToolBarBuilder;
class FLevelSnapshotsEditorToolkit;
class ULevelSnapshotsEditorData;

class FLevelSnapshotsEditorModule : public IModuleInterface
{
public:

	static FLevelSnapshotsEditorModule& Get();
	static void OpenLevelSnapshotsSettings();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	bool GetUseCreationForm() const;
	void SetUseCreationForm(bool bInUseCreationForm);
	void ToggleUseCreationForm() { SetUseCreationForm(!GetUseCreationForm()); }

	void OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData);

	TWeakObjectPtr<ULevelSnapshotsEditorProjectSettings> GetLevelSnapshotsUserSettings() const { return ProjectSettingsObjectPtr; }
	TWeakObjectPtr<ULevelSnapshotsEditorDataManagementSettings> GetLevelSnapshotsDataManagementSettings() const { return DataMangementSettingsObjectPtr; }

private:
	
	void PostEngineInit();

	void SetupReminderToEnableSupportPlugins();

	void RegisterMenuItem();
	bool RegisterProjectSettings();
	bool HandleModifiedProjectSettings();
	
	void RegisterEditorToolbar();
	void MapEditorToolbarActions();
	void CreateEditorToolbarButton(FToolBarBuilder& Builder);
	TSharedRef<SWidget> FillEditorToolbarComboButtonMenuOptions(TSharedPtr<class FUICommandList> Commands);

	void OpenSnapshotsEditor();
	ULevelSnapshotsEditorData* AllocateTransientPreset();

	
	/** Command list (for combo button sub menu options) */
	TSharedPtr<FUICommandList> EditorToolbarButtonCommandList;

	/** Lives for as long as the UI is open. */
	TWeakPtr<FLevelSnapshotsEditorToolkit> SnapshotEditorToolkit;

	TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorProjectSettings> ProjectSettingsObjectPtr;
	
	TSharedPtr<ISettingsSection> DataMangementSettingsSectionPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorDataManagementSettings> DataMangementSettingsObjectPtr;
};
