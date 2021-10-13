// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorModule.h"

#include "AssetTypeActions/AssetTypeActions_ConsoleVariables.h"
#include "ConsoleVariablesEditorCommands.h"
#include "ConsoleVariablesEditorStyle.h"
#include "ConsoleVariablesEditorProjectSettings.h"
#include "Toolkits/ConsoleVariablesEditorToolkit.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "LevelEditor.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FConsoleVariablesEditorModule"

FConsoleVariablesEditorModule& FConsoleVariablesEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FConsoleVariablesEditorModule>("ConsoleVariablesEditor");
}

void FConsoleVariablesEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_ConsoleVariables>());

	FConsoleVariablesEditorStyle::Initialize();
	FConsoleVariablesEditorCommands::Register();
	
	// add the menu subsection
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FConsoleVariablesEditorModule::PostEngineInit);
}

void FConsoleVariablesEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	FConsoleVariablesEditorStyle::Shutdown();
	
	FConsoleVariablesEditorCommands::Unregister();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Console Variables UI");
	}
}

void FConsoleVariablesEditorModule::PostEngineInit()
{
	RegisterMenuItem();
	RegisterProjectSettings();
}

void FConsoleVariablesEditorModule::RegisterMenuItem()
{
	if (FSlateApplication::IsInitialized())
	{
		if (IsRunningGame())
		{
			return;
		}
		
		TSharedRef<FUICommandList> MenuItemCommandList = MakeShareable(new FUICommandList);

		MenuItemCommandList->MapAction(
			FConsoleVariablesEditorCommands::Get().OpenConsoleVariablesEditorMenuItem,
			FExecuteAction::CreateLambda([this] ()
			{
				OpenConsoleVariablesEditor(EToolkitMode::WorldCentric, FModuleManager::LoadModuleChecked< FLevelEditorModule >("LevelEditor").GetFirstLevelEditor());
			})
		);
		
		TSharedPtr<FExtender> NewMenuExtender = MakeShareable(new FExtender);
		NewMenuExtender->AddMenuExtension("ExperimentalTabSpawners", 
		                                  EExtensionHook::After, 
		                                  MenuItemCommandList, 
		                                  FMenuExtensionDelegate::CreateLambda([this] (FMenuBuilder& MenuBuilder)
		                                  {
			                                  MenuBuilder.AddMenuEntry(FConsoleVariablesEditorCommands::Get().OpenConsoleVariablesEditorMenuItem);
		                                  }));
	
		// Get the Level Editor so we can insert our item into the Level Editor menu subsection
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(NewMenuExtender);
	}
}

bool FConsoleVariablesEditorModule::RegisterProjectSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		ProjectSettingsSectionPtr = SettingsModule.RegisterSettings("Project", "Plugins", "Console Variables UI",
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsCategoryDisplayName", "Console Variables UI"),
			NSLOCTEXT("ConsoleVariables", "ConsoleVariablesSettingsDescription", "Configure the Console Variables UI user settings"),
			GetMutableDefault<UConsoleVariablesEditorProjectSettings>());

		if (ProjectSettingsSectionPtr.IsValid() && ProjectSettingsSectionPtr->GetSettingsObject().IsValid())
		{
			ProjectSettingsObjectPtr = Cast<UConsoleVariablesEditorProjectSettings>(ProjectSettingsSectionPtr->GetSettingsObject());

			ProjectSettingsSectionPtr->OnModified().BindRaw(this, &FConsoleVariablesEditorModule::HandleModifiedProjectSettings);
		}
	}

	return ProjectSettingsObjectPtr.IsValid();
}

bool FConsoleVariablesEditorModule::HandleModifiedProjectSettings()
{	
	return true;
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesDialogWithAssetSelected(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FAssetData& InAssetData)
{
	if (InAssetData.IsValid())
	{
		OpenConsoleVariablesEditor(Mode, InitToolkitHost);
	}

	if (ConsoleVariablesEditorToolkit.IsValid())
	{
		TWeakPtr<FConsoleVariablesEditorMainPanel> MainPanel = ConsoleVariablesEditorToolkit.Pin()->GetMainPanel();
		if (MainPanel.IsValid())
		{
			MainPanel.Pin()->ImportPreset(InAssetData);
		}
	}
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost)
{
	if (ConsoleVariablesEditorToolkit.IsValid())
	{
		ConsoleVariablesEditorToolkit.Pin()->CloseWindow();
	}
	
	ConsoleVariablesEditorToolkit = FConsoleVariablesEditorToolkit::CreateConsoleVariablesEditor(Mode, InitToolkitHost);
}

void FConsoleVariablesEditorModule::OpenConsoleVariablesSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Console Variables UI");
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FConsoleVariablesEditorModule, ConsoleVariablesEditor)
