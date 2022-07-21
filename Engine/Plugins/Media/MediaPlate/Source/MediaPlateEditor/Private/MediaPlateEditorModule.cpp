// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateEditorModule.h"

#include "AssetTools/MediaPlateActions.h"
#include "Editor.h"
#include "ISequencerModule.h"
#include "LevelEditorViewport.h"
#include "MaterialList.h"
#include "MediaPlateComponent.h"
#include "MediaPlateCustomization.h"
#include "MediaPlateEditorStyle.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "Models/MediaPlateEditorCommands.h"
#include "PropertyEditorModule.h"
#include "Sequencer/MediaPlateTrackEditor.h"
#include "SLevelViewport.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Widgets/SMediaPlateEditorMaterial.h"

DEFINE_LOG_CATEGORY(LogMediaPlateEditor);

void FMediaPlateEditorModule::StartupModule()
{
	Style = MakeShareable(new FMediaPlateEditorStyle());

	FMediaPlateEditorCommands::Register();

	RegisterAssetTools();

	// Register customizations.
	MediaPlateName = UMediaPlateComponent::StaticClass()->GetFName();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(MediaPlateName,
		FOnGetDetailCustomizationInstance::CreateStatic(&FMediaPlateCustomization::MakeInstance));

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	TrackEditorBindingHandle = SequencerModule.RegisterPropertyTrackEditor<FMediaPlateTrackEditor>();

	// Add bottom extender for material item
	FMaterialList::OnAddMaterialItemViewExtraBottomWidget.AddLambda([](const TSharedRef<FMaterialItemView>& InMaterialItemView, UActorComponent* InCurrentComponent, IDetailLayoutBuilder& InDetailBuilder, TArray<TSharedPtr<SWidget>>& OutExtensions)
	{
		LLM_SCOPE_BYNAME("MediaPlate/MediaPlateEditor");
		OutExtensions.Add(SNew(SMediaPlateEditorMaterial, InMaterialItemView, InCurrentComponent));
	});

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMediaPlateEditorModule::OnPostEngineInit);
}

void FMediaPlateEditorModule::ShutdownModule()
{
	FMaterialList::OnAddMaterialItemViewExtraBottomWidget.RemoveAll(this);

	if (GEditor != nullptr)
	{
		UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
		if (EditorAssetSubsystem != nullptr)
		{
			EditorAssetSubsystem->GetOnExtractAssetFromFile().RemoveAll(this);
		}
	}

	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnRegisterTrackEditor(TrackEditorBindingHandle);
	}

	UnregisterAssetTools();

	// Unregister customizations.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(MediaPlateName);
}

void FMediaPlateEditorModule::Tick(float DeltaTime)
{
	bool bIsMediaPlatePlaying = false;

	// Loop through all our plates.
	for (int Index = 0; Index < ActiveMediaPlates.Num(); ++Index)
	{
		// Get the player.
		TWeakObjectPtr<UMediaPlateComponent>& PlatePtr = ActiveMediaPlates[Index];
		TObjectPtr<UMediaPlateComponent> MediaPlate = PlatePtr.Get();
		bool bIsMediaPlateToBeRemoved = true;
		if (MediaPlate != nullptr)
		{
			// Update sound component.
			TObjectPtr<UMediaSoundComponent> SoundComponent = MediaPlate->SoundComponent;
			if (SoundComponent != nullptr)
			{
				SoundComponent->UpdatePlayer();
			}

			// Get the player.
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
			if (MediaPlayer != nullptr)
			{
				bIsMediaPlateToBeRemoved = false;
				// Are we playing something?
				if (MediaPlayer->IsPlaying())
				{
					bIsMediaPlatePlaying = true;
					break;
				}
			}
		}

		// Is this player gone?
		if (bIsMediaPlateToBeRemoved)
		{
			ActiveMediaPlates.RemoveAtSwap(Index);
			--Index;
		}
	}

	// Is anything playing?
	if (bIsMediaPlatePlaying)
	{
		// Yes. Invalidate the viewport so we can see it.
		if (GCurrentLevelEditingViewportClient != nullptr)
		{
			GCurrentLevelEditingViewportClient->Invalidate();
		}
	}
}

void FMediaPlateEditorModule::MediaPlateStartedPlayback(TObjectPtr<UMediaPlateComponent> MediaPlate)
{
	if (MediaPlate != nullptr)
	{
		ActiveMediaPlates.Add(MediaPlate);
	}
}

bool FMediaPlateEditorModule::RemoveMediaSourceFromDragDropCache(UMediaSource* MediaSource)
{
	const FString* Key = MapFileToMediaSource.FindKey(MediaSource);
	bool bIsInCache = Key != nullptr;
	if (bIsInCache)
	{
		MapFileToMediaSource.Remove(*Key);
	}
	return bIsInCache;
}

void FMediaPlateEditorModule::RegisterAssetTools()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	RegisterAssetTypeAction(AssetTools, MakeShareable(new FMediaPlateActions(Style.ToSharedRef())));
}

void FMediaPlateEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	RegisteredAssetTypeActions.Add(Action);
}

void FMediaPlateEditorModule::UnregisterAssetTools()
{
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

	if (AssetToolsModule != nullptr)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();

		for (TSharedRef<IAssetTypeActions>& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}
}

void FMediaPlateEditorModule::OnPostEngineInit()
{
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (EditorAssetSubsystem != nullptr)
	{
		EditorAssetSubsystem->GetOnExtractAssetFromFile().AddRaw(this, &FMediaPlateEditorModule::ExtractAssetDataFromFiles);
	}
}

void FMediaPlateEditorModule::ExtractAssetDataFromFiles(const TArray<FString>& Files,
	TArray<FAssetData>& AssetDataArray)
{
	if (Files.Num() > 0)
	{
		// Do we have this already?
		UMediaSource* MediaSource = nullptr;
		TWeakObjectPtr<UMediaSource>* ExistingMediaSourcePtr = MapFileToMediaSource.Find(Files[0]);
		if (ExistingMediaSourcePtr != nullptr)
		{
			MediaSource = ExistingMediaSourcePtr->Get();
		}

		// If we dont have it then create one now.
		if (MediaSource == nullptr)
		{
			MediaSource = UMediaSource::SpawnMediaSourceForString(Files[0], GetTransientPackage());
			if (MediaSource != nullptr)
			{
				MapFileToMediaSource.Emplace(Files[0], MediaSource);
			}
		}

		// Return this via the array.
		if (MediaSource != nullptr)
		{
			FAssetData AssetData(MediaSource);
			AssetDataArray.Add(AssetData);
		}
	}
}

IMPLEMENT_MODULE(FMediaPlateEditorModule, MediaPlateEditor)
