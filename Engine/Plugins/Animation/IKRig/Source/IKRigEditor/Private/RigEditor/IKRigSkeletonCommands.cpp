// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigSkeletonCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigHierarchyCommands"

void FIKRigSkeletonCommands::RegisterCommands()
{
	UI_COMMAND(NewGoal, "New IK Goal", "Add new IK goal at the selected bone.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Delete, "Delete Goal or Effector", "Delete the selected IK Goal or Effector.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
	UI_COMMAND(ConnectGoalToSolvers, "Connect Goal to Selected Solvers", "Make the selected goal an effector in the selected solvers.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DisconnectGoalFromSolvers, "Disconnect Goal from Selected Solvers", "Remove effectors from the selected solvers that use this goal.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetRootBoneOnSolvers, "Set Root Bone on Selected Solvers", "Set the Root Bone setting on the selected solvers.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddBoneSettings, "Add Settings to Selected Bone", "Apply settings to the selected bone for all selected solvers (defined per-solver for limits, stiffness etc).", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RemoveBoneSettings, "Remove Settings on Selected Bone", "Remove all settings on the selected bone in all selected solvers.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
