// Copyright Epic Games, Inc. All Rights Reserved.

#include "ISequencerModule.h"
#include "LiveLinkCameraControllerTrackEditor.h"
#include "Modules/ModuleManager.h"

class FLiveLinkCameraRecordingModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		CreateLiveLinkCameraControllerTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FLiveLinkCameraControllerTrackEditor::CreateTrackEditor));
	}

	virtual void ShutdownModule() override
	{
		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule != nullptr)
		{
			SequencerModule->UnRegisterTrackEditor(CreateLiveLinkCameraControllerTrackEditorHandle);
		}
	}

private:

	FDelegateHandle CreateLiveLinkCameraControllerTrackEditorHandle;
};

IMPLEMENT_MODULE(FLiveLinkCameraRecordingModule, LiveLinkCameraRecording);