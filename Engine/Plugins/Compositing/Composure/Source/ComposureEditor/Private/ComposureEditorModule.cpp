// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComposureEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ISequencerModule.h"
#include "Sequencer/ComposurePostMoveSettingsPropertyTrackEditor.h"
#include "Sequencer/ComposureExportTrackEditor.h"
#include "ComposureBlueprintLibrary.h"
#include "ComposureActorLayerPropertyTypeCustomization.h"

DEFINE_LOG_CATEGORY(LogComposureEditor);

class FComposureEditorModule : public IModuleInterface
{
	FName ComposureActorLayerTypeName;

	static TSharedRef<IPropertyTypeCustomization> MakeCustomization()
	{
		return MakeShared<FComposureActorLayerPropertyTypeCustomization>();
	}

	virtual void StartupModule() override
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		CreatePostMoveSettingsPropertyTrackEditorHandle = SequencerModule.RegisterPropertyTrackEditor<FComposurePostMoveSettingsPropertyTrackEditor>();
		ComposureExportTrackEditorHandle               = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateLambda([](TSharedRef<ISequencer> In){ return MakeShared<FComposureExportTrackEditor>(In); }));

		ComposureActorLayerTypeName = FComposureActorLayer::StaticStruct()->GetFName();

		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomPropertyTypeLayout(ComposureActorLayerTypeName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(MakeCustomization));
	}

	virtual void ShutdownModule() override
	{
		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule != nullptr)
		{
			SequencerModule->UnRegisterTrackEditor(CreatePostMoveSettingsPropertyTrackEditorHandle);
			SequencerModule->UnRegisterTrackEditor(ComposureExportTrackEditorHandle);
		}

		FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

		if (PropertyModule && ComposureActorLayerTypeName != NAME_None)
		{
			PropertyModule->UnregisterCustomPropertyTypeLayout(ComposureActorLayerTypeName);
		}
	}

private:

	FDelegateHandle CreatePostMoveSettingsPropertyTrackEditorHandle;
	FDelegateHandle ComposureExportTrackEditorHandle;
};

IMPLEMENT_MODULE(FComposureEditorModule, ComposureEditor )