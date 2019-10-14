// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorBlueprintLibrary.h"

#include "ISequencer.h"
#include "LevelSequence.h"

#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "MovieSceneCommonHelpers.h"

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

bool ULevelSequenceEditorBlueprintLibrary::IsPlaying()
{
	if (CurrentSequencer.IsValid())
	{
		return CurrentSequencer.Pin()->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing;
	}
	return false;
}

void ULevelSequenceEditorBlueprintLibrary::SetSequencer(TSharedRef<ISequencer> InSequencer)
{
	CurrentSequencer = TWeakPtr<ISequencer>(InSequencer);
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

