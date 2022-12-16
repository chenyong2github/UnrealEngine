// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleModule.h"

#include "DMXControlConsoleManager.h"
#include "DMXEditorModule.h"
#include "Commands/DMXControlConsoleCommands.h"
#include "Style/DMXControlConsoleStyle.h"
#include "Views/SDMXControlConsoleView.h"

#include "LevelEditor.h"
#include "ToolMenu.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleModule"

const FName FDMXControlConsoleModule::ControlConsoleTabName("DMXControlConsoleTabName");

void FDMXControlConsoleModule::StartupModule()
{
	FDMXControlConsoleCommands::Register();

	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	CommandList->MapAction(
		FDMXControlConsoleCommands::Get().OpenControlConsole,
		FExecuteAction::CreateStatic(&FDMXControlConsoleModule::OpenControlConsole)
	);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ControlConsoleTabName, 
		FOnSpawnTab::CreateStatic(&FDMXControlConsoleModule::OnSpawnControlConsoleTab))
		.SetDisplayName(LOCTEXT("DMXControlConsoleTabTitle", "DMX Control Console"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FDMXControlConsoleStyle::Get().GetStyleSetName(), "DMXControlConsole.TabIcon"));

	FCoreDelegates::OnPostEngineInit.AddStatic(&FDMXControlConsoleModule::RegisterDMXMenuExtender);
}

void FDMXControlConsoleModule::ShutdownModule()
{
}

void FDMXControlConsoleModule::RegisterDMXMenuExtender()
{
	FDMXEditorModule& DMXEditorModule = FModuleManager::GetModuleChecked<FDMXEditorModule>(TEXT("DMXEditor"));
	const TSharedPtr<FExtender> LevelEditorToolbarDMXMenuExtender = DMXEditorModule.GetLevelEditorToolbarDMXMenuExtender();
	
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	LevelEditorToolbarDMXMenuExtender->AddMenuExtension("OpenActivityMonitor", EExtensionHook::Position::After, CommandList, FMenuExtensionDelegate::CreateStatic(&FDMXControlConsoleModule::ExtendDMXMenu));
}

void FDMXControlConsoleModule::ExtendDMXMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(FDMXControlConsoleCommands::Get().OpenControlConsole,
			NAME_None,
			LOCTEXT("test", "DMX ControlConsole"),
			LOCTEXT("test2", "Sets whether DMX is received in from the network"),
			FSlateIcon(FDMXControlConsoleStyle::Get().GetStyleSetName(), "DMXControlConsole.TabIcon")
		);
}

void FDMXControlConsoleModule::OpenControlConsole()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ControlConsoleTabName);
}

TSharedRef<SDockTab> FDMXControlConsoleModule::OnSpawnControlConsoleTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DMXControlConsoleTitle", "DMX ControlConsole"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXControlConsoleView)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDMXControlConsoleModule, DMXControlConsole)
