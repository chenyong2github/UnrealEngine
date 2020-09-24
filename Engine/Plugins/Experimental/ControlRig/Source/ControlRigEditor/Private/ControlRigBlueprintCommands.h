// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ControlRigEditorStyle.h"

class FControlRigBlueprintCommands : public TCommands<FControlRigBlueprintCommands>
{
public:
	FControlRigBlueprintCommands() : TCommands<FControlRigBlueprintCommands>
	(
		"ControlRigBlueprint",
		NSLOCTEXT("Contexts", "Animation", "Rig Blueprint"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		FControlRigEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}
	
	/** Deletes the selected items and removes their nodes from the graph. */
	TSharedPtr< FUICommandInfo > DeleteItem;

	/** Toggle Execute the Graph */
	TSharedPtr< FUICommandInfo > ExecuteGraph;

	/** Toggle Auto Compilation in the Graph */
	TSharedPtr< FUICommandInfo > AutoCompileGraph;

	/** Toggle between this and the last event queue */
	TSharedPtr< FUICommandInfo > ToggleEventQueue;

	/** Enable the setup mode for the rig */
	TSharedPtr< FUICommandInfo > SetupEvent;

	/** Run the normal update graph */
	TSharedPtr< FUICommandInfo > UpdateEvent;

	/** Run the inverse graph */
	TSharedPtr< FUICommandInfo > InverseEvent;

	/** Run the inverse graph followed by the update graph */
	TSharedPtr< FUICommandInfo > InverseAndUpdateEvent;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
