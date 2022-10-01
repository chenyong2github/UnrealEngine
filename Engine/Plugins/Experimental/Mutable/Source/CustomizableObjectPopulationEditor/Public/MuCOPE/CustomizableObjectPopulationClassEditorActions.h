// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/AppStyle.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/Commands.h"
#include "CustomizableObjectPopulationEditorStyle.h"


class FCustomizableObjectPopulationClassEditorCommands : public TCommands<FCustomizableObjectPopulationClassEditorCommands>
{
public:

	FCustomizableObjectPopulationClassEditorCommands() : TCommands<FCustomizableObjectPopulationClassEditorCommands>
		(
			"CustomizableObjectPopulationClassEditor", // Context name for fast lookup
			NSLOCTEXT("Contexts", "CustomizableObjectPopulationClassEditor", "CustomizableObject Population Class Editor"), // Localized context name for displaying
			NAME_None, // Parent
			FCustomizableObjectPopulationEditorStyle::GetStyleSetName()
			)
	{
	}

	TSharedPtr< FUICommandInfo > Save;
	TSharedPtr< FUICommandInfo > SaveCustomizableObject;
	TSharedPtr< FUICommandInfo > OpenCustomizableObjectEditor;
	TSharedPtr< FUICommandInfo > SaveEditorCurve;
	TSharedPtr< FUICommandInfo > TestPopulationClass;
	TSharedPtr< FUICommandInfo > GenerateInstances;
	TSharedPtr< FUICommandInfo > InspectSelectedInstance;
	TSharedPtr< FUICommandInfo > InspectSelectedSkeletalMesh;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

};
