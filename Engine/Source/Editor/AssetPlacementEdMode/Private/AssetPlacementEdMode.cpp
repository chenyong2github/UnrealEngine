// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdMode.h"
#include "AssetPlacementEdModeToolkit.h"
#include "InteractiveToolManager.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementEdModeCommands.h"
#include "EdModeInteractiveToolsContext.h"
#include "EditorModeManager.h"
#include "Selection.h"

#include "Tools/PlacementSelectTool.h"
#include "Tools/PlacementLassoSelectTool.h"
#include "Tools/PlacementPlaceTool.h"
#include "Tools/PlacementPlaceSingleTool.h"
#include "Tools/PlacementFillTool.h"
#include "Tools/PlacementEraseTool.h"

#include "Factories/AssetFactoryInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "Elements/Framework/EngineElementsLibrary.h"

#include "Settings/LevelEditorMiscSettings.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

constexpr TCHAR UAssetPlacementEdMode::AssetPlacementEdModeID[];

UAssetPlacementEdMode::UAssetPlacementEdMode()
{
	TAttribute<bool> IsEnabledAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([]() -> bool { return GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode; }));
	Info = FEditorModeInfo(UAssetPlacementEdMode::AssetPlacementEdModeID,
		LOCTEXT("AssetPlacementEdModeName", "Placement"),
		FSlateIcon(FAssetPlacementEdModeStyle::GetStyleSetName(), "PlacementBrowser.ShowAllContent"),
		MoveTemp(IsEnabledAttr));

	SettingsClass = UAssetPlacementSettings::StaticClass();
}

UAssetPlacementEdMode::~UAssetPlacementEdMode()
{
}

void UAssetPlacementEdMode::Enter()
{
	Super::Enter();

	UAssetPlacementSettings* PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);

	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();
	RegisterTool(PlacementModeCommands.Select, UPlacementModeSelectTool::ToolName, NewObject<UPlacementModeSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.LassoSelect, UPlacementModeLassoSelectTool::ToolName, NewObject<UPlacementModeLassoSelectToolBuilder>(this));

	UPlacementModePlacementToolBuilder* PlaceToolBuilder = NewObject<UPlacementModePlacementToolBuilder>(this);
	PlaceToolBuilder->PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	RegisterTool(PlacementModeCommands.Place, UPlacementModePlacementTool::ToolName, PlaceToolBuilder);

	UPlacementModePlaceSingleToolBuilder* SinglePlaceToolBuilder = NewObject<UPlacementModePlaceSingleToolBuilder>(this);
	SinglePlaceToolBuilder->PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	RegisterTool(PlacementModeCommands.PlaceSingle, UPlacementModePlaceSingleTool::ToolName, SinglePlaceToolBuilder);

	RegisterTool(PlacementModeCommands.Fill, UPlacementModeFillTool::ToolName, NewObject<UPlacementModeFillToolBuilder>(this));

	UPlacementModeEraseToolBuilder* EraseToolBuilder = NewObject<UPlacementModeEraseToolBuilder>(this);
	EraseToolBuilder->PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	RegisterTool(PlacementModeCommands.Erase, UPlacementModeEraseTool::ToolName, EraseToolBuilder);

	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Mouse, UPlacementModeSelectTool::ToolName);
	ToolsContext->ToolManager->ActivateTool(EToolSide::Mouse);

	// TODO - Stash existing selection and update to filter on valid things in the palette
	Owner->GetSelectedActors()->GetElementSelectionSet()->ClearSelection(FTypedElementSelectionOptions());
	Owner->GetSelectedComponents()->GetElementSelectionSet()->ClearSelection(FTypedElementSelectionOptions());
}

void UAssetPlacementEdMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FAssetPlacementEdModeToolkit(Cast<UAssetPlacementSettings>(SettingsObject)));
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

	CommandList->MapAction(PlacementModeCommands.Deselect,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::SelectAssets, EPaletteFilter::ActivePaletteOnly, ESelectMode::Deselect),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));

	CommandList->MapAction(PlacementModeCommands.Delete,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::DeleteAssets),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasAnyAssetsInPalette, EPaletteFilter::ActivePaletteOnly));
}

bool UAssetPlacementEdMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	// Need to be in selection tool for selection to be allowed
	if (GetToolManager()->GetActiveToolName(EToolSide::Mouse) != UPlacementModeSelectTool::ToolName)
	{
		return false;
	}

	UAssetPlacementSettings* PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor);

	return DoesPaletteSupportElement(ActorHandle, PlacementSettings->PaletteItems);
}

bool UAssetPlacementEdMode::UsesPropertyWidgets() const
{
	if (GetToolManager()->GetActiveToolName(EToolSide::Mouse) != UPlacementModeSelectTool::ToolName)
	{
		return false;
	}

	return true;
}

bool UAssetPlacementEdMode::ShouldDrawWidget() const
{
	if (GetToolManager()->GetActiveToolName(EToolSide::Mouse) != UPlacementModeSelectTool::ToolName)
	{
		return false;
	}

	return Super::ShouldDrawWidget();
}

bool UAssetPlacementEdMode::IsEnabled()
{
	return GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode;
}

bool UAssetPlacementEdMode::DoesPaletteSupportElement(const FTypedElementHandle& InElementToCheck, const TArray<FPaletteItem>& InPaletteToCheck)
{
	if (!InElementToCheck.IsSet())
	{
		return false;
	}

	for (const FPaletteItem& Item : InPaletteToCheck)
	{
		FAssetData FoundAssetDataFromFactory = Item.FactoryOverride->GetAssetDataFromElementHandle(InElementToCheck);
		if (FoundAssetDataFromFactory == Item.AssetData)
		{
			return true;
		}
	}

	return false;
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
	UAssetPlacementSettings* PlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	return (PlacementSettings->PaletteItems.Num() > 0);
}

#undef LOCTEXT_NAMESPACE
