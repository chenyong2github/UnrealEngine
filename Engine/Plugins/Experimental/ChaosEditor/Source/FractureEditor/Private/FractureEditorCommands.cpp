// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureEditorCommands.h"

#include "FractureEditorStyle.h"
#include "FractureTool.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "FractureEditorCommands"

FFractureEditorCommands::FFractureEditorCommands()
	: TCommands<FFractureEditorCommands>("FractureEditor", LOCTEXT("Fracture", "Fracture"), NAME_None, FFractureEditorStyle::StyleName)
{
}

void FFractureEditorCommands::RegisterCommands()
{

	// Selection Commands
	UI_COMMAND(SelectAll, "SelectAll", "Selects all Bones in the GeometryCollection", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::A));
	UI_COMMAND(SelectNone, "SelectNone", "Deselects all Bones in the GeometryCollection", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::D));
	UI_COMMAND(SelectNeighbors, "SelectNeighbors", "Select all bones adjacent to the currently selected bones", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(SelectSiblings, "SelectSiblings", "Select all bones at the same levels as the currently selected bones", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(SelectAllInCluster, "SelectAllInCluster", "Select all bones with the same parent as selected bones", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(SelectInvert, "SelectInvert", "Invert the selected bones", EUserInterfaceActionType::Button, FInputChord() );

	// View settings
	UI_COMMAND(ToggleShowBoneColors, "ShowBoneColors", "Toggle Show Bone Colors", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::B) );
	UI_COMMAND(ViewUpOneLevel, "ViewUpOneLevel", "View Up One Level", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::W) );
	UI_COMMAND(ViewDownOneLevel, "ViewDownOneLevel", "View Down One Level", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::S) );
	UI_COMMAND(ExplodeMore, "ExplodeMore", "Explode 10% More", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::E) );
	UI_COMMAND(ExplodeLess, "ExplodeLess", "Explode 10% Less", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::Q) );

	// Cluster Operations
	UI_COMMAND(Flatten, "Flatten", "Flattens all bones to level 1", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(FlattenToLevel, "FlattenToLevel", "Flatten Only up to the Current View Level", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(AutoCluster, "AutoCluster", "Auto Cluster", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(Cluster, "Cluster", "Clusters selected bones under a new parent", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(Uncluster, "Uncluster", "Uncluster", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(Merge, "Merge", "Merge", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(MoveUp, "MoveUp", "Move Bones Up One Level", EUserInterfaceActionType::Button, FInputChord() );

	UI_COMMAND(GenerateAsset, "GenerateAsset", "Generate Geometry Collection Asset from static meshes contained in selected actors", EUserInterfaceActionType::Button, FInputChord() );


	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->IsChildOf(UFractureTool::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			TSubclassOf<UFractureTool> SubclassOf = (*ClassIterator);
			UFractureTool* FractureTool = SubclassOf->GetDefaultObject<UFractureTool>();
			FractureTool->RegisterUICommand(this);
		}
	}


}

#undef LOCTEXT_NAMESPACE
