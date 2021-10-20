// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ControlRigEditorStyle.h"

class FControlRigEditModeCommands : public TCommands<FControlRigEditModeCommands>
{

public:
	FControlRigEditModeCommands() : TCommands<FControlRigEditModeCommands>
	(
		"ControlRigEditMode",
		NSLOCTEXT("Contexts", "RigAnimation", "Rig Animation"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		FControlRigEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}
	

	/** Toggles hiding all manipulators in the viewport */
	TSharedPtr< FUICommandInfo > ToggleManipulators;

	/** Reset Transforms for Controls */
	TSharedPtr< FUICommandInfo > ResetTransforms;

	/** Reset Transforms for Controls */
	TSharedPtr< FUICommandInfo > ResetAllTransforms;

	/** Clear Selection*/
	TSharedPtr< FUICommandInfo > ClearSelection;

	/** Frame selected elements */
	TSharedPtr<FUICommandInfo> FrameSelection;

	/** Increase Shape Size */
	TSharedPtr< FUICommandInfo > IncreaseControlShapeSize;

	/** Decrease Shape Size */
	TSharedPtr< FUICommandInfo > DecreaseControlShapeSize;

	/** Reset Shape Size */
	TSharedPtr< FUICommandInfo > ResetControlShapeSize;

	/** Toggle Shape Transform Edit*/
	TSharedPtr< FUICommandInfo > ToggleControlShapeTransformEdit;

	/** Opens up the space picker widget */
	TSharedPtr< FUICommandInfo > OpenSpacePickerWidget;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
