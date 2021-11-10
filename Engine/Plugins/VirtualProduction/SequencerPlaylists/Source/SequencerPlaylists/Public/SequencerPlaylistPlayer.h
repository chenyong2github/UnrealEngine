// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "SequencerPlaylistItem.h"
#include "SequencerPlaylistPlayer.generated.h"


class ISequencer;
class ISequencerPlaylistItemPlayer;
class USequencerPlaylist;
class UTakeRecorder;


/** Controls playback of playlist items */
UCLASS(BlueprintType)
class USequencerPlaylistPlayer : public UObject
{
	GENERATED_BODY()

public:
	USequencerPlaylistPlayer(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject interface
	void BeginDestroy() override;
	//~ End UObject interface

	void SetPlaylist(USequencerPlaylist* InPlaylist) { Playlist = InPlaylist; }
	USequencerPlaylist* GetPlaylist() { return Playlist; }

public:
	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool PlayItem(USequencerPlaylistItem* Item);

	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool StopItem(USequencerPlaylistItem* Item);

	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool ResetItem(USequencerPlaylistItem* Item);

	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool PlayAll();

	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool StopAll();

	UFUNCTION(BlueprintCallable, Category="SequencerPlaylists")
	bool ResetAll();

private:
	TSharedPtr<ISequencer> GetSequencer();

	/** Centralizes additional common assumptions that are convenient for downstream code to make. */
	TSharedPtr<ISequencer> GetValidatedSequencer();

	void OnTakeRecorderInitialized(UTakeRecorder* InRecorder);
	void OnTakeRecorderStarted(UTakeRecorder* InRecorder);
	void OnTakeRecorderStopped(UTakeRecorder* InRecorder);

	TSharedPtr<ISequencerPlaylistItemPlayer> GetCheckedItemPlayer(USequencerPlaylistItem* Item);

private:
	TMap<TSubclassOf<USequencerPlaylistItem>, TSharedRef<ISequencerPlaylistItemPlayer>> ItemPlayersByType;

	UPROPERTY(Transient)
	TObjectPtr<USequencerPlaylist> Playlist;

	/** The last initialized recorder, and the one we've bound delegates on. */
	TWeakObjectPtr<UTakeRecorder> WeakRecorder;

	TWeakPtr<ISequencer> WeakSequencer;
};
