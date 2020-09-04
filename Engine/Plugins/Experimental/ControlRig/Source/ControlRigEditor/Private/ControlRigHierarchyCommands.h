// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ControlRigEditorStyle.h"

class FControlRigHierarchyCommands : public TCommands<FControlRigHierarchyCommands>
{
public:
	FControlRigHierarchyCommands() : TCommands<FControlRigHierarchyCommands>
	(
		"ControlRigHierarchy",
		NSLOCTEXT("Contexts", "RigHierarchy", "Rig Hierarchy"),
		NAME_None, // "MainFrame" // @todo Fix this crash
		FControlRigEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}
	
	/** Add Item at origin */
	TSharedPtr< FUICommandInfo > AddBoneItem;

	/** Add Item at origin */
	TSharedPtr< FUICommandInfo > AddControlItem;

	/** Add Item at origin */
	TSharedPtr< FUICommandInfo > AddSpaceItem;

	/** Duplicate currently selected items */
	TSharedPtr< FUICommandInfo > DuplicateItem;

	/** Mirror currently selected items */
	TSharedPtr< FUICommandInfo > MirrorItem;

	/** Delete currently selected items */
	TSharedPtr< FUICommandInfo > DeleteItem;

	/** Rename selected item */
	TSharedPtr< FUICommandInfo > RenameItem;

	/** Copy the selected items. */
	TSharedPtr< FUICommandInfo > CopyItems;

	/** Paste the selected items. */
	TSharedPtr< FUICommandInfo > PasteItems;

	/** Paste Local Transform", "Paste the local transforms. */
	TSharedPtr< FUICommandInfo > PasteLocalTransforms;

	/** Paste Global Transform", "Paste the global transforms. */
	TSharedPtr< FUICommandInfo > PasteGlobalTransforms;

	/* Reset transform */
	TSharedPtr<FUICommandInfo> ResetTransform;

	/* Reset all transforms */
	TSharedPtr<FUICommandInfo> ResetAllTransforms;

	/* Reset space */
	TSharedPtr<FUICommandInfo> ResetSpace;

	/* Set initial transform from current */
	TSharedPtr<FUICommandInfo> SetInitialTransformFromCurrentTransform;

	/* set initial transform from closest bone */
	TSharedPtr<FUICommandInfo> SetInitialTransformFromClosestBone;

	/* frames the selection in the tree */
	TSharedPtr<FUICommandInfo> FrameSelection;

	/* sets the bone transform using a gizmo */
	TSharedPtr<FUICommandInfo> ControlBoneTransform;

	/* sets the space transform using a gizmo */
	TSharedPtr<FUICommandInfo> ControlSpaceTransform;

	/* Unparents the selected elements from the hierarchy */
	TSharedPtr<FUICommandInfo> Unparent;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
