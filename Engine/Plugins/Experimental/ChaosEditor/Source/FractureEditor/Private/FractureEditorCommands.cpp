// Copyright Epic Games, Inc. All Rights Reserved.

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

	// View settings
	UI_COMMAND(ToggleShowBoneColors, "Colors", "Toggle Show Bone Colors", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Shift, EKeys::B) );
	UI_COMMAND(ViewUpOneLevel, "ViewUpOneLevel", "View Up One Level", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::W) );
	UI_COMMAND(ViewDownOneLevel, "ViewDownOneLevel", "View Down One Level", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::S) );
	UI_COMMAND(ExplodeMore, "ExplodeMore", "Explode 10% More", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::E) );
	UI_COMMAND(ExplodeLess, "ExplodeLess", "Explode 10% Less", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::Q) );

	// Fracture Tools
	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->IsChildOf(UFractureActionTool::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			TSubclassOf<UFractureActionTool> SubclassOf = (*ClassIterator);
			UFractureActionTool* FractureTool = SubclassOf->GetDefaultObject<UFractureActionTool>();
			FractureTool->RegisterUICommand(this);
		}
	}

}

#undef LOCTEXT_NAMESPACE
