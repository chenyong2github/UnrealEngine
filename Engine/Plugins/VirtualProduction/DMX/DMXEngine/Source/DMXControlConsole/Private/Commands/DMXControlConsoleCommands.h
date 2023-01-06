// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"


/** Defines commands for the DMX Control Console. */
class FDMXControlConsoleCommands
	: public TCommands<FDMXControlConsoleCommands>
{
public:
	/** Constructor */
	FDMXControlConsoleCommands();

	/** Registers commands for DMX Control Console */
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenControlConsole;

	TSharedPtr<FUICommandInfo> SendDMX;
	TSharedPtr<FUICommandInfo> StopDMX;
	TSharedPtr<FUICommandInfo> ClearAll;
};
