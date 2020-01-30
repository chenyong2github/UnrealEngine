// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Framework/Commands/Commands.h"

class FDatasmithUICommands : public TCommands<FDatasmithUICommands>
{
public:
	FDatasmithUICommands();

	// TCommands interface
	virtual void RegisterCommands() override;
	// End of TCommands interface

	// Add a new command to the Datasmith menu
	static TSharedPtr< FUICommandInfo > AddMenuCommand(const FString& CommandName, const FText& Caption, const FText& Description);

	// Remove a command from the Datasmith menu
	static void RemoveMenuCommand(const TSharedPtr< FUICommandInfo >& Command);

public:
	TSharedPtr<FUICommandInfo> RepeatLastImport;
	TArray<TSharedPtr<FUICommandInfo>> MenuCommands;
};
