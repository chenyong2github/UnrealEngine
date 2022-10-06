// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorModeToolkit.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FChaosClothAssetEditorModeToolkit"

FName FChaosClothAssetEditorModeToolkit::GetToolkitFName() const
{
	return FName("ChaosClothAssetEditorMode");
}

FText FChaosClothAssetEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("ChaosClothAssetEditorModeToolkit", "DisplayName", "ChaosClothAssetEditorMode");
}

void FChaosClothAssetEditorModeToolkit::BuildToolPalette(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	const FChaosClothAssetEditorCommands& Commands = FChaosClothAssetEditorCommands::Get();

	if (PaletteIndex == FBaseCharacterFXEditorModeToolkit::ToolsTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeightMapPaintTool);
	}
}

const FSlateBrush* FChaosClothAssetEditorModeToolkit::GetActiveToolIcon(const FString& ActiveToolIdentifier) const
{
	FName ActiveToolIconName = ISlateStyle::Join(FChaosClothAssetEditorCommands::Get().GetContextName(), TCHAR_TO_ANSI(*ActiveToolIdentifier));
	return FChaosClothAssetEditorStyle::Get().GetOptionalBrush(ActiveToolIconName);
}

#undef LOCTEXT_NAMESPACE
