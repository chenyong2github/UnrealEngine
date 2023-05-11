// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsSubsystem.h"
#include "ConcertSequencerMessages.h"
#include "IConcertClient.h"
#include "IConcertSyncClient.h"
#include "IConcertSyncClientModule.h"
#include "IConcertSession.h"
#include "LevelSequence.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistItem_Sequence.h"
#include "SequencerPlaylistPlayer.h"
#include "SequencerPlaylistsLog.h"
#include "UObject/Package.h"


void USequencerPlaylistsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser")))
	{
		IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();
		ConcertClient->OnSessionStartup().AddUObject(this, &USequencerPlaylistsSubsystem::OnConcertSessionStartup);
		ConcertClient->OnSessionShutdown().AddUObject(this, &USequencerPlaylistsSubsystem::OnConcertSessionShutdown);

		if (TSharedPtr<IConcertClientSession> ConcertClientSession = ConcertClient->GetCurrentSession())
		{
			OnConcertSessionStartup(ConcertClientSession.ToSharedRef());
		}
	}
}


void USequencerPlaylistsSubsystem::Deinitialize()
{
	Super::Deinitialize();

	ensureMsgf(EditorPlaylists.Num() == 0, TEXT("USequencerPlaylistsSubsystem::EditorPlaylists leak"));
	ensureMsgf(EditorPlayers.Num() == 0, TEXT("USequencerPlaylistsSubsystem::EditorPlayers leak"));
	ensureMsgf(EditorPackages.Num() == 0, TEXT("USequencerPlaylistsSubsystem::EditorPackages leak"));
}


USequencerPlaylistPlayer* USequencerPlaylistsSubsystem::CreatePlayerForEditor(TSharedRef<SSequencerPlaylistPanel> Editor)
{
	FSequencerPlaylistEditorHandle EditorHandle(&Editor.Get());

	TObjectPtr<USequencerPlaylistPlayer>* ExistingPlayer = EditorPlayers.Find(EditorHandle);
	if (!ensure(ExistingPlayer == nullptr))
	{
		return *ExistingPlayer;
	}

	ensure(EditorPackages.Find(EditorHandle) == nullptr);
	ensure(EditorPlaylists.Find(EditorHandle) == nullptr);

	USequencerPlaylist* NewPlaylist = CreateTransientPlaylistForEditor(Editor);
	EditorPlaylists.Add(EditorHandle, NewPlaylist);
	EditorPackages.Add(EditorHandle, NewPlaylist->GetPackage());

	USequencerPlaylistPlayer* NewPlayer = NewObject<USequencerPlaylistPlayer>();
	EditorPlayers.Add(EditorHandle, NewPlayer);
	NewPlayer->SetPlaylist(NewPlaylist);

	return NewPlayer;
}


USequencerPlaylist* USequencerPlaylistsSubsystem::CreateTransientPlaylistForEditor(TSharedRef<SSequencerPlaylistPanel> Editor)
{
	FName PackageName = ::MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Engine/Transient/SequencerPlaylist"));
	UPackage* PlaylistPackage = NewObject<UPackage>(nullptr, PackageName, RF_Transient | RF_Transactional);
	return NewObject<USequencerPlaylist>(PlaylistPackage, TEXT("UntitledPlaylist"), RF_Transactional);
}


void USequencerPlaylistsSubsystem::NotifyEditorClosed(SSequencerPlaylistPanel* Editor)
{
	check(Editor);

	FSequencerPlaylistEditorHandle EditorHandle(Editor);

	ensure(EditorPlaylists.Remove(EditorHandle));
	ensure(EditorPlayers.Remove(EditorHandle));
	ensure(EditorPackages.Remove(EditorHandle));

	UpdatePrecacheSet();
}


void USequencerPlaylistsSubsystem::UpdatePrecacheSet()
{
	// Determine which sequences (if any) were added to/removed from the precache set.
	// We copy the previous set, recompute the current set, then compare them.
	const TSet<TObjectPtr<ULevelSequence>> PreviousPrecache = PrecacheSequences;

	PrecacheSequences.Empty(PrecacheSequences.Num());

	using FPlaylistPair = TPair<FSequencerPlaylistEditorHandle, TObjectPtr<USequencerPlaylist>>;
	for (const FPlaylistPair& EditorPlaylist : EditorPlaylists)
	{
		for (USequencerPlaylistItem* Item : EditorPlaylist.Value->Items)
		{
			USequencerPlaylistItem_Sequence* SequenceItem = Cast<USequencerPlaylistItem_Sequence>(Item);
			if (SequenceItem && SequenceItem->GetSequence())
			{
				PrecacheSequences.Add(SequenceItem->GetSequence());
			}
		}
	}

	FConcertSequencerPrecacheEvent PrecacheAddedEvent{ .bShouldBePrecached = true };
	FConcertSequencerPrecacheEvent PrecacheRemovedEvent{ .bShouldBePrecached = false };

	const TSet<TObjectPtr<ULevelSequence>> PrecacheUnion = PreviousPrecache.Union(PrecacheSequences);
	for (const TObjectPtr<ULevelSequence>& Sequence : PrecacheUnion)
	{
		const FString SequenceObjectPath = Sequence->GetPathName();
		if (!PreviousPrecache.Contains(Sequence))
		{
			UE_LOG(LogSequencerPlaylists, Verbose, TEXT("Adding sequence '%s' to precache set"), *SequenceObjectPath);
			PrecacheAddedEvent.SequenceObjectPaths.Add(*SequenceObjectPath);
		}
		else if (!PrecacheSequences.Contains(Sequence))
		{
			UE_LOG(LogSequencerPlaylists, Verbose, TEXT("Removing sequence '%s' from precache set"), *SequenceObjectPath);
			PrecacheRemovedEvent.SequenceObjectPaths.Add(*SequenceObjectPath);
		}
	}

	if (TSharedPtr<IConcertClientSession> Session = WeakSession.Pin())
	{
		if (PrecacheAddedEvent.SequenceObjectPaths.Num() > 0)
		{
			Session->SendCustomEvent(PrecacheAddedEvent, Session->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}

		if (PrecacheRemovedEvent.SequenceObjectPaths.Num() > 0)
		{
			Session->SendCustomEvent(PrecacheRemovedEvent, Session->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}
	}
}


void USequencerPlaylistsSubsystem::OnConcertSessionStartup(TSharedRef<IConcertClientSession> InSession)
{
	WeakSession = InSession;

	FConcertSequencerPrecacheEvent PrecacheEvent{ .bShouldBePrecached = true };

	for (const TObjectPtr<ULevelSequence>& Sequence : PrecacheSequences)
	{
		PrecacheEvent.SequenceObjectPaths.Add(*Sequence->GetPathName());
	}

	if (PrecacheEvent.SequenceObjectPaths.Num() > 0)
	{
		InSession->SendCustomEvent(PrecacheEvent, InSession->GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
	}
}


void USequencerPlaylistsSubsystem::OnConcertSessionShutdown(TSharedRef<IConcertClientSession> InSession)
{
	ensure(WeakSession == InSession);
	WeakSession = nullptr;
}
