// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectMixerEditorModule.h"

#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FObjectMixerEditorModule"

IMPLEMENT_MODULE(FObjectMixerEditorModule, ObjectMixerEditor)

void FObjectMixerEditorModule::StartupModule()
{
	FObjectMixerEditorStyle::Initialize();

	// In the future, Object Mixer and Light Mixer tabs may go into an Object Mixer group
	//RegisterMenuGroup();
	
	Initialize();
}

void FObjectMixerEditorModule::ShutdownModule()
{
	FObjectMixerEditorStyle::Shutdown();

	UnregisterMenuGroup();
	
	Teardown();
}

void FObjectMixerEditorModule::Initialize()
{
	DelegateHandles.Add(FEditorDelegates::MapChange.AddLambda([this](uint32 Index)
	{
		RequestRebuildList();
	}));
	
	SetupMenuItemVariables();
	
	RegisterTabSpawner();
	RegisterProjectSettings();
}

void FObjectMixerEditorModule::Teardown()
{
	// Unbind Delegates
	FEditorDelegates::MapChange.RemoveAll(this);

	for (FDelegateHandle& Delegate : DelegateHandles)
	{
		Delegate.Reset();
	}
	DelegateHandles.Empty();
	
	MainPanel.Reset();

	UToolMenus::UnregisterOwner(this);
	
	UnregisterTabSpawner();
	UnregisterProjectSettings();
}

void FObjectMixerEditorModule::AddOnComponentEditedDelegate(const FDelegateHandle& InOnComponentEditedHandle)
{
	DelegateHandles.Add(InOnComponentEditedHandle);
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

void FObjectMixerEditorModule::RequestRebuildList() const
{
	if (MainPanel.IsValid())
	{
		MainPanel->RequestRebuildList();
	}
}

void FObjectMixerEditorModule::RefreshList() const
{
	if (MainPanel.IsValid())
	{
		MainPanel->RefreshList();
	}
}

void FObjectMixerEditorModule::RegisterMenuGroup()
{
	WorkspaceGroup = WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory()->AddGroup(
		LOCTEXT("ObjectMixerMenuGroupItemName", "Object Mixer"), 
		FSlateIcon(FObjectMixerEditorStyle::Get().GetStyleSetName(),
			"ObjectMixer.ToolbarButton", "ObjectMixer.ToolbarButton.Small"));
}

void FObjectMixerEditorModule::UnregisterMenuGroup()
{
	if (WorkspaceGroup)
	{
		for (const TSharedRef<FWorkspaceItem>& ChildItem : WorkspaceGroup->GetChildItems())
		{
			WorkspaceGroup->RemoveItem(ChildItem);
		}
		
		WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory()->RemoveItem(WorkspaceGroup.ToSharedRef());
		WorkspaceGroup.Reset();
	}
}

void FObjectMixerEditorModule::SetupMenuItemVariables()
{
	TabLabel = LOCTEXT("ObjectMixerTabLabel", "Object Mixer");

	MenuItemName = LOCTEXT("ObjectMixerEditorMenuItem", "Object Mixer");
	MenuItemIcon =
		FSlateIcon(FObjectMixerEditorStyle::Get().GetStyleSetName(), "ObjectMixer.ToolbarButton", "ObjectMixer.ToolbarButton.Small");
	MenuItemTooltip = LOCTEXT("ObjectMixerEditorMenuItemTooltip", "Open an Object Mixer instance.");

	// Should be hidden for now since it's not ready yet for public release
	TabSpawnerType = ETabSpawnerMenuType::Hidden;
}

void FObjectMixerEditorModule::RegisterTabSpawner()
{
	FTabSpawnerEntry& BrowserSpawnerEntry =
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			GetTabSpawnerId(), 
			FOnSpawnTab::CreateRaw(this, &FObjectMixerEditorModule::SpawnTab)
		)
		.SetIcon(MenuItemIcon)
		.SetDisplayName(MenuItemName)
		.SetTooltipText(MenuItemTooltip)
		.SetMenuType(TabSpawnerType);

	// Always use the base ObjectMixer function call or WorkspaceGroup may be null 
	if (!FObjectMixerEditorModule::Get().RegisterItemInMenuGroup(BrowserSpawnerEntry))
	{
		BrowserSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
	}
}

FName FObjectMixerEditorModule::GetTabSpawnerId()
{
	return "ObjectMixerToolkit";
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
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GetTabSpawnerId());
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

TSharedRef<SDockTab> FObjectMixerEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
	 return SpawnMainPanelTab();
}

TSharedRef<SDockTab> FObjectMixerEditorModule::SpawnMainPanelTab()
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
	MainPanel->RequestRebuildList();
			
	return DockTab;
}

TSharedPtr<FWorkspaceItem> FObjectMixerEditorModule::GetWorkspaceGroup()
{
	return WorkspaceGroup;
}

#undef LOCTEXT_NAMESPACE
