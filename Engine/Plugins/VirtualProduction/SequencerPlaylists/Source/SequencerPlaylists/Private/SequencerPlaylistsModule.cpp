// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsModule.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistItem_Sequence.h"
#include "SequencerPlaylistPlayer.h"
#include "SequencerPlaylistsLog.h"
#include "SequencerPlaylistsCommands.h"
#include "SequencerPlaylistsStyle.h"
#include "SequencerPlaylistsWidgets.h"

#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"


static const FName SequencerPlaylistsTabName("SequencerPlaylists");

DEFINE_LOG_CATEGORY(LogSequencerPlaylists)


#define LOCTEXT_NAMESPACE "SequencerPlaylists"


void FSequencerPlaylistsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	DefaultPlayer.Reset(NewObject<USequencerPlaylistPlayer>());
	DefaultPlayer->SetPlaylist(NewObject<USequencerPlaylist>());

	RegisterItemPlayer(USequencerPlaylistItem_Sequence::StaticClass(),
		FSequencerPlaylistItemPlayerFactory::CreateLambda(
			[](TSharedRef<ISequencer> Sequencer) {
				return MakeShared<FSequencerPlaylistItemPlayer_Sequence>(Sequencer);
			}
	));

	FSequencerPlaylistsStyle::Initialize();
	FSequencerPlaylistsStyle::ReloadTextures();

	FSequencerPlaylistsCommands::Register();

	PluginCommands = MakeShared<FUICommandList>();

	PluginCommands->MapAction(
		FSequencerPlaylistsCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FSequencerPlaylistsModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSequencerPlaylistsModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SequencerPlaylistsTabName, FOnSpawnTab::CreateRaw(this, &FSequencerPlaylistsModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabSpawnerDisplayName", "Playlists"))
		.SetTooltipText(LOCTEXT("TabSpawnerTooltipText", "Open the Sequencer Playlists tab."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorVirtualProductionCategory())
		.SetIcon(FSlateIcon(FSequencerPlaylistsStyle::GetStyleSetName(), "SequencerPlaylists.TabIcon"));
}


void FSequencerPlaylistsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FSequencerPlaylistsStyle::Shutdown();

	FSequencerPlaylistsCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SequencerPlaylistsTabName);
}


bool FSequencerPlaylistsModule::RegisterItemPlayer(TSubclassOf<USequencerPlaylistItem> ItemClass, FSequencerPlaylistItemPlayerFactory PlayerFactory)
{
	if (ItemPlayerFactories.Contains(ItemClass))
	{
		return false;
	}

	ItemPlayerFactories.Add(ItemClass, PlayerFactory);
	return true;
}


TSharedPtr<ISequencerPlaylistItemPlayer> FSequencerPlaylistsModule::CreateItemPlayerForClass(TSubclassOf<USequencerPlaylistItem> ItemClass, TSharedRef<ISequencer> Sequencer)
{
	if (FSequencerPlaylistItemPlayerFactory* Factory = ItemPlayerFactories.Find(ItemClass))
	{
		return Factory->Execute(Sequencer);
	}

	return nullptr;
}


TSharedRef<SDockTab> FSequencerPlaylistsModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SSequencerPlaylistPanel, DefaultPlayer.Get())
		];
}


void FSequencerPlaylistsModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SequencerPlaylistsTabName);
}


void FSequencerPlaylistsModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FSequencerPlaylistsCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FSequencerPlaylistsModule, SequencerPlaylists)
