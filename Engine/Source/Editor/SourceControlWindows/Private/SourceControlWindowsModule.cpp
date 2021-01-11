// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ISourceControlWindowsModule.h"

#include "Widgets/Docking/SDockTab.h"
#include "Textures/SlateIcon.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "LevelEditor.h"

#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "SSourceControlChangelists.h"

#define LOCTEXT_NAMESPACE "SourceControlWindows"

/**
 * SourceControlWindows module
 */
class FSourceControlWindowsModule : public ISourceControlWindowsModule
{
public:
	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override;

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override;

	virtual void ShowChangelistsTab() override;

private:
	TSharedRef<SDockTab> CreateChangelistsTab(const FSpawnTabArgs& Args);
	TSharedPtr<SWidget> CreateChangelistsUI();

private:
	TWeakPtr<SDockTab> ChangelistsTab;
	TWeakPtr<SSourceControlChangelistsWidget> ChangelistsWidget;
};

IMPLEMENT_MODULE(FSourceControlWindowsModule, SourceControlWindows);

static const FName SourceControlChangelistsTabName = FName(TEXT("SourceControlChangelists"));

void FSourceControlWindowsModule::StartupModule()
{
	ISourceControlWindowsModule::StartupModule();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SourceControlChangelistsTabName, FOnSpawnTab::CreateRaw(this, &FSourceControlWindowsModule::CreateChangelistsTab))
		.SetDisplayName(LOCTEXT("ChangelistsTabTitle", "Changelists"));

#if WITH_HOT_RELOAD
	// This code attempts to relaunch the GameplayCueEditor tab when you hotreload this module
	if (GIsHotReload && FSlateApplication::IsInitialized())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		LevelEditorTabManager->TryInvokeTab(SourceControlChangelistsTabName);
	}
#endif // WITH_HOT_RELOAD
}

void FSourceControlWindowsModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SourceControlChangelistsTabName);

		if (ChangelistsTab.IsValid())
		{
			ChangelistsTab.Pin()->RequestCloseTab();
		}
	}
}

TSharedRef<SDockTab> FSourceControlWindowsModule::CreateChangelistsTab(const FSpawnTabArgs & Args)
{
	return SAssignNew(ChangelistsTab, SDockTab)
		.Icon(FEditorStyle::Get().GetBrush("SourceControl.ChangelistsTab"))
		.TabRole(ETabRole::NomadTab)
		[
			CreateChangelistsUI().ToSharedRef()
		];
}

TSharedPtr<SWidget> FSourceControlWindowsModule::CreateChangelistsUI()
{
	TSharedPtr<SWidget> ReturnWidget;
	if (IsInGameThread())
	{
		TSharedPtr<SSourceControlChangelistsWidget> SharedPtr = SNew(SSourceControlChangelistsWidget);
		ReturnWidget = SharedPtr;
		ChangelistsWidget = SharedPtr;
	}
	return ReturnWidget;
}

void FSourceControlWindowsModule::ShowChangelistsTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(SourceControlChangelistsTabName));
}

#undef LOCTEXT_NAMESPACE
