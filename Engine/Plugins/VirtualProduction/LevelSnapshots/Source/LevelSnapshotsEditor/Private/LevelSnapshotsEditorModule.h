// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWidget.h"

class FToolBarBuilder;
class FLevelSnapshotsEditorToolkit;
class ULevelSnapshotsEditorData;

class FLevelSnapshotsEditorModule : public IModuleInterface
{
public:

	static FLevelSnapshotsEditorModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	bool GetUseCreationForm() const
	{
		return bUseCreationForm;
	}

	void SetUseCreationForm(bool bInUseCreationForm)
	{
		bUseCreationForm = bInUseCreationForm;
	}

	void ToggleUseCreationForm()
	{
		SetUseCreationForm(!GetUseCreationForm());
	}

	void CallTakeSnapshot();

private:
	
	void RegisterMenus();
	
	void RegisterEditorToolbar();
	void MapEditorToolbarActions();
	void CreateEditorToolbarButton(FToolBarBuilder& Builder);
	TSharedRef<SWidget> FillEditorToolbarComboButtonMenuOptions(TSharedPtr<class FUICommandList> Commands);

	
	void OpenSnapshotsEditor();
	void OpenLevelSnapshotsSettings();

	ULevelSnapshotsEditorData* AllocateTransientPreset();

	// Command list (for combo button sub menu options)
	TSharedPtr<FUICommandList> EditorToolbarButtonCommandList;

	/* Lives for as long as the UI is open. */
	TWeakPtr<FLevelSnapshotsEditorToolkit> SnapshotEditorToolkit;

	bool bUseCreationForm = false;
};