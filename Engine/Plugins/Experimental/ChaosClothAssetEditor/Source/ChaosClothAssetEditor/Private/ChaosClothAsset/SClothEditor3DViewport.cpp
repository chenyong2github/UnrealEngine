// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SClothEditor3DViewport.h"
#include "ChaosClothAsset/ClothEditor3DViewportClient.h"
#include "ChaosClothAsset/SClothEditor3DViewportToolBar.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "EditorModeTools.h"

#define LOCTEXT_NAMESPACE "SChaosClothAssetEditor3DViewport"

void SChaosClothAssetEditor3DViewport::BindCommands()
{
	SAssetEditorViewport::BindCommands();
	const FChaosClothAssetEditorCommands& CommandInfos = FChaosClothAssetEditorCommands::Get();

	CommandList->MapAction(
		CommandInfos.ToggleSimMeshWireframe,
		FExecuteAction::CreateLambda([this]() 
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->EnableSimMeshWireframe(!ClothViewportClient->SimMeshWireframeEnabled());
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->SimMeshWireframeEnabled(); }));

	CommandList->MapAction(
		CommandInfos.ToggleRenderMeshWireframe,
		FExecuteAction::CreateLambda([this]()
		{
			TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothViewportClient = StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client);
			ClothViewportClient->EnableRenderMeshWireframe(!ClothViewportClient->RenderMeshWireframeEnabled());
		}),
		FCanExecuteAction::CreateLambda([this]() { return true; }),
		FIsActionChecked::CreateLambda([this]() { return StaticCastSharedPtr<FChaosClothAssetEditor3DViewportClient>(Client)->RenderMeshWireframeEnabled(); }));
}

TSharedPtr<SWidget> SChaosClothAssetEditor3DViewport::MakeViewportToolbar()
{
	return SNew(SChaosClothAssetEditor3DViewportToolBar, SharedThis(this))
		.CommandList(CommandList);
}

#undef LOCTEXT_NAMESPACE
