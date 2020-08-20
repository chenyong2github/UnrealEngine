// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailEditorModeModule.h"
#include "MotionTrailEditorMode.h"

#include "ISequencerModule.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "UnrealEdGlobals.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"

#define LOCTEXT_NAMESPACE "FMotionTrailEditorModeModule"

void FMotionTrailEditorModeModule::StartupModule()
{
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateRaw(this, &FMotionTrailEditorModeModule::OnSequencerCreated));
}

void FMotionTrailEditorModeModule::ShutdownModule()
{
	ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}
}

void FMotionTrailEditorModeModule::OnSequencerCreated(TSharedRef<ISequencer> Sequencer)
{
	Sequencer->GetSelectionChangedTracks().AddLambda([](TArray<UMovieSceneTrack*> SelectedTracks) {
		for (UMovieSceneTrack* Track : SelectedTracks)
		{
			if (Track->GetClass() == UMovieScene3DTransformTrack::StaticClass() || Track->GetClass() == UMovieSceneControlRigParameterTrack::StaticClass())
			{
				if (!GLevelEditorModeTools().IsModeActive(FName(TEXT("MotionTrailEditorMode"))))
				{
					//GLevelEditorModeTools().ActivateMode(FName(TEXT("MotionTrailEditorMode")));
				}
			}
		}
	});
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMotionTrailEditorModeModule, MotionTrailEditorMode)