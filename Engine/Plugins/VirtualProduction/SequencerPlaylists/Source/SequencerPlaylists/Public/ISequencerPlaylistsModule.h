// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Modules/ModuleManager.h"
#include "Templates/SubclassOf.h"


class ISequencer;
class USequencerPlaylistItem;
class USequencerPlaylistPlayer;


class ISequencerPlaylistItemPlayer
{
public:
	virtual ~ISequencerPlaylistItemPlayer() {}

	virtual bool Play(USequencerPlaylistItem* Item) = 0;
	virtual bool Stop(USequencerPlaylistItem* Item) = 0;
	virtual bool Reset(USequencerPlaylistItem* Item) = 0;
	virtual bool AddHold(USequencerPlaylistItem* Item) = 0;
};


DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<ISequencerPlaylistItemPlayer>, FSequencerPlaylistItemPlayerFactory, TSharedRef<ISequencer>);


class ISequencerPlaylistsModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static ISequencerPlaylistsModule& Get()
	{
		static const FName ModuleName = "SequencerPlaylists";
		return FModuleManager::LoadModuleChecked<ISequencerPlaylistsModule>(ModuleName);
	}

	virtual bool RegisterItemPlayer(TSubclassOf<USequencerPlaylistItem> ItemClass, FSequencerPlaylistItemPlayerFactory PlayerFactory) = 0;

	virtual USequencerPlaylistPlayer* GetDefaultPlayer() = 0;
};
