// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorBlueprintLibrary.h"

#include "ISequencer.h"
#include "IKeyArea.h"
#include "LevelSequence.h"

#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "MovieSceneCommonHelpers.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannelProxy.h"

namespace
{
	static TWeakPtr<ISequencer> CurrentSequencer;
}

bool ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(ULevelSequence* LevelSequence)
{
	if (LevelSequence)
	{
		return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);
	}

	return false;
}

ULevelSequence* ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence()
{
	if (CurrentSequencer.IsValid())
	{
		return Cast<ULevelSequence>(CurrentSequencer.Pin()->GetRootMovieSceneSequence());
	}
	return nullptr;
}

ULevelSequence* ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence()
{
	if (CurrentSequencer.IsValid())
	{
		return Cast<ULevelSequence>(CurrentSequencer.Pin()->GetFocusedMovieSceneSequence());
	}
	return nullptr;
}

void ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence()
{
	if (CurrentSequencer.IsValid())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(CurrentSequencer.Pin()->GetRootMovieSceneSequence());
	}
}

void ULevelSequenceEditorBlueprintLibrary::Play()
{
	const bool bTogglePlay = false;
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->OnPlay(bTogglePlay);
	}
}

void ULevelSequenceEditorBlueprintLibrary::Pause()
{
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->Pause();
	}
}

void ULevelSequenceEditorBlueprintLibrary::SetCurrentTime(int32 NewFrame)
{
	if (CurrentSequencer.IsValid())
	{
		FFrameRate DisplayRate = CurrentSequencer.Pin()->GetFocusedDisplayRate();
		FFrameRate TickResolution = CurrentSequencer.Pin()->GetFocusedTickResolution();

		CurrentSequencer.Pin()->SetGlobalTime(ConvertFrameTime(NewFrame, DisplayRate, TickResolution));
	}
}

int32 ULevelSequenceEditorBlueprintLibrary::GetCurrentTime()
{
	if (CurrentSequencer.IsValid())
	{
		FFrameRate DisplayRate = CurrentSequencer.Pin()->GetFocusedDisplayRate();
		FFrameRate TickResolution = CurrentSequencer.Pin()->GetFocusedTickResolution();

		return ConvertFrameTime(CurrentSequencer.Pin()->GetGlobalTime().Time, TickResolution, DisplayRate).FloorToFrame().Value;
	}
	return 0;
}

void ULevelSequenceEditorBlueprintLibrary::SetCurrentLocalTime(int32 NewFrame)
{
	if (CurrentSequencer.IsValid())
	{
		FFrameRate DisplayRate = CurrentSequencer.Pin()->GetFocusedDisplayRate();
		FFrameRate TickResolution = CurrentSequencer.Pin()->GetFocusedTickResolution();

		CurrentSequencer.Pin()->SetLocalTime(ConvertFrameTime(NewFrame, DisplayRate, TickResolution));
	}
}

int32 ULevelSequenceEditorBlueprintLibrary::GetCurrentLocalTime()
{
	if (CurrentSequencer.IsValid())
	{
		FFrameRate DisplayRate = CurrentSequencer.Pin()->GetFocusedDisplayRate();
		FFrameRate TickResolution = CurrentSequencer.Pin()->GetFocusedTickResolution();

		return ConvertFrameTime(CurrentSequencer.Pin()->GetLocalTime().Time, TickResolution, DisplayRate).FloorToFrame().Value;
	}
	return 0;
}

void ULevelSequenceEditorBlueprintLibrary::PlayTo(FMovieSceneSequencePlaybackParams PlaybackParams)
{
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->PlayTo(PlaybackParams);
	}
}

bool ULevelSequenceEditorBlueprintLibrary::IsPlaying()
{
	if (CurrentSequencer.IsValid())
	{
		return CurrentSequencer.Pin()->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing;
	}
	return false;
}

TArray<UMovieSceneTrack*> ULevelSequenceEditorBlueprintLibrary::GetSelectedTracks()
{
	TArray<UMovieSceneTrack*> OutSelectedTracks;
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->GetSelectedTracks(OutSelectedTracks);
	}
	return OutSelectedTracks;
}

TArray<UMovieSceneSection*> ULevelSequenceEditorBlueprintLibrary::GetSelectedSections()
{
	TArray<UMovieSceneSection*> OutSelectedSections;
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->GetSelectedSections(OutSelectedSections);
	}
	return OutSelectedSections;
}

TArray<FSequencerChannelProxy> ULevelSequenceEditorBlueprintLibrary::GetSelectedChannels()
{
	TArray<FSequencerChannelProxy> OutSelectedChannels;
	if (CurrentSequencer.IsValid())
	{
		TArray<const IKeyArea*> SelectedKeyAreas;

		CurrentSequencer.Pin()->GetSelectedKeyAreas(SelectedKeyAreas);

		for (const IKeyArea* KeyArea : SelectedKeyAreas)
		{
			if (KeyArea)
			{
				FSequencerChannelProxy ChannelProxy(KeyArea->GetName(), KeyArea->GetOwningSection());
				OutSelectedChannels.Add(ChannelProxy);
			}
		}
	}
	return OutSelectedChannels;
}

