// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorModule.h"

#include "DMXControlConsoleEditorFromLegacyUpgradeHandler.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXEditorModule.h"
#include "Commands/DMXControlConsoleEditorCommands.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Views/SDMXControlConsoleEditorView.h"

#include "LevelEditor.h"
#include "ToolMenu.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorModule"

const FName FDMXControlConsoleEditorModule::ControlConsoleTabName("DMXControlConsoleTabName");

void FDMXControlConsoleEditorModule::StartupModule()
{
	FDMXControlConsoleEditorCommands::Register();
	RegisterControlConsoleActions();

	// Try UpgradePath if configurations settings have data from previous Control Console versions
	FDMXControlConsoleEditorFromLegacyUpgradeHandler::TryUpgradePathFromLegacy();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ControlConsoleTabName, 
		FOnSpawnTab::CreateStatic(&FDMXControlConsoleEditorModule::OnSpawnControlConsoleTab))
		.SetDisplayName(LOCTEXT("DMXControlConsoleTabTitle", "DMX Control Console"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.TabIcon"));

	FCoreDelegates::OnPostEngineInit.AddStatic(&FDMXControlConsoleEditorModule::RegisterDMXMenuExtender);
}

void FDMXControlConsoleEditorModule::ShutdownModule()
{
}

void FDMXControlConsoleEditorModule::RegisterControlConsoleActions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().OpenControlConsole,
		FExecuteAction::CreateStatic(&FDMXControlConsoleEditorModule::OpenControlConsole)
	);

	FDMXControlConsoleEditorManager& ControlConsoleManager = FDMXControlConsoleEditorManager::Get();

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().Save,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::Save)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SaveAs,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::SaveAs)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().Load,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::Load)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SendDMX,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::SendDMX),
		FCanExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::CanSendDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::CanSendDMX)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().StopDMX,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::StopDMX),
		FCanExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::CanStopDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::CanStopDMX)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().ClearAll,
		FExecuteAction::CreateSP(ControlConsoleManager.AsShared(), &FDMXControlConsoleEditorManager::ClearAll)
	);
}

void FDMXControlConsoleEditorModule::RegisterDMXMenuExtender()
{
	FDMXEditorModule& DMXEditorModule = FModuleManager::GetModuleChecked<FDMXEditorModule>(TEXT("DMXEditor"));
	const TSharedPtr<FExtender> LevelEditorToolbarDMXMenuExtender = DMXEditorModule.GetLevelEditorToolbarDMXMenuExtender();
	
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();

	LevelEditorToolbarDMXMenuExtender->AddMenuExtension("OpenActivityMonitor", EExtensionHook::Position::After, CommandList, FMenuExtensionDelegate::CreateStatic(&FDMXControlConsoleEditorModule::ExtendDMXMenu));
}

void FDMXControlConsoleEditorModule::ExtendDMXMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().OpenControlConsole,
			NAME_None,
			LOCTEXT("DMXControlConsoleMenuLabel", "Control Console"),
			LOCTEXT("DMXControlConsoleMenuTooltip", "Opens a small console that can send DMX locally or over the network"),
			FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.TabIcon")
		);
}

void FDMXControlConsoleEditorModule::OpenControlConsole()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ControlConsoleTabName);
}

TSharedRef<SDockTab> FDMXControlConsoleEditorModule::OnSpawnControlConsoleTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DMXControlConsoleTitle", "DMX ControlConsole"))
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDMXControlConsoleEditorView)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDMXControlConsoleEditorModule, DMXControlConsoleEditor)
