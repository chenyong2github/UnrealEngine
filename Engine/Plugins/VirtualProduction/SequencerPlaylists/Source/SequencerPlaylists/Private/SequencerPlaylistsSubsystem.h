// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "SequencerPlaylistsSubsystem.generated.h"


class IConcertClientSession;
class SSequencerPlaylistPanel;
class ULevelSequence;
class USequencerPlaylist;
class USequencerPlaylistPlayer;


USTRUCT()
struct FSequencerPlaylistEditorHandle
{
	GENERATED_BODY()

	FSequencerPlaylistEditorHandle() {}

	explicit FSequencerPlaylistEditorHandle(SSequencerPlaylistPanel* InEditor)
		: Editor(InEditor)
	{
	}

	bool operator==(const FSequencerPlaylistEditorHandle& Other) const
	{
		return Editor == Other.Editor;
	}

	friend uint32 GetTypeHash(const FSequencerPlaylistEditorHandle& Handle)
	{
		return GetTypeHash(Handle.Editor);
	}

	SSequencerPlaylistPanel* Editor = nullptr;
};


UCLASS()
class USequencerPlaylistsSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** For every tab, we manage the lifetime of the associated Player and its transient Playlist. */
	USequencerPlaylistPlayer* CreatePlayerForEditor(TSharedRef<SSequencerPlaylistPanel> Editor);
	void NotifyEditorClosed(SSequencerPlaylistPanel* Editor);

	void UpdatePrecacheSet();

protected:
	USequencerPlaylist* CreateTransientPlaylistForEditor(TSharedRef<SSequencerPlaylistPanel> Editor);

	void OnConcertSessionStartup(TSharedRef<IConcertClientSession> InSession);
	void OnConcertSessionShutdown(TSharedRef<IConcertClientSession> InSession);

protected:
	UPROPERTY(Transient)
	TMap<FSequencerPlaylistEditorHandle, TObjectPtr<UPackage>> EditorPackages;

	UPROPERTY(Transient)
	TMap<FSequencerPlaylistEditorHandle, TObjectPtr<USequencerPlaylistPlayer>> EditorPlayers;

	UPROPERTY(Transient)
	TMap<FSequencerPlaylistEditorHandle, TObjectPtr<USequencerPlaylist>> EditorPlaylists;

	UPROPERTY(Transient)
	TSet<TObjectPtr<ULevelSequence>> PrecacheSequences;

	/** Weak pointer to the client session with which to send events. May be null or stale. */
	TWeakPtr<IConcertClientSession> WeakSession;
};
