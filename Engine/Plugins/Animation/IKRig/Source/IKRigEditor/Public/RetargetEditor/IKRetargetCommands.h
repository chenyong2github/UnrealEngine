// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "IKRetargetEditorStyle.h"

class FIKRetargetCommands : public TCommands<FIKRetargetCommands>
{
public:
	FIKRetargetCommands() : TCommands<FIKRetargetCommands>
	(
		"IKRetarget",
		NSLOCTEXT("Contexts", "IKRetarget", "IK Retarget"),
		NAME_None,
		FIKRetargetEditorStyle::Get().GetStyleSetName() // Icon Style Set
	)
	{}

	/** reset reference pose */
	TSharedPtr< FUICommandInfo > GoToRetargetPose;
	
	/** edit reference pose */
	TSharedPtr< FUICommandInfo > EditRetargetPose;

	/** reset reference pose */
	TSharedPtr< FUICommandInfo > SetToRefPose;

	/** new reference pose */
	TSharedPtr< FUICommandInfo > NewRetargetPose;

	/** delete reference pose */
	TSharedPtr< FUICommandInfo > DeleteRetargetPose;

	/** rename reference pose */
	TSharedPtr< FUICommandInfo > RenameRetargetPose;
	
	/** export animation */
	TSharedPtr< FUICommandInfo > ExportAnimation;

	/** initialize commands */
	virtual void RegisterCommands() override;
};
