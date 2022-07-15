// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectMixerEditorModule.h"

#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Framework/Docking/TabManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FObjectMixerEditorModule"

IMPLEMENT_MODULE(FObjectMixerEditorModule, ObjectMixerEditor)

const FName FObjectMixerEditorModule::ObjectMixerToolkitPanelTabId(TEXT("ObjectMixerToolkit"));

void FObjectMixerEditorModule::StartupModule()
{
	FObjectMixerEditorStyle::Initialize();

	RegisterMenuGroupAndTabSpawner();
	RegisterProjectSettings();
	
	TabLabel = LOCTEXT("ObjectMixerTabLabel", "Object Mixer");

	// Initialize Light Mixer
	FModuleManager::Get().LoadModuleChecked("LightMixer");
}

void FObjectMixerEditorModule::ShutdownModule()
{
	FObjectMixerEditorStyle::Shutdown();
	
	MainPanel.Reset();

	UnregisterTabSpawner();
	UnregisterProjectSettings();
}

FObjectMixerEditorModule& FObjectMixerEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked< FObjectMixerEditorModule >("ObjectMixerEditor");
}

TSharedPtr<SWidget> FObjectMixerEditorModule::MakeObjectMixerDialog() const
{
	if (MainPanel.IsValid())
	{
		return MainPanel->GetOrCreateWidget();
	}

	return nullptr;
}

void FObjectMixerEditorModule::RebuildList(const FString InItemToScrollTo, bool bShouldCacheValues) const
{
	MainPanel->RebuildList(InItemToScrollTo, bShouldCacheValues);
}

void FObjectMixerEditorModule::RefreshList() const
{
	MainPanel->RefreshList();
}

void FObjectMixerEditorModule::RegisterMenuGroupAndTabSpawner()
{
	const FText MenuItemName = LOCTEXT("OpenObjectMixerEditorMenuItem", "Object Mixer");
	const FSlateIcon MenuItemIcon =
		FSlateIcon(FObjectMixerEditorStyle::Get().GetStyleSetName(), "ObjectMixer.ToolbarButton", "ObjectMixer.ToolbarButton.Small");
	
	FTabSpawnerEntry& BrowserSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		FObjectMixerEditorModule::ObjectMixerToolkitPanelTabId,
		FOnSpawnTab::CreateRaw(&FObjectMixerEditorModule::Get(), & FObjectMixerEditorModule::SpawnMainPanelTab))
		.SetIcon(MenuItemIcon)
		.SetDisplayName(MenuItemName)
		.SetTooltipText(LOCTEXT("OpenObjectMixerEditorMenuItem", "Open an Object Mixer instance."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	WorkspaceGroup = WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory()->AddGroup(MenuItemName, MenuItemIcon);

	BrowserSpawnerEntry.SetGroup(WorkspaceGroup.ToSharedRef());
}

bool FObjectMixerEditorModule::RegisterItemInMenuGroup(FWorkspaceItem& InItem)
{
	if (WorkspaceGroup)
	{
		WorkspaceGroup->AddItem(MakeShareable(&InItem));
		
		return true;
	}

	return false;
}

void FObjectMixerEditorModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FObjectMixerEditorModule::ObjectMixerToolkitPanelTabId);
}

void FObjectMixerEditorModule::RegisterProjectSettings() const
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		// User Project Settings
		const TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr = SettingsModule->RegisterSettings(
			"Project", "Editor", "Object Mixer",
			LOCTEXT("ObjectMixerSettingsCategoryDisplayName", "Object Mixer"),
			LOCTEXT("ObjectMixerSettingsDescription", "Configure Object Mixer user settings"),
			GetMutableDefault<UObjectMixerEditorProjectSettings>());	
	}
}

void FObjectMixerEditorModule::UnregisterProjectSettings() const
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Object Mixer");
	}
}

TSharedRef<SDockTab> FObjectMixerEditorModule::SpawnMainPanelTab(const FSpawnTabArgs& Args)
{
	MainPanel = MakeShared<FObjectMixerEditorMainPanel>();
	
	const TSharedRef<SDockTab> DockTab =
		SNew(SDockTab)
		.Label(TabLabel)
		.TabRole(ETabRole::NomadTab)
	;
	const TSharedPtr<SWidget> ObjectMixerDialog = MakeObjectMixerDialog();
	DockTab->SetContent(ObjectMixerDialog ? ObjectMixerDialog.ToSharedRef() : SNullWidget::NullWidget);
	MainPanel->OnClassSelectionChanged(DefaultFilterClass);
	MainPanel->RebuildList();
			
	return DockTab;
}

TSharedPtr<FWorkspaceItem> FObjectMixerEditorModule::GetWorkspaceGroup()
{
	return WorkspaceGroup;
}

#undef LOCTEXT_NAMESPACE
