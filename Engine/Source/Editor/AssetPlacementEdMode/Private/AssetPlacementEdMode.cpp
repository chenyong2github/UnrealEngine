// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdMode.h"
#include "AssetPlacementEdModeToolkit.h"
#include "InteractiveToolManager.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementEdModeCommands.h"

#include "Settings/LevelEditorMiscSettings.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

constexpr const TCHAR UAssetPlacementEdMode::AssetPlacementEdModeID[];

UAssetPlacementEdMode::UAssetPlacementEdMode()
{
	Info = FEditorModeInfo(UAssetPlacementEdMode::AssetPlacementEdModeID,
		LOCTEXT("AssetPlacementEdModeName", "Placement"),
		FSlateIcon(FAssetPlacementEdModeStyle::GetStyleSetName(), "PlacementBrowser.ShowAllContent"),
		GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode);
}

UAssetPlacementEdMode::~UAssetPlacementEdMode()
{
}

void UAssetPlacementEdMode::Enter()
{
	UEdMode::Enter();

	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();
}

void UAssetPlacementEdMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FAssetPlacementEdModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UAssetPlacementEdMode::GetModeCommands() const
{
	return FAssetPlacementEdModeCommands::Get().GetCommands();
}

#undef LOCTEXT_NAMESPACE
