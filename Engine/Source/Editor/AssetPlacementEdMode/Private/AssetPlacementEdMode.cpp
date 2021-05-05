// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdMode.h"
#include "AssetPlacementEdModeToolkit.h"
#include "InteractiveToolManager.h"
#include "AssetPlacementEdModeStyle.h"
#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementSettings.h"
#include "EdModeInteractiveToolsContext.h"
#include "EditorModeManager.h"
#include "InstancedFoliageActor.h"
#include "Selection.h"
#include "EngineUtils.h"

#include "Tools/AssetEditorContextInterface.h"
#include "Tools/PlacementSelectTool.h"
#include "Tools/PlacementLassoSelectTool.h"
#include "Tools/PlacementPlaceTool.h"
#include "Tools/PlacementPlaceSingleTool.h"
#include "Tools/PlacementEraseTool.h"

#include "Factories/AssetFactoryInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementCommonActions.h"
#include "Elements/Framework/TypedElementRegistry.h"

#include "Settings/LevelEditorMiscSettings.h"
#include "Modes/PlacementModeSubsystem.h"

#include "InstancedFoliageActor.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

constexpr TCHAR UAssetPlacementEdMode::AssetPlacementEdModeID[];

UAssetPlacementEdMode::UAssetPlacementEdMode()
{
	TAttribute<bool> IsEnabledAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([]() -> bool { return GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode; }));
	Info = FEditorModeInfo(UAssetPlacementEdMode::AssetPlacementEdModeID,
		LOCTEXT("AssetPlacementEdModeName", "Placement"),
		FSlateIcon(FAssetPlacementEdModeStyle::Get().GetStyleSetName(), "LevelEditor.AssetPlacementEdMode"),
		MoveTemp(IsEnabledAttr));
}

UAssetPlacementEdMode::~UAssetPlacementEdMode()
{
}

void UAssetPlacementEdMode::Enter()
{
	// Set the settings object before we call Super::Enter, since we're using a shared one from the subsystem.
	SettingsObject = const_cast<UAssetPlacementSettings*>(GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject());

	Super::Enter();

	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();
	RegisterTool(PlacementModeCommands.Select, UPlacementModeSelectTool::ToolName, NewObject<UPlacementModeSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Place, UPlacementModePlacementTool::ToolName, NewObject<UPlacementModePlacementToolBuilder>(this));
	RegisterTool(PlacementModeCommands.LassoSelect, UPlacementModeLassoSelectTool::ToolName, NewObject<UPlacementModeLassoSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.PlaceSingle, UPlacementModePlaceSingleTool::ToolName, NewObject<UPlacementModePlaceSingleToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Erase, UPlacementModeEraseTool::ToolName, NewObject<UPlacementModeEraseToolBuilder>(this));

	// Stash the current editor selection, since this mode will modify it.
	Owner->StoreSelection(AssetPlacementEdModeID);
	bIsInSelectionTool = false;

	// Disable undo tracking so that we can't accidentally undo ourselves out of the select mode and into an invalid state.
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);

	// Enable the select tool by default.
	ToolsContext->StartTool(UPlacementModeSelectTool::ToolName);
}

void UAssetPlacementEdMode::Exit()
{
	// Todo: remove with HISM element handles
	for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		// null component will clear all selection
		constexpr bool bAppendSelection = false;
		IFA->SelectInstance(nullptr, 0, bAppendSelection);
	}

	Super::Exit();

	// Restore the selection to the original state after all the tools have shutdown in UEdMode::Exit()
	// Since they can continue messing with selection states.
	Owner->RestoreSelection(AssetPlacementEdModeID);

	SettingsObjectAsPlacementSettings.Reset();
}

void UAssetPlacementEdMode::CreateToolkit()
{
	SettingsObjectAsPlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	Toolkit = MakeShared<FAssetPlacementEdModeToolkit>();
}

bool UAssetPlacementEdMode::UsesToolkits() const
{
	return true;
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UAssetPlacementEdMode::GetModeCommands() const
{
	return FAssetPlacementEdModeCommands::Get().GetCommands();
}

void UAssetPlacementEdMode::BindCommands()
{
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();

	CommandList->MapAction(PlacementModeCommands.Deselect,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::ClearSelection),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasActiveSelection));

	CommandList->MapAction(PlacementModeCommands.Delete,
		FExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::DeleteSelection),
		FCanExecuteAction::CreateUObject(this, &UAssetPlacementEdMode::HasActiveSelection));
}

bool UAssetPlacementEdMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	// Always allow deselection, for stashing selection set.
	if (!bInSelection)
	{
		return true;
	}

	// Otherwise, need to be in selection tool for selection to be allowed
	if (!bIsInSelectionTool)
	{
		return false;
	}

	// And we need to have a valid palette item.
	FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor);
	return GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(ActorHandle);
}

void UAssetPlacementEdMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	Super::OnToolStarted(Manager, Tool);

	bool bWasInSelectionTool = bIsInSelectionTool;
	FString ActiveToolName = GetToolManager()->GetActiveToolName(EToolSide::Mouse);
	bool bIsSinglePlaceTool = ActiveToolName == UPlacementModePlaceSingleTool::ToolName;
	if (ActiveToolName == UPlacementModeSelectTool::ToolName || ActiveToolName == UPlacementModeLassoSelectTool::ToolName || bIsSinglePlaceTool)
	{
		bIsInSelectionTool = true;
	}
	else
	{
		bIsInSelectionTool = false;
	}

	// Restore the selection if we're going into the selection tools.
	// Allow the selection to be empty if we're going into single place tool for a clean slate.
	bool bRestoreSelectionState = bIsInSelectionTool && !bWasInSelectionTool && !bIsSinglePlaceTool;
	if (bRestoreSelectionState)
	{
		Owner->RestoreSelection(UPlacementModeSelectTool::ToolName);

		// Todo: remove with HISM element handles
		for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
		{
			AInstancedFoliageActor* IFA = *It;
			IFA->ApplySelection(bIsInSelectionTool);
		}
	}
	else if (!bIsInSelectionTool)
	{
		// If we can't select, clear out the selection set for the active tool.
		ClearSelection();
	}
}

void UAssetPlacementEdMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	Super::OnToolEnded(Manager, Tool);

	// Always store the most recent selection, even if we are leaving single placement tool to preserve what the user was doing last.
	if (bIsInSelectionTool)
	{
		constexpr bool bClearSelection = false;
		Owner->StoreSelection(UPlacementModeSelectTool::ToolName, false);
	}
}

bool UAssetPlacementEdMode::UsesPropertyWidgets() const
{
	return IsInSelectionTool();
}

bool UAssetPlacementEdMode::ShouldDrawWidget() const
{
	if (!IsInSelectionTool())
	{
		return false;
	}

	return Super::ShouldDrawWidget();
}

bool UAssetPlacementEdMode::IsEnabled()
{
	return GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode;
}

void UAssetPlacementEdMode::DeleteSelection()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlacementDeleteAllSelected", "Delete Selected Assets"));

	if (UTypedElementCommonActions* CommonActions = Owner->GetToolkitHost()->GetCommonActions())
	{
		CommonActions->DeleteSelectedElements(Owner->GetEditorSelectionSet(), GetWorld(), FTypedElementDeletionOptions());
	}

	for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
	{
		if (AInstancedFoliageActor* FoliageActor = *It)
		{
			FoliageActor->ForEachFoliageInfo([](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
			{
				TArray<int32> SelectedIndices = FoliageInfo.SelectedIndices.Array();
				FoliageInfo.RemoveInstances(SelectedIndices, true);
				return true; // continue iteration
			});
		}
	}

	GetToolManager()->EndUndoTransaction();
}

void UAssetPlacementEdMode::ClearSelection()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlacementClearSelection", "Clear Selection"));
	if (UTypedElementSelectionSet* SelectionSet = Owner->GetEditorSelectionSet())
	{
		SelectionSet->ClearSelection(FTypedElementSelectionOptions());
	}

	// Todo - remove with instanced handles
	for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
	{
		if (AInstancedFoliageActor* FoliageActor = *It)
		{
			FoliageActor->ForEachFoliageInfo([](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
			{
				FoliageInfo.ClearSelection();
				return true; // continue iteration
			});
		}
	}
	GetToolManager()->EndUndoTransaction();
}

bool UAssetPlacementEdMode::HasAnyAssetsInPalette() const
{
	return SettingsObjectAsPlacementSettings.IsValid() ? (SettingsObjectAsPlacementSettings->PaletteItems.Num() > 0) : false;
}

bool UAssetPlacementEdMode::HasActiveSelection() const
{
	if (!HasAnyAssetsInPalette())
	{
		return false;
	}

	if (Owner->GetEditorSelectionSet()->HasSelectedElements())
	{
		return true;
	}

	for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
	{
		if (AInstancedFoliageActor* FoliageActor = *It)
		{
			bool bHasSelectedFoliage = false;
			FoliageActor->ForEachFoliageInfo([&bHasSelectedFoliage](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo)
				{
					bHasSelectedFoliage = (FoliageInfo.SelectedIndices.Num() > 0);
					return !bHasSelectedFoliage;
				});

			if (bHasSelectedFoliage)
			{
				return true;
			}
		}
	}

	return false;
}

bool UAssetPlacementEdMode::IsInSelectionTool() const
{
	return bIsInSelectionTool;
}

#undef LOCTEXT_NAMESPACE
