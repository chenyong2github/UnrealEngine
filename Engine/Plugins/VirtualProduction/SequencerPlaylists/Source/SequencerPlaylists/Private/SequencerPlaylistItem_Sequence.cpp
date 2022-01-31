// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistItem_Sequence.h"
#include "SequencerPlaylist.h"
#include "SequencerPlaylistsLog.h"

#include "ISequencer.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelSequence.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneSubSection.h"
#include "TrackEditors/SubTrackEditorBase.h" // for FSubTrackEditorUtil::CanAddSubSequence
#include "Tracks/MovieSceneSubTrack.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "SequencerPlaylists"


FText USequencerPlaylistItem_Sequence::GetDisplayName()
{
	return Sequence ? Sequence->GetDisplayName() : LOCTEXT("SequenceItemNullDisplayName", "(No sequence)");
}


FSequencerPlaylistItemPlayer_Sequence::FSequencerPlaylistItemPlayer_Sequence(TSharedRef<ISequencer> Sequencer)
	: WeakSequencer(Sequencer)
{
}


bool FSequencerPlaylistItemPlayer_Sequence::Play(USequencerPlaylistItem* Item)
{
	USequencerPlaylistItem_Sequence* SequenceItem = CastChecked<USequencerPlaylistItem_Sequence>(Item);
	if (!SequenceItem || !SequenceItem->Sequence)
	{
		return false;
	}

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!ensure(Sequencer))
	{
		return false;
	}

	ULevelSequence* RootSequence = Cast<ULevelSequence>(Sequencer->GetRootMovieSceneSequence());
	UMovieScene* RootScene = RootSequence->GetMovieScene();

	if (!FSubTrackEditorUtil::CanAddSubSequence(RootSequence, *SequenceItem->Sequence))
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequence", "Invalid level sequence {0}. There could be a circular dependency."), SequenceItem->Sequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	RootSequence->Modify();

	FItemState& ItemState = GetOrCreateItemState(Item);
	UMovieSceneSubTrack* WorkingTrack = GetOrCreateWorkingTrack(Item);

	const FQualifiedFrameTime GlobalTime = Sequencer->GetGlobalTime();

	UMovieScene* PlayScene = SequenceItem->Sequence->GetMovieScene();
	TRange<FFrameNumber> PlayRange = PlayScene->GetPlaybackRange();

	const FFrameTime StartFrameOffset_RootTicks = ConvertFrameTime(FFrameNumber(SequenceItem->StartFrameOffset),
		PlayScene->GetDisplayRate(), RootScene->GetTickResolution());

	const FFrameTime EndFrameOffset_RootTicks = ConvertFrameTime(FFrameNumber(SequenceItem->EndFrameOffset),
		PlayScene->GetDisplayRate(), RootScene->GetTickResolution());

	if (SequenceItem->StartFrameOffset > 0)
	{
		PlayRange.SetLowerBoundValue(PlayRange.GetLowerBoundValue() + StartFrameOffset_RootTicks.FloorToFrame());
	}

	if (SequenceItem->EndFrameOffset > 0)
	{
		PlayRange.SetUpperBoundValue(PlayRange.GetUpperBoundValue() - EndFrameOffset_RootTicks.FloorToFrame());
	}

	if (UMovieSceneSubSection* HoldSection = ItemState.WeakHoldSection.Get())
	{
		EndSection(HoldSection);
		ItemState.WeakHoldSection.Reset();
	}

	const float TimeScale = FMath::Max(SMALL_NUMBER, SequenceItem->PlaybackSpeed);
	const FFrameTime SingleLoopDuration = ConvertFrameTime(PlayRange.Size<FFrameTime>() / TimeScale,
		PlayScene->GetTickResolution(), RootScene->GetTickResolution());

	const FFrameNumber StartFrame = GlobalTime.Time.FloorToFrame();
	const int32 MaxDuration = TNumericLimits<int32>::Max() - StartFrame.Value - 1;
	const int32 Duration = FMath::Min(MaxDuration,
		(SingleLoopDuration * FMath::Max(1, SequenceItem->NumLoops + 1)).FloorToFrame().Value);

	UMovieSceneSubSection* WorkingSubSection = WorkingTrack->AddSequence(SequenceItem->Sequence, StartFrame, Duration);

	WorkingSubSection->Parameters.TimeScale = TimeScale;

	if (SequenceItem->StartFrameOffset > 0)
	{
		WorkingSubSection->Parameters.StartFrameOffset = StartFrameOffset_RootTicks.FloorToFrame();
	}

	if (SequenceItem->EndFrameOffset > 0)
	{
		WorkingSubSection->Parameters.EndFrameOffset = EndFrameOffset_RootTicks.FloorToFrame();
	}

	if (SequenceItem->NumLoops != 0)
	{
		WorkingSubSection->Parameters.bCanLoop = true;
	}

	ItemState.WeakPlaySections.Add(WorkingSubSection);

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);

	return true;
}


