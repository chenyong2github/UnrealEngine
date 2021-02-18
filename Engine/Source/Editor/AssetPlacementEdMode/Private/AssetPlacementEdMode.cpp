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

#include "Tools/PlacementSelectTool.h"
#include "Tools/PlacementLassoSelectTool.h"
#include "Tools/PlacementPlaceTool.h"
#include "Tools/PlacementPlaceSingleTool.h"
#include "Tools/PlacementEraseTool.h"

#include "Factories/AssetFactoryInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementAssetDataInterface.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"

#include "Settings/LevelEditorMiscSettings.h"
#include "Modes/PlacementModeSubsystem.h"

#include "Subsystems/EditorActorSubsystem.h"
#include "InstancedFoliageActor.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

constexpr TCHAR UAssetPlacementEdMode::AssetPlacementEdModeID[];

UAssetPlacementEdMode::UAssetPlacementEdMode()
{
	TAttribute<bool> IsEnabledAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([]() -> bool { return GetDefault<ULevelEditorMiscSettings>()->bEnableAssetPlacementMode; }));
	Info = FEditorModeInfo(UAssetPlacementEdMode::AssetPlacementEdModeID,
		LOCTEXT("AssetPlacementEdModeName", "Placement"),
		FSlateIcon(FAssetPlacementEdModeStyle::GetStyleSetName(), "PlacementBrowser.ShowAllContent"),
		MoveTemp(IsEnabledAttr));

	bIsInSelectionTool = false;
}

UAssetPlacementEdMode::~UAssetPlacementEdMode()
{
}

void UAssetPlacementEdMode::Enter()
{
	// Set the settings object before we call Super::Enter, so that it's available for hooking up in the toolkit
	SettingsObject = const_cast<UAssetPlacementSettings*>(GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject().Get());

	Super::Enter();

	const FAssetPlacementEdModeCommands& PlacementModeCommands = FAssetPlacementEdModeCommands::Get();
	RegisterTool(PlacementModeCommands.Select, UPlacementModeSelectTool::ToolName, NewObject<UPlacementModeSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Place, UPlacementModePlacementTool::ToolName, NewObject<UPlacementModePlacementToolBuilder>(this));
	RegisterTool(PlacementModeCommands.LassoSelect, UPlacementModeLassoSelectTool::ToolName, NewObject<UPlacementModeLassoSelectToolBuilder>(this));
	RegisterTool(PlacementModeCommands.PlaceSingle, UPlacementModePlaceSingleTool::ToolName, NewObject<UPlacementModePlaceSingleToolBuilder>(this));
	RegisterTool(PlacementModeCommands.Erase, UPlacementModeEraseTool::ToolName, NewObject<UPlacementModeEraseToolBuilder>(this));

	// Enable the select tool by default.
	// Disable undo tracking so that we can't accidentally undo ourselves out of the select mode and into an invalid state.
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	ToolsContext->StartTool(UPlacementModeSelectTool::ToolName);
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::FullUndoRedo);

	// Stash the current editor selection, since this mode will modify it.
	Owner->StoreSelection(AssetPlacementEdModeID);
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

	// Restore the selection to the original state.
	Owner->RestoreSelection(AssetPlacementEdModeID);

	SettingsObjectAsPlacementSettings.Reset();

	Super::Exit();
}

void UAssetPlacementEdMode::CreateToolkit()
{
	SettingsObjectAsPlacementSettings = Cast<UAssetPlacementSettings>(SettingsObject);
	Toolkit = MakeShareable(new FAssetPlacementEdModeToolkit(SettingsObjectAsPlacementSettings));
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
	if (ActiveToolName == UPlacementModeSelectTool::ToolName || ActiveToolName == UPlacementModeLassoSelectTool::ToolName)
	{
		bIsInSelectionTool = true;
	}
	else
	{
		bIsInSelectionTool = false;
	}

	// Stash or pop selection
	if (bWasInSelectionTool != bIsInSelectionTool)
	{
		if (bIsInSelectionTool)
		{
			Owner->RestoreSelection(UPlacementModeSelectTool::ToolName);
		}
		else
		{
			Owner->StoreSelection(UPlacementModeSelectTool::ToolName);
		}

		// Todo: remove with HISM element handles
		for (TActorIterator<AInstancedFoliageActor> It(GetWorld()); It; ++It)
		{
			AInstancedFoliageActor* IFA = *It;
			IFA->ApplySelection(bIsInSelectionTool);
		}
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

	// Todo - replace with delete in world interface and replace foliage with element handles
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	auto DeleteSelectedElement = [ActorSubsystem](const TTypedElement<UTypedElementObjectInterface>& InElementInterface)
	{
		if (InElementInterface.IsSet())
		{
			if (AActor* Actor = InElementInterface.GetObjectAs<AActor>())
			{
				if (ActorSubsystem)
				{
					ActorSubsystem->DestroyActor(Actor);
				}
			}
		}
	};

	// Gather a copy of the selected handles, since the delete operation will remove them from the selection set.
	TArray<FTypedElementHandle> SelectedElementHandles;
	Owner->GetEditorSelectionSet()->GetSelectedElementHandles(SelectedElementHandles, UTypedElementObjectInterface::StaticClass());
	for (const FTypedElementHandle& ElementHandle : SelectedElementHandles)
	{
		if (TTypedElement<UTypedElementObjectInterface> ObjectInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementObjectInterface>(ElementHandle))
		{
			DeleteSelectedElement(ObjectInterface);
		}
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
	return (SettingsObjectAsPlacementSettings->PaletteItems.Num() > 0);
}

bool UAssetPlacementEdMode::HasActiveSelection() const
{
	return HasAnyAssetsInPalette() && Owner->GetEditorSelectionSet()->HasSelectedElements();
}

bool UAssetPlacementEdMode::IsInSelectionTool() const
{
	return bIsInSelectionTool;
}

#undef LOCTEXT_NAMESPACE
