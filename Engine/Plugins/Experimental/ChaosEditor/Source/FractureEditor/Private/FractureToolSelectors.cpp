// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolSelectors.h"

#include "Editor.h"
#include "Engine/Selection.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollection.h"
#include "FractureSelectionTools.h"


#define LOCTEXT_NAMESPACE "FractureToolSelectionOps"


FText UFractureToolSelectAll::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectAll", "Select All"));
}

FText UFractureToolSelectAll::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectAllTooltip", "Selects all Bones in the GeometryCollection"));
}

FSlateIcon UFractureToolSelectAll::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectAll");
}

void UFractureToolSelectAll::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectAll", "Select All", "Selects all Bones in the GeometryCollection.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::A));
	BindingContext->SelectAll = UICommandInfo;
}

void UFractureToolSelectAll::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::AllGeometry);
	}
}

void UFractureToolSelectAll::SelectByMode(FFractureEditorModeToolkit* InToolkit, GeometryCollection::ESelectionMode SelectionMode)
{
	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());
	SelectionSet->GetSelectedObjects(SelectedActors);

	for (AActor* Actor : SelectedActors)
	{
		TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents(GeometryCollectionComponents);

		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
		{
			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			EditBoneColor.SelectBones(SelectionMode);
			InToolkit->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);
		}
	}
}



FText UFractureToolSelectNone::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectNone", "Select None"));
}

FText UFractureToolSelectNone::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectNoneTooltip", "Deselects all Bones in the GeometryCollection."));
}

FSlateIcon UFractureToolSelectNone::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectNone");
}

void UFractureToolSelectNone::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectNone", "Select None", "Deselects all Bones in the GeometryCollection.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::D));
	BindingContext->SelectNone = UICommandInfo;
}

void UFractureToolSelectNone::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::None);
	}
}


FText UFractureToolSelectNeighbors::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectNeighbors", "Select Neighbors"));
}

FText UFractureToolSelectNeighbors::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectNeighborsTooltip", "Select all bones adjacent to the currently selected bones."));
}

FSlateIcon UFractureToolSelectNeighbors::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectNeighbors");
}

void UFractureToolSelectNeighbors::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectNeighbors", "Select Neighbors", "Select all bones adjacent to the currently selected bones.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->SelectNeighbors = UICommandInfo;
}

void UFractureToolSelectNeighbors::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::Neighbors);
	}
}


FText UFractureToolSelectSiblings::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectSiblings", "Select Siblings"));
}

FText UFractureToolSelectSiblings::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectSiblingsTooltip", "Select all bones at the same levels as the currently selected bones."));
}

FSlateIcon UFractureToolSelectSiblings::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectSiblings");
}

void UFractureToolSelectSiblings::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectSiblings", "Select Siblings", "Select all bones at the same levels as the currently selected bones.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->SelectSiblings = UICommandInfo;
}

void UFractureToolSelectSiblings::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::Siblings);
	}
}


FText UFractureToolSelectAllInCluster::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectAllInCluster", "Select All In Cluster"));
}

FText UFractureToolSelectAllInCluster::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectAllInClusterTooltip", "Select all bones with the same parent as selected bones."));
}

FSlateIcon UFractureToolSelectAllInCluster::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectAllInCluster");
}

void UFractureToolSelectAllInCluster::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectAllInCluster", "Select All In Cluster", "Select all bones with the same parent as selected bones.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->SelectAllInCluster = UICommandInfo;
}

void UFractureToolSelectAllInCluster::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::AllInCluster);
	}
}


FText UFractureToolSelectInvert::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectInvert", "Invert Selection"));
}

FText UFractureToolSelectInvert::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSelectInvertTooltip", "Invert current selection of bones."));
}

FSlateIcon UFractureToolSelectInvert::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SelectInvert");
}

void UFractureToolSelectInvert::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SelectInvert", "Invert Selection", "Invert current selection of bones.", EUserInterfaceActionType::Button, FInputChord());
	BindingContext->SelectInvert = UICommandInfo;
}

void UFractureToolSelectInvert::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		SelectByMode(InToolkit.Pin().Get(), GeometryCollection::ESelectionMode::InverseGeometry);
	}
}

#undef LOCTEXT_NAMESPACE