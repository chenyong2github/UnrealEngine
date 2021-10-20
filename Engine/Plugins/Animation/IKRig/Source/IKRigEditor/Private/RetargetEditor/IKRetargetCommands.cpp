// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetCommands.h"

#define LOCTEXT_NAMESPACE "IKRetargetCommands"

void FIKRetargetCommands::RegisterCommands()
{
	UI_COMMAND(EditRetargetPose, "Edit Pose", "Edit retarget reference pose of target mesh. Usually best to match the source mesh.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(NewRetargetPose, "New Pose", "Create a new retarget pose for the target mesh.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteRetargetPose, "Delete Pose", "Delete a retarget pose.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetRetargetPose, "Reset Pose", "Resets a retarget pose to the mesh reference pose.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
