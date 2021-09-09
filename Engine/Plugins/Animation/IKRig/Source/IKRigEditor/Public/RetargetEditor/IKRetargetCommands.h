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
	
	/** edit reference pose */
	TSharedPtr< FUICommandInfo > EditRetargetPose;

	/** new reference pose */
	TSharedPtr< FUICommandInfo > NewRetargetPose;

	/** delete reference pose */
	TSharedPtr< FUICommandInfo > DeleteRetargetPose;

	/** reset reference pose */
	TSharedPtr< FUICommandInfo > ResetRetargetPose;
	
	/** export animation */
	TSharedPtr< FUICommandInfo > ExportAnimation;

	/** initialize commands */
	virtual void RegisterCommands() override;
};
