// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigHierarchyCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigHierarchyCommands"

void FControlRigHierarchyCommands::RegisterCommands()
{
	UI_COMMAND(AddBoneItem, "New Bone", "Add new bone at the origin (0, 0, 0) to the hierarchy.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddControlItem, "New Control", "Add new control at the origin (0, 0, 0) to the hierarchy.", EUserInterfaceActionType::Button, FInputChord(EKeys::N, EModifierKey::Control));
	UI_COMMAND(AddSpaceItem, "New Space", "Add new space at the origin (0, 0, 0) to the hierarchy.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DuplicateItem, "Duplicate", "Duplicate the selected items in the hierarchy.", EUserInterfaceActionType::Button, FInputChord(EKeys::W, EModifierKey::Control));
	UI_COMMAND(MirrorItem, "Mirror", "Mirror the selected items in the hierarchy.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteItem, "Delete", "Deletes the selected items from the hierarchy.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
	UI_COMMAND(RenameItem, "Rename", "Rename the selected item.", EUserInterfaceActionType::Button, FInputChord(EKeys::F2));
	UI_COMMAND(CopyItems, "Copy", "Copy the selected items.", EUserInterfaceActionType::Button, FInputChord(EKeys::C, EModifierKey::Control));
	UI_COMMAND(PasteItems, "Paste", "Paste the selected items.", EUserInterfaceActionType::Button, FInputChord(EKeys::V, EModifierKey::Control));
	UI_COMMAND(PasteLocalTransforms, "Paste Local Transform", "Paste the local transforms.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PasteGlobalTransforms, "Paste Global Transform", "Paste the global transforms.", EUserInterfaceActionType::Button, FInputChord(EKeys::V, EModifierKey::Control | EModifierKey::Shift));
	UI_COMMAND(SetInitialTransformFromClosestBone, "Set Initial Transform from Closest Bone", "Find the Closest Bone to Initial Transform of the Selected Bones", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetInitialTransformFromCurrentTransform, "Set Initial Transform from Current", "Save the Current Transform To Initial Transform of the Selected Bones", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetTransform, "Reset Transform", "Reset the Transform", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control));
	UI_COMMAND(ResetAllTransforms, "Reset All Transforms", "Resets all Transforms", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control | EModifierKey::Shift));
	UI_COMMAND(ResetSpace, "Reset Space", "Resets or injects a Space below the Control", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(FrameSelection, "Frame Selection", "Expands and frames the selection in the tree", EUserInterfaceActionType::Button, FInputChord(EKeys::F));
	UI_COMMAND(ControlBoneTransform, "Control Bone Transform", "Sets the bone transform using a gizmo", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ControlSpaceTransform, "Control Space Transform", "Sets the space transform using a gizmo", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Unparent, "Unparent", "Unparents the selected elements from the hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::P, EModifierKey::Shift));
}

#undef LOCTEXT_NAMESPACE