TArray<UMovieSceneFolder*> ULevelSequenceEditorBlueprintLibrary::GetSelectedFolders()
{
	TArray<UMovieSceneFolder*> OutSelectedFolders;
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->GetSelectedFolders(OutSelectedFolders);
	}
	return OutSelectedFolders;
}

TArray<FGuid> ULevelSequenceEditorBlueprintLibrary::GetSelectedObjects()
{
	TArray<FGuid> OutSelectedGuids;
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->GetSelectedObjects(OutSelectedGuids);
	}
	return OutSelectedGuids;
}

void ULevelSequenceEditorBlueprintLibrary::SelectTracks(const TArray<UMovieSceneTrack*>& Tracks)
{
	if (CurrentSequencer.IsValid())
	{
		for (UMovieSceneTrack* Track : Tracks)
		{
			CurrentSequencer.Pin()->SelectTrack(Track);
		}
	}
}

void ULevelSequenceEditorBlueprintLibrary::SelectSections(const TArray<UMovieSceneSection*>& Sections)
{
	if (CurrentSequencer.IsValid())
	{
		for (UMovieSceneSection* Section : Sections)
		{
			CurrentSequencer.Pin()->SelectSection(Section);
		}
	}
}

void ULevelSequenceEditorBlueprintLibrary::SelectChannels(const TArray<FSequencerChannelProxy>& Channels)
{
	if (CurrentSequencer.IsValid())
	{
		for (FSequencerChannelProxy ChannelProxy : Channels)
		{
			UMovieSceneSection* Section = ChannelProxy.Section;
			if (Section)
			{
				TArray<FName> ChannelNames;
				ChannelNames.Add(ChannelProxy.ChannelName);
				CurrentSequencer.Pin()->SelectByChannels(Section, ChannelNames, false, true);
			}
		}
	}
}

void ULevelSequenceEditorBlueprintLibrary::SelectFolders(const TArray<UMovieSceneFolder*>& Folders)
{
	if (CurrentSequencer.IsValid())
	{
		for (UMovieSceneFolder* Folder : Folders)
		{
			CurrentSequencer.Pin()->SelectFolder(Folder);
		}
	}
}

void ULevelSequenceEditorBlueprintLibrary::SelectObjects(TArray<FGuid> ObjectBindings)
{
	if (CurrentSequencer.IsValid())
	{
		for (FGuid ObjectBinding : ObjectBindings)
		{
			CurrentSequencer.Pin()->SelectObject(ObjectBinding);
		}
	}
}

void ULevelSequenceEditorBlueprintLibrary::EmptySelection()
{
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->EmptySelection();
	}
}

void ULevelSequenceEditorBlueprintLibrary::SetSequencer(TSharedRef<ISequencer> InSequencer)
{
	CurrentSequencer = TWeakPtr<ISequencer>(InSequencer);
}

void ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence()
{
	if (CurrentSequencer.IsValid())
	{
		CurrentSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
	}
}
	
TArray<UObject*> ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(FMovieSceneObjectBindingID ObjectBinding)
{
	TArray<UObject*> BoundObjects;
	if (CurrentSequencer.IsValid())
	{
		for (TWeakObjectPtr<> WeakObject : ObjectBinding.ResolveBoundObjects(CurrentSequencer.Pin()->GetFocusedTemplateID(), *CurrentSequencer.Pin()))
		{
			if (WeakObject.IsValid())
			{
				BoundObjects.Add(WeakObject.Get());
			}
		}

	}
	return BoundObjects;
}


bool ULevelSequenceEditorBlueprintLibrary::IsLevelSequenceLocked()
{
	if (CurrentSequencer.IsValid())
	{
		TSharedPtr<ISequencer> Sequencer = CurrentSequencer.Pin();
		UMovieSceneSequence* FocusedMovieSceneSequence = Sequencer->GetFocusedMovieSceneSequence();
		if (FocusedMovieSceneSequence) 
		{
			if (FocusedMovieSceneSequence->GetMovieScene()->IsReadOnly()) 
			{
				return true;
			}
			else
			{
				TArray<UMovieScene*> DescendantMovieScenes;
				MovieSceneHelpers::GetDescendantMovieScenes(Sequencer->GetFocusedMovieSceneSequence(), DescendantMovieScenes);

				for (UMovieScene* DescendantMovieScene : DescendantMovieScenes)
				{
					if (DescendantMovieScene && DescendantMovieScene->IsReadOnly())
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void ULevelSequenceEditorBlueprintLibrary::SetLockLevelSequence(bool bLock)
{
	if (CurrentSequencer.IsValid())
	{
		TSharedPtr<ISequencer> Sequencer = CurrentSequencer.Pin();

		if (Sequencer->GetFocusedMovieSceneSequence())
		{
			UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

			if (bLock != MovieScene->IsReadOnly()) 
			{
				MovieScene->Modify();
				MovieScene->SetReadOnly(bLock);
			}

			TArray<UMovieScene*> DescendantMovieScenes;
			MovieSceneHelpers::GetDescendantMovieScenes(Sequencer->GetFocusedMovieSceneSequence(), DescendantMovieScenes);

			for (UMovieScene* DescendantMovieScene : DescendantMovieScenes)
			{
				if (DescendantMovieScene && bLock != DescendantMovieScene->IsReadOnly())
				{
					DescendantMovieScene->Modify();
					DescendantMovieScene->SetReadOnly(bLock);
				}
			}

			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
		}
	}
}

