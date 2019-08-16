// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPUtilitiesEditorModule.h"

#include "Framework/Docking/WorkspaceItem.h"
#include "GameplayTagContainer.h"
#include "LevelEditor.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "SGenlockProviderTab.h"
#include "Textures/SlateIcon.h"
#include "VPCustomUIHandler.h"
#include "VPSettings.h"
#include "VPUtilitiesEditorSettings.h"
#include "VPUtilitiesEditorStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"


#define LOCTEXT_NAMESPACE "VPUtilitiesEditor"

DEFINE_LOG_CATEGORY(LogVPUtilitiesEditor);


class FVPUtilitiesEditorModule : public IModuleInterface
{
public:
	TStrongObjectPtr<UVPCustomUIHandler> CustomUIHandler;
	FName VPRoleNotificationBarIdentifier = TEXT("VPRoles");

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FVPUtilitiesEditorStyle::Register();

		CustomUIHandler.Reset(NewObject<UVPCustomUIHandler>());
		CustomUIHandler->Init();

		{
			const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
			TSharedRef<FWorkspaceItem> MediaBrowserGroup = MenuStructure.GetDeveloperToolsMiscCategory()->GetParent()->AddGroup(
				LOCTEXT("WorkspaceMenu_VirtualProductionCategory", "Virtual Production"),
				FSlateIcon(),
				true);

			SGenlockProviderTab::RegisterNomadTabSpawner(MediaBrowserGroup);
		}

		RegisterSettings();
	}


	virtual void ShutdownModule() override
	{
		UnregisterSettings();
		SGenlockProviderTab::UnregisterNomadTabSpawner();

		if (UObjectInitialized())
		{
			CustomUIHandler->Uninit();
		}

		CustomUIHandler.Reset();

		FVPUtilitiesEditorStyle::Unregister();
	}


	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualProduction",
				LOCTEXT("VirtualProductionSettingsName", "Virtual Production"),
				LOCTEXT("VirtualProductionSettingsDescription", "Configure the Virtual Production settings."),
				GetMutableDefault<UVPSettings>());

			ISettingsSectionPtr EditorSettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualProductionEditor",
				LOCTEXT("VirtualProductionEditorSettingsName", "Virtual Production Editor"),
				LOCTEXT("VirtualProductionEditorSettingsDescription", "Configure the Virtual Production Editor settings."),
				GetMutableDefault<UVPUtilitiesEditorSettings>());
		}

		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule != nullptr)
		{
			FLevelEditorModule::FStatusBarItem Item;
			Item.Label = LOCTEXT("VPRolesLabel", "VP Roles: ");
			Item.Value = MakeAttributeLambda([]() { return FText::FromString(GetMutableDefault<UVPSettings>()->GetRoles().ToStringSimple()); });
			Item.Visibility = MakeAttributeLambda([]() { return GetMutableDefault<UVPSettings>()->bShowRoleInEditor ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; });
			LevelEditorModule->AddStatusBarItem(VPRoleNotificationBarIdentifier, Item);
		}
	}


	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualProduction");
			SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualProductionEditor");
		}

		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule != nullptr)
		{
			LevelEditorModule->RemoveStatusBarItem(VPRoleNotificationBarIdentifier);
		}
	}
};


IMPLEMENT_MODULE(FVPUtilitiesEditorModule, VPUtilitiesEditor)

#undef LOCTEXT_NAMESPACE
