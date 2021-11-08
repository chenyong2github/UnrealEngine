// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistPlayer.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistItem.h"
#include "SequencerPlaylistsLog.h"
#include "SequencerPlaylistsModule.h"

#include "ILevelSequenceEditorToolkit.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Recorder/TakeRecorder.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "SequencerPlaylists"


USequencerPlaylistPlayer::USequencerPlaylistPlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UTakeRecorder::OnRecordingInitialized().AddUObject(this, &USequencerPlaylistPlayer::OnTakeRecorderInitialized);
		if (UTakeRecorder* ExistingRecorder = UTakeRecorder::GetActiveRecorder())
		{
			OnTakeRecorderInitialized(ExistingRecorder);
		}
	}
}


void USequencerPlaylistPlayer::BeginDestroy()
{
	Super::BeginDestroy();

	UTakeRecorder::OnRecordingInitialized().RemoveAll(this);

	if (UTakeRecorder* BoundRecorder = WeakRecorder.Get())
	{
		BoundRecorder->OnRecordingStarted().RemoveAll(this);
		BoundRecorder->OnRecordingStopped().RemoveAll(this);
	}
}


bool USequencerPlaylistPlayer::PlayItem(USequencerPlaylistItem* Item)
{
	if (!Item)
	{
		return false;
	}

	FScopedTransaction Transaction(FText::Format(LOCTEXT("PlayItemTransaction", "Trigger playback of {0}"), Item->GetDisplayName()));
	return GetCheckedItemPlayer(Item)->Play(Item);
}


bool USequencerPlaylistPlayer::StopItem(USequencerPlaylistItem* Item)
{
	if (!Item)
	{
		return false;
	}

	FScopedTransaction Transaction(FText::Format(LOCTEXT("StopItemTransaction", "Stop playback of {0}"), Item->GetDisplayName()));
	return GetCheckedItemPlayer(Item)->Stop(Item);
}


bool USequencerPlaylistPlayer::ResetItem(USequencerPlaylistItem* Item)
{
	if (!Item)
	{
		return false;
	}

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ResetItemTransaction", "Reset playback of {0}"), Item->GetDisplayName()));
	return GetCheckedItemPlayer(Item)->Reset(Item);
}


bool USequencerPlaylistPlayer::PlayAll()
{
	if (!ensure(Playlist) || !Playlist->Items.Num())
	{
		return false;
	}

	bool bResult = true;

	FScopedTransaction Transaction(LOCTEXT("PlayAllTransaction", "Trigger playback of all items"));
	for (USequencerPlaylistItem* Item : Playlist->Items)
	{
		bResult |= GetCheckedItemPlayer(Item)->Play(Item);
	}

	return bResult;
}


bool USequencerPlaylistPlayer::StopAll()
{
	if (!ensure(Playlist) || !Playlist->Items.Num())
	{
		return false;
	}

	bool bResult = true;

	FScopedTransaction Transaction(LOCTEXT("StopAllTransaction", "Stop playback of all items"));
	for (USequencerPlaylistItem* Item : Playlist->Items)
	{
		bResult |= GetCheckedItemPlayer(Item)->Stop(Item);
	}

	return bResult;
}


bool USequencerPlaylistPlayer::ResetAll()
{
	if (!ensure(Playlist) || !Playlist->Items.Num())
	{
		return false;
	}

	bool bResult = true;

	FScopedTransaction Transaction(LOCTEXT("ResetAllTransaction", "Reset playback of all items"));
	for (USequencerPlaylistItem* Item : Playlist->Items)
	{
		bResult |= GetCheckedItemPlayer(Item)->Reset(Item);
	}

	return bResult;
}


TSharedPtr<ISequencer> USequencerPlaylistPlayer::GetSequencer()
{
	if (TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin())
	{
		return Sequencer;
	}

	ULevelSequence* RootSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (!RootSequence)
	{
		// TODO: Instantiate sequencer with new empty take?
		UE_LOG(LogSequencerPlaylists, Error, TEXT("USequencerPlaylistPlayer::GetSequencer: GetCurrentLevelSequence returned null"));
		return nullptr;
	}

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(RootSequence);
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(RootSequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);

	TSharedPtr<ISequencer> Sequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	if (!Sequencer)
	{
		UE_LOG(LogSequencerPlaylists, Error, TEXT("USequencerPlaylistPlayer::GetSequencer: Unable to open Sequencer for asset"));
	}
	else
	{
		Sequencer->OnCloseEvent().AddWeakLambda(this, [this](TSharedRef<ISequencer>) {
			// Existing item players invalidated by their sequencer going away.
			ItemPlayersByType.Empty();
		});
	}
	WeakSequencer = Sequencer;
	return Sequencer;
}


