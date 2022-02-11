// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencerPlaylistsModule.h"
#include "SequencerPlaylistItem.h"
#include "SequencerPlaylistItem_Sequence.generated.h"


class ISequencer;
class ULevelSequence;
class UMovieSceneSubSection;
class UMovieSceneSubTrack;


UCLASS(BlueprintType)
class USequencerPlaylistItem_Sequence : public USequencerPlaylistItem
{
	GENERATED_BODY()

	FText GetDisplayName() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	TObjectPtr<ULevelSequence> Sequence;
};


class FSequencerPlaylistItemPlayer_Sequence : public ISequencerPlaylistItemPlayer
{
	struct FItemState
	{
		TWeakObjectPtr<UMovieSceneSubTrack> WeakTrack;
		TWeakObjectPtr<UMovieSceneSubSection> WeakHoldSection;
		TArray<TWeakObjectPtr<UMovieSceneSubSection>> WeakPlaySections;
		int32 PlayingUntil_RootTicks = TNumericLimits<int32>::Min();
	};

public:
	FSequencerPlaylistItemPlayer_Sequence(TSharedRef<ISequencer> Sequencer);
	~FSequencerPlaylistItemPlayer_Sequence() override;

	//~ Begin ISequencerPlaylistItemPlayer
	bool Play(USequencerPlaylistItem* Item) override;
	bool Stop(USequencerPlaylistItem* Item) override;
	bool AddHold(USequencerPlaylistItem* Item) override;
	bool Reset(USequencerPlaylistItem* Item) override;

	bool IsPlaying(USequencerPlaylistItem* Item) const override;
	//~ End ISequencerPlaylistItemPlayer

private:
	void ClearItemStates();

	UMovieSceneSubTrack* GetOrCreateWorkingTrack(USequencerPlaylistItem* Item);
	void EndSection(UMovieSceneSection* Section);

private:
	TMap<USequencerPlaylistItem*, FItemState> ItemStates;

	TWeakPtr<ISequencer> WeakSequencer;
};