bool FSequencerPlaylistItemPlayer_Sequence::Stop(USequencerPlaylistItem* Item)
{
	USequencerPlaylistItem_Sequence* SequenceItem = CastChecked<USequencerPlaylistItem_Sequence>(Item);
	if (!SequenceItem || !SequenceItem->Sequence)
	{
		return false;
	}

	FItemState& ItemState = GetOrCreateItemState(Item);
	UMovieSceneSubSection* HoldSection = ItemState.WeakHoldSection.Get();

	if (ItemState.WeakPlaySections.Num() == 0 && HoldSection == nullptr)
	{
		return true;
	}

	if (HoldSection)
	{
		EndSection(HoldSection);
		ItemState.WeakHoldSection.Reset();
	}

	for (const TWeakObjectPtr<UMovieSceneSubSection>& WeakPlaySection : ItemState.WeakPlaySections)
	{
		if (UMovieSceneSubSection* PlaySection = WeakPlaySection.Get())
		{
			EndSection(PlaySection);
		}
	}

	ItemState.WeakPlaySections.Empty();

	return true;
}


bool FSequencerPlaylistItemPlayer_Sequence::Reset(USequencerPlaylistItem* Item)
{
	if (Item->bHoldAtFirstFrame)
	{
		Stop(Item);
		return AddHold(Item);
	}
	else
	{
		return Stop(Item);
	}
}


bool FSequencerPlaylistItemPlayer_Sequence::AddHold(USequencerPlaylistItem* Item)
{
	USequencerPlaylistItem_Sequence* SequenceItem = CastChecked<USequencerPlaylistItem_Sequence>(Item);
	if (!SequenceItem || !SequenceItem->Sequence)
	{
		return false;
	}

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!ensure(Sequencer))
	{
		return false;
	}

	FItemState& ItemState = GetOrCreateItemState(Item);
	UMovieSceneSubSection* HoldSection = ItemState.WeakHoldSection.Get();

	if (HoldSection)
	{
		return false;
	}

	ULevelSequence* RootSequence = Cast<ULevelSequence>(Sequencer->GetRootMovieSceneSequence());
	UMovieScene* RootScene = RootSequence->GetMovieScene();

	if (!FSubTrackEditorUtil::CanAddSubSequence(RootSequence, *SequenceItem->Sequence))
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequence", "Invalid level sequence {0}. There could be a circular dependency."), SequenceItem->Sequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return false;
	}

	RootSequence->Modify();

	UMovieSceneSubTrack* WorkingTrack = GetOrCreateWorkingTrack(Item);

	const FQualifiedFrameTime GlobalTime = Sequencer->GetGlobalTime();
	const FFrameNumber StartFrame = GlobalTime.Time.FloorToFrame();
	const int32 MaxDuration = TNumericLimits<int32>::Max() - StartFrame.Value - 1;
	HoldSection = WorkingTrack->AddSequence(SequenceItem->Sequence, StartFrame, MaxDuration);

	if (SequenceItem->StartFrameOffset > 0)
	{
		const FFrameTime StartFrameOffset_RootTicks = ConvertFrameTime(
			FFrameNumber(SequenceItem->StartFrameOffset),
			SequenceItem->Sequence->GetMovieScene()->GetDisplayRate(),
			RootScene->GetTickResolution());
		HoldSection->Parameters.StartFrameOffset = StartFrameOffset_RootTicks.FloorToFrame();
	}

	HoldSection->Parameters.TimeScale = 0.f;

	ItemState.WeakHoldSection = HoldSection;

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);

	return true;
}


