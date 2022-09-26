// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/Commands.h"
#include "CustomizableObjectPopulationEditorStyle.h"

class FCustomizableObjectPopulationEditorCommands : public TCommands<FCustomizableObjectPopulationEditorCommands>
{

public:
	FCustomizableObjectPopulationEditorCommands() : TCommands<FCustomizableObjectPopulationEditorCommands>
		(
			"CustomizableObjectPopulationEditor", // Context name for fast lookup
			NSLOCTEXT("Contexts", "CustomizableObjectPopulationEditor", "CustomizableObject Population Editor"), // Localized context name for displaying
			NAME_None, // Parent
			FCustomizableObjectPopulationEditorStyle::GetStyleSetName()
			)
	{
	}

	TSharedPtr< FUICommandInfo > Save;
	TSharedPtr< FUICommandInfo > TestPopulation;
	TSharedPtr< FUICommandInfo > GenerateInstances;
	TSharedPtr< FUICommandInfo > InspectSelectedInstance;
	TSharedPtr< FUICommandInfo > InspectSelectedSkeletalMesh;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

};
