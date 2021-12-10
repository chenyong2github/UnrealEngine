// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Framework/Commands/Commands.h"

/**
 * Defines commands for the Remote Control API which enables most functionality of the Remote Control Panel.
 */
class FRemoteControlCommands : public TCommands<FRemoteControlCommands>
{
public:

	FRemoteControlCommands();

	//~ BEGIN : TCommands<> Implementation(s)

	virtual void RegisterCommands() override;

	//~ END : TCommands<> Implementation(s)

	/**
	 * Holds the information about UI Command that toggles edit mode in RC Panel.
	 */
	TSharedPtr<FUICommandInfo> ToggleEditMode;
};
