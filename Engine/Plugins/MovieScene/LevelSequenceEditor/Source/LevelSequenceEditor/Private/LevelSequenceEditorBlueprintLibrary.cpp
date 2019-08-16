// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorBlueprintLibrary.h"

#include "ISequencer.h"
#include "LevelSequence.h"

#include "Modules/ModuleManager.h"
#include "LevelEditor.h"

namespace
{
	static TWeakPtr<ISequencer> CurrentSequencer;
}

bool ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(ULevelSequence* LevelSequence)
{
	return FAssetEditorManager::Get().OpenEditorForAsset(LevelSequence);
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
		FAssetEditorManager::Get().CloseAllEditorsForAsset(CurrentSequencer.Pin()->GetRootMovieSceneSequence());
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

bool ULevelSequenceEditorBlueprintLibrary::IsPaused()
{
	if (CurrentSequencer.IsValid())
	{
		return CurrentSequencer.Pin()->GetPlaybackStatus() == EMovieScenePlayerStatus::Paused;
	}
	return false;
}

void ULevelSequenceEditorBlueprintLibrary::SetSequencer(TSharedRef<ISequencer> InSequencer)
{
	CurrentSequencer = TWeakPtr<ISequencer>(InSequencer);
}