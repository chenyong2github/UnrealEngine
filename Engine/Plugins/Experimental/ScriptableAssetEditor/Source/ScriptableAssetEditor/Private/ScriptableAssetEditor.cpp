// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptableAssetEditor.h"
#include "ScriptableAssetEditorCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "UScriptableAssetEditor.h"

static const FName ScriptableAssetEditorTabName("ScriptableAssetEditor");

#define LOCTEXT_NAMESPACE "FScriptableAssetEditorModule"

void FScriptableAssetEditorModule::StartupModule()
{
	FScriptableAssetEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FScriptableAssetEditorCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FScriptableAssetEditorModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FScriptableAssetEditorModule::RegisterMenus));
}

void FScriptableAssetEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	PluginCommands.Reset();
	FScriptableAssetEditorCommands::Unregister();
}

void FScriptableAssetEditorModule::PluginButtonClicked()
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	UAssetEditor* AssetEditor = NewObject<UAssetEditor>(AssetEditorSubsystem, UScriptableAssetEditor::StaticClass());
	AssetEditor->Initialize();
}

void FScriptableAssetEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("ExperimentalTabSpawners");
			Section.AddMenuEntryWithCommandList(FScriptableAssetEditorCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FScriptableAssetEditorModule, ScriptableAssetEditor)