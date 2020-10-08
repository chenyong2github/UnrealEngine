// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdMode.h"
#include "AssetPlacementEdModeToolkit.h"
#include "InteractiveToolManager.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementEdModeCommands.h"
#include "EdModeInteractiveToolsContext.h"

#include "Tools/PlacementSelectTool.h"
#include "Tools/PlacementLassoSelectTool.h"
#include "Tools/PlacementPlaceTool.h"
#include "Tools/PlacementPlaceSingleTool.h"
#include "Tools/PlacementFillTool.h"
#include "Tools/PlacementEraseTool.h"
#include "Tools/PlacementReapplySettingsTool.h"

#include "Settings/LevelEditorMiscSettings.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

constexpr TCHAR UAssetPlacementEdMode::AssetPlacementEdModeID[];

UAssetPlacementEdMode::UAssetPlacementEdMode()
{
	Info = FEditorModeInfo(UAssetPlacementEdMode::AssetPlacementEdModeID,
		LOCTEXT("AssetPlacementEdModeName", "Placement"),
		FSlateIcon(FAssetPlacementEdModeStyle::GetStyleSetName(), "PlacementBrowser.ShowAllContent"),
		GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode);

	SettingsClass = UAssetPlacementSettings::StaticClass();
}

UAssetPlacementEdMode::~UAssetPlacementEdMode()
{
}

void UAssetPlacementEdMode::Enter()
{
	UEdMode::Enter();

	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();
	RegisterTool(PlacementModeCommands.Select, UPlacementModeSelectTool::ToolName, NewObject<UPlacementModeSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.LassoSelect, UPlacementModeLassoSelectTool::ToolName, NewObject<UPlacementModeLassoSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Place, UPlacementModePlacementTool::ToolName, NewObject<UPlacementModePlacementToolBuilder>(this));
	RegisterTool(PlacementModeCommands.PlaceSingle, UPlacementModePlaceSingleTool::ToolName, NewObject<UPlacementModeSelectAllToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Fill, UPlacementModeFillTool::ToolName, NewObject<UPlacementModeFillToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Erase, UPlacementModeEraseTool::ToolName, NewObject<UPlacementModeEraseToolBuilder>(this));
	RegisterTool(PlacementModeCommands.ReapplySettings, UPlacementModeReapplySettingsTool::ToolName, NewObject<UPlacementModeReapplySettingsToolBuilder>(this));

	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Mouse, UPlacementModeSelectTool::ToolName);
	ToolsContext->ToolManager->ActivateTool(EToolSide::Mouse);
}

void UAssetPlacementEdMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FAssetPlacementEdModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UAssetPlacementEdMode::GetModeCommands() const
{
	return FAssetPlacementEdModeCommands::Get().GetCommands();
}

void UAssetPlacementEdMode::BindCommands()
{
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();

	CommandList->MapAction(PlacementModeCommands.SelectAll,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::SelectAssets, EPaletteFilter::ActivePaletteOnly, ESelectMode::Select),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));

	CommandList->MapAction(PlacementModeCommands.SelectInvalid,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::SelectAssets, EPaletteFilter::InvalidInstances, ESelectMode::Select),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::InvalidInstances));

	CommandList->MapAction(PlacementModeCommands.Deselect,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::SelectAssets, EPaletteFilter::ActivePaletteOnly, ESelectMode::Deselect),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));

	CommandList->MapAction(PlacementModeCommands.Delete,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::DeleteAssets),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));

	CommandList->MapAction(PlacementModeCommands.MoveToActivePartition,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::MoveAssetToActivePartition),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));
}

void UAssetPlacementEdMode::SelectAssets(EPaletteFilter InSelectAllType, ESelectMode InSelectMode)
{
}

void UAssetPlacementEdMode::DeleteAssets()
{
}

void UAssetPlacementEdMode::MoveAssetToActivePartition()
{
}

bool UAssetPlacementEdMode::HasAnyAssetsInPalette(EPaletteFilter InSelectAllType)
{
	return false;
}

#undef LOCTEXT_NAMESPACE