TSharedPtr<ISequencer> USequencerPlaylistPlayer::GetValidatedSequencer()
{
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Sequencer)
	{
		return nullptr;
	}

	ULevelSequence* RootSequence = Cast<ULevelSequence>(Sequencer->GetRootMovieSceneSequence());
	if (!RootSequence)
	{
		UE_LOG(LogSequencerPlaylists, Error, TEXT("USequencerPlaylistPlayer::GetValidatedSequencer: Unable to get root sequence"));
		return nullptr;
	}

	UMovieScene* RootScene = RootSequence->GetMovieScene();
	if (!RootScene)
	{
		// TODO: Seems like this may not be possible?
		UE_LOG(LogSequencerPlaylists, Error, TEXT("USequencerPlaylistPlayer::GetValidatedSequencer: Unable to get root scene"));
		return nullptr;
	}

	return Sequencer;
}


void USequencerPlaylistPlayer::OnTakeRecorderInitialized(UTakeRecorder* InRecorder)
{
	if (InRecorder)
	{
		if (UTakeRecorder* PrevRecorder = WeakRecorder.Get())
		{
			PrevRecorder->OnRecordingStarted().RemoveAll(this);
			PrevRecorder->OnRecordingStopped().RemoveAll(this);
		}

		InRecorder->OnRecordingStarted().AddUObject(this, &USequencerPlaylistPlayer::OnTakeRecorderStarted);
		InRecorder->OnRecordingStopped().AddUObject(this, &USequencerPlaylistPlayer::OnTakeRecorderStopped);
		WeakRecorder = InRecorder;
	}
}


void USequencerPlaylistPlayer::OnTakeRecorderStarted(UTakeRecorder* InRecorder)
{
	if (!ensure(Playlist))
	{
		return;
	}

	if (TSharedPtr<ISequencer> Sequencer = GetValidatedSequencer())
	{
		for (USequencerPlaylistItem* Item : Playlist->Items)
		{
			if (Item->bHoldAtFirstFrame)
			{
				GetCheckedItemPlayer(Item)->AddHold(Item);
			}
		}
	}
}


void USequencerPlaylistPlayer::OnTakeRecorderStopped(UTakeRecorder* InRecorder)
{
	if (!ensure(Playlist))
	{
		return;
	}

	// FIXME: Any sequences not already stopped end up a few frames too long; pass in explicit end frame?
	if (TSharedPtr<ISequencer> Sequencer = GetValidatedSequencer())
	{
		for (USequencerPlaylistItem* Item : Playlist->Items)
		{
			GetCheckedItemPlayer(Item)->Stop(Item);
		}
	}
}


TSharedPtr<ISequencerPlaylistItemPlayer> USequencerPlaylistPlayer::GetCheckedItemPlayer(USequencerPlaylistItem* Item)
{
	check(Item);

	TSubclassOf<USequencerPlaylistItem> ItemClass = Item->GetClass();
	if (TSharedRef<ISequencerPlaylistItemPlayer>* ExistingPlayer = ItemPlayersByType.Find(ItemClass))
	{
		return *ExistingPlayer;
	}

	TSharedPtr<ISequencer> Sequencer = GetValidatedSequencer();
	check(Sequencer.IsValid());

	TSharedPtr<ISequencerPlaylistItemPlayer> NewPlayer =
		static_cast<FSequencerPlaylistsModule&>(FSequencerPlaylistsModule::Get()).CreateItemPlayerForClass(ItemClass, Sequencer.ToSharedRef());
	check(NewPlayer);

	ItemPlayersByType.Add(ItemClass, NewPlayer.ToSharedRef());
	return NewPlayer;
}


#undef LOCTEXT_NAMESPACE
