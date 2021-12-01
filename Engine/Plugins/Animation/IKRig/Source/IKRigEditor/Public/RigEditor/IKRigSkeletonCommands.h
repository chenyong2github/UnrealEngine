// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "IKRigEditorStyle.h"

class FIKRigSkeletonCommands : public TCommands<FIKRigSkeletonCommands>
{
public:
	FIKRigSkeletonCommands() : TCommands<FIKRigSkeletonCommands>
	(
		"IKRigSkeleton",
		NSLOCTEXT("Contexts", "IKRigSkeleton", "IK Rig Skeleton"),
		NAME_None,
		FIKRigEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}
	
	/** create new goal on selected bone */
	TSharedPtr< FUICommandInfo > NewGoal;
	/** delete selected element */
	TSharedPtr< FUICommandInfo > DeleteElement;
	/** add goal to selected solvers */
	TSharedPtr< FUICommandInfo > ConnectGoalToSolvers;
	/** remove goal to selected solvers */
	TSharedPtr< FUICommandInfo > DisconnectGoalFromSolvers;

	/** set root bone on selected solvers */
	TSharedPtr< FUICommandInfo > SetRootBoneOnSolvers;

	/** set end bone on selected solvers (Pole Solvers only)*/
	TSharedPtr< FUICommandInfo > SetEndBoneOnSolvers;
	
	/** add bone setting to bone */
	TSharedPtr< FUICommandInfo > AddBoneSettings;
	/** remove bone setting from bone */
	TSharedPtr< FUICommandInfo > RemoveBoneSettings;
	
	/** remove bone from solve */
	TSharedPtr< FUICommandInfo > ExcludeBone;
	/** add bone to solve */
	TSharedPtr< FUICommandInfo > IncludeBone;

	/** create retarget chain */
	TSharedPtr< FUICommandInfo > NewRetargetChain;
	/** set the root of the retargeting */
	TSharedPtr< FUICommandInfo > SetRetargetRoot;

	/** rename selected goal */
	TSharedPtr< FUICommandInfo > RenameGoal;
	
	/** initialize commands */
	virtual void RegisterCommands() override;
};
