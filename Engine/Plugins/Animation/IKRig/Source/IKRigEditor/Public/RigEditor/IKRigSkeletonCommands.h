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
	/** delete selected goal */
	TSharedPtr< FUICommandInfo > Delete;
	/** add goal to selected solvers */
	TSharedPtr< FUICommandInfo > ConnectGoalToSolvers;
	/** remove goal to selected solvers */
	TSharedPtr< FUICommandInfo > DisconnectGoalFromSolvers;

	/** set root bone on selected solvers */
	TSharedPtr< FUICommandInfo > SetRootBoneOnSolvers;
	
	/** add bone setting to bone */
	TSharedPtr< FUICommandInfo > AddBoneSettings;
	/** remove bone setting from bone */
	TSharedPtr< FUICommandInfo > RemoveBoneSettings;

	/** initialize commands */
	virtual void RegisterCommands() override;
};