UMovieSceneSubTrack* FSequencerPlaylistItemPlayer_Sequence::GetOrCreateWorkingTrack(USequencerPlaylistItem* Item)
{
	USequencerPlaylistItem_Sequence* SequenceItem = CastChecked<USequencerPlaylistItem_Sequence>(Item);
	if (!SequenceItem || !SequenceItem->Sequence)
	{
		return nullptr;
	}

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!ensure(Sequencer))
	{
		return nullptr;
	}

	FItemState& ItemState = GetOrCreateItemState(Item);

	ULevelSequence* RootSequence = Cast<ULevelSequence>(Sequencer->GetRootMovieSceneSequence());
	UMovieScene* RootScene = RootSequence->GetMovieScene();

	// Ensure we have a working track, and that it belongs to the current root sequence.
	if (ItemState.WeakTrack.IsValid() && ItemState.WeakTrack->GetTypedOuter<ULevelSequence>() == RootSequence)
	{
		return ItemState.WeakTrack.Get();
	}

	UMovieSceneSubTrack* NewWorkingTrack = RootScene->AddMasterTrack<UMovieSceneSubTrack>();
	NewWorkingTrack->SetDisplayName(FText::Format(LOCTEXT("SequenceItemTrackName", "Item - {0}"), SequenceItem->Sequence->GetDisplayName()));

	// Find or create folder named for our playlist, and organize our track beneath it.
	FText PlaylistName = FText::GetEmpty();
	if (USequencerPlaylist* OuterPlaylist = SequenceItem->GetTypedOuter<USequencerPlaylist>())
	{
		PlaylistName = FText::FromString(*OuterPlaylist->GetName());
	}

	const FText PlaylistFolderNameText = FText::Format(LOCTEXT("PlaylistFolderName", "Playlist - {0}"), PlaylistName);
	const FName PlaylistFolderName = FName(*PlaylistFolderNameText.ToString());

	UMovieSceneFolder* FolderToUse = nullptr;
	for (UMovieSceneFolder* Folder : RootScene->GetRootFolders())
	{
		if (Folder->GetFolderName() == PlaylistFolderName)
		{
			FolderToUse = Folder;
			break;
		}
	}

	if (FolderToUse == nullptr)
	{
		FolderToUse = NewObject<UMovieSceneFolder>(RootScene, NAME_None, RF_Transactional);
		FolderToUse->SetFolderName(PlaylistFolderName);
		RootScene->GetRootFolders().Add(FolderToUse);
	}

	FolderToUse->AddChildMasterTrack(NewWorkingTrack);

	ItemState.WeakTrack = NewWorkingTrack;
	return NewWorkingTrack;
}


FSequencerPlaylistItemPlayer_Sequence::FItemState& FSequencerPlaylistItemPlayer_Sequence::GetOrCreateItemState(USequencerPlaylistItem* Item)
{
	check(Item);
	return ItemStates.FindOrAdd(Item);
}


void FSequencerPlaylistItemPlayer_Sequence::EndSection(UMovieSceneSection* Section)
{
	if (!ensure(Section))
	{
		return;
	}

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!ensure(Sequencer))
	{
		return;
	}

	ULevelSequence* RootSequence = Cast<ULevelSequence>(Sequencer->GetRootMovieSceneSequence());

	RootSequence->Modify();

	UMovieScene* SectionScene = Section->GetTypedOuter<UMovieScene>();
	check(SectionScene);

	const FFrameTime SectionNow = Sequencer->GetGlobalTime().ConvertTo(SectionScene->GetTickResolution());
	if (Section->IsTimeWithinSection(SectionNow.FloorToFrame()))
	{
		Section->SetEndFrame(TRangeBound<FFrameNumber>::Exclusive(SectionNow.FloorToFrame()));
	}
}


#undef LOCTEXT_NAMESPACE
