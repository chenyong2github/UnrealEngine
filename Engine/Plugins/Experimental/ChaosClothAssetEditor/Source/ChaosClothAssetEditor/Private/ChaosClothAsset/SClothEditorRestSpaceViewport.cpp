// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SClothEditorRestSpaceViewport.h"
#include "SViewportToolBar.h"
#include "ChaosClothAsset/SClothEditorRestSpaceViewportToolBar.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "EditorModeManager.h"

// TODO evntually:
//#include "ClothEditorRestSpaceViewportClient.h"

#define LOCTEXT_NAMESPACE "SChaosClothAssetEditorRestSpaceViewport"

void SChaosClothAssetEditorRestSpaceViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();

	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	CommandList->MapAction(
		CommandInfos.TogglePatternMode,
		FExecuteAction::CreateLambda([this]()
	{
		const FEditorModeTools* const EditorModeTools = Client->GetModeTools();
		UChaosClothAssetEditorMode* const ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));

		if (ClothEdMode)
		{
			ClothEdMode->TogglePatternMode();
		}
	}),
	FCanExecuteAction::CreateLambda([this]() 
	{ 
		const FEditorModeTools* const EditorModeTools = Client->GetModeTools();
		const UChaosClothAssetEditorMode* const ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));

		if (ClothEdMode)
		{
			return ClothEdMode->CanTogglePatternMode();
		}

		return false; 
	}),
	EUIActionRepeatMode::RepeatDisabled);
}

TSharedPtr<SWidget> SChaosClothAssetEditorRestSpaceViewport::MakeViewportToolbar()
{
	return SNew(SChaosClothAssetEditorRestSpaceViewportToolBar, SharedThis(this))
		.CommandList(CommandList);
}


void SChaosClothAssetEditorRestSpaceViewport::OnFocusViewportToSelection()
{
	const FEditorModeTools* const EditorModeTools = Client->GetModeTools();
	const UChaosClothAssetEditorMode* const ClothEdMode = Cast<UChaosClothAssetEditorMode>(EditorModeTools->GetActiveScriptableMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId));

	if (ClothEdMode)
	{
		Client->FocusViewportOnBox(ClothEdMode->SelectionBoundingBox());
	}
}

#undef LOCTEXT_NAMESPACE
