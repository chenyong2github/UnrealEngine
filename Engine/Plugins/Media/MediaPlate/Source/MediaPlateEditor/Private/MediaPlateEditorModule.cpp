// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateEditorModule.h"

#include "AssetTools/MediaPlateActions.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "MediaPlate.h"
#include "MediaPlateCustomization.h"
#include "MediaPlateEditorStyle.h"
#include "MediaPlayer.h"
#include "Models/MediaPlateEditorCommands.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY(LogMediaPlateEditor);

void FMediaPlateEditorModule::StartupModule()
{
	Style = MakeShareable(new FMediaPlateEditorStyle());

	FMediaPlateEditorCommands::Register();

	RegisterAssetTools();

	// Register customizations.
	MediaPlateName = AMediaPlate::StaticClass()->GetFName();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(MediaPlateName,
		FOnGetDetailCustomizationInstance::CreateStatic(&FMediaPlateCustomization::MakeInstance));
}

void FMediaPlateEditorModule::ShutdownModule()
{
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
		TWeakObjectPtr<AMediaPlate>& PlatePtr = ActiveMediaPlates[Index];
		TObjectPtr<AMediaPlate> MediaPlate = PlatePtr.Get();
		bool bIsMediaPlateToBeRemoved = true;
		if (MediaPlate != nullptr)
		{
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

void FMediaPlateEditorModule::MediaPlateStartedPlayback(TObjectPtr<AMediaPlate> MediaPlate)
{
	if (MediaPlate != nullptr)
	{
		ActiveMediaPlates.Add(MediaPlate);
	}
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

IMPLEMENT_MODULE(FMediaPlateEditorModule, MediaPlateEditor)
