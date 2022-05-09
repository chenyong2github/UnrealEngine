// Copyright Epic Games, Inc. All Rights Reserved.

#include "IUndoHistoryEditorModule.h"
#include "Styling/AppStyle.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SUndoHistory.h"
#include "Widgets/Docking/SDockTab.h"

const FName UndoHistoryTabName("UndoHistory");

class FUndoHistoryEditorModule : public IUndoHistoryEditorModule
{
public:

	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(UndoHistoryTabName, FOnSpawnTab::CreateRaw(this, &FUndoHistoryEditorModule::HandleSpawnSettingsTab))
			.SetDisplayName(NSLOCTEXT("FUndoHistoryModule", "UndoHistoryTabTitle", "Undo History"))
			.SetTooltipText(NSLOCTEXT("FUndoHistoryModule", "UndoHistoryTooltipText", "Open the Undo History tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "UndoHistory.TabIcon"))
			.SetAutoGenerateMenuEntry(false);
	}
	
	virtual void ShutdownModule() override
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UndoHistoryTabName);
	}
	
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	virtual void ExecuteOpenUndoHistory() override
	{
		FGlobalTabmanager::Get()->TryInvokeTab(UndoHistoryTabName);
	}

private:
	
	/** Handles creating the project settings tab. */
	TSharedRef<SDockTab> HandleSpawnSettingsTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
		[
			SNew(SUndoHistory)
		];
		return DockTab;
	}
};

IMPLEMENT_MODULE(FUndoHistoryEditorModule, UndoHistoryEditor);

