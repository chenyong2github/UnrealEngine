// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ISourceControlWindowsModule.h"
#include "ISourceControlModule.h"

#include "Widgets/Docking/SDockTab.h"
#include "Textures/SlateIcon.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "LevelEditor.h"

#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "SSourceControlChangelists.h"
#include "UncontrolledChangelistsModule.h"

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
	virtual bool CanShowChangelistsTab() const override;

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

	// We're going to call a static function in the editor style module, so we need to make sure the module has actually been loaded
	FModuleManager::Get().LoadModuleChecked("EditorStyle");

	// Create a Source Control group under the Tools category
	const FSlateIcon SourceControlIcon(FEditorStyle::GetStyleSetName(), "SourceControl.ChangelistsTab");

	// Register the changelist tab spawner
	FGlobalTabmanager::Get()->RegisterTabSpawner(SourceControlChangelistsTabName, FOnSpawnTab::CreateRaw(this, &FSourceControlWindowsModule::CreateChangelistsTab))
		.SetDisplayName(LOCTEXT("ChangelistsTabTitle", "View Changelists"))
		.SetTooltipText(LOCTEXT("ChangelistsTabTooltip", "Opens a dialog displaying current changelists."))
		.SetIcon(SourceControlIcon);

#if WITH_RELOAD
	// This code attempts to relaunch the GameplayCueEditor tab when you reload this module
	if (IsReloadActive() && FSlateApplication::IsInitialized())
	{
		ShowChangelistsTab();
	}
#endif // WITH_RELOAD
}

void FSourceControlWindowsModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterTabSpawner(SourceControlChangelistsTabName);

		if (ChangelistsTab.IsValid())
		{
			ChangelistsTab.Pin()->RequestCloseTab();
		}
	}
}

TSharedRef<SDockTab> FSourceControlWindowsModule::CreateChangelistsTab(const FSpawnTabArgs & Args)
{
	return SAssignNew(ChangelistsTab, SDockTab)
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

bool FSourceControlWindowsModule::CanShowChangelistsTab() const
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();

	return (SourceControlModule.IsEnabled() && SourceControlModule.GetProvider().IsAvailable()) || FUncontrolledChangelistsModule::Get().IsEnabled();
}

#undef LOCTEXT_NAMESPACE
