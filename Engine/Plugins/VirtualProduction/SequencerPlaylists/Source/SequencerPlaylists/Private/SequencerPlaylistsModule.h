// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/StrongObjectPtr.h"
#include "ISequencerPlaylistsModule.h"
#include "SequencerPlaylistItem.h"


class FUICommandList;
class SDockTab;
class USequencerPlaylist;


class FSequencerPlaylistsModule : public ISequencerPlaylistsModule
{
public:
	//~ Begin IModuleInterface
	void StartupModule() override;
	void ShutdownModule() override;
	//~ End IModuleInterface

	//~ Begin ISequencerPlaylistsModule
	bool RegisterItemPlayer(TSubclassOf<USequencerPlaylistItem> ItemClass, FSequencerPlaylistItemPlayerFactory PlayerFactory) override;
	USequencerPlaylistPlayer* GetDefaultPlayer() override { return DefaultPlayer.Get(); }
	//~ End ISequencerPlaylistsModule

	TSharedPtr<ISequencerPlaylistItemPlayer> CreateItemPlayerForClass(TSubclassOf<USequencerPlaylistItem> ItemClass, TSharedRef<ISequencer> Sequencer);

	TSharedPtr<FUICommandList> GetCommandList() { return PluginCommands; }

private:
	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

	void RegisterMenus();
	TSharedRef<SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
	TMap<TSubclassOf<USequencerPlaylistItem>, FSequencerPlaylistItemPlayerFactory> ItemPlayerFactories;

	TStrongObjectPtr<USequencerPlaylistPlayer> DefaultPlayer;

	TSharedPtr<FUICommandList> PluginCommands;
};
