// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetCommands.h"

#define LOCTEXT_NAMESPACE "IKRetargetCommands"

void FIKRetargetCommands::RegisterCommands()
{
	UI_COMMAND(GoToRetargetPose, "Go to Retarget Pose", "Stop playback and set Source and Target meshes to the Retarget Pose.", EUserInterfaceActionType::Button, FInputChord());
	
	UI_COMMAND(EditRetargetPose, "Edit Mode", "Enter into mode allowing manual editing of the target skeleton pose in the viewport.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetToRefPose, "Set to Ref Pose", "Sets the retarget pose to the mesh reference pose.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(NewRetargetPose, "New", "Create a new retarget pose for the target mesh.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteRetargetPose, "Delete", "Delete current retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RenameRetargetPose, "Rename", "Rename current retarget pose.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
