// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"


class FConsoleVariablesEditorCommands
	: public TCommands<FConsoleVariablesEditorCommands>
{
public:
	FConsoleVariablesEditorCommands()
		: TCommands<FConsoleVariablesEditorCommands>(TEXT("ConsoleVariablesEditor"),
			NSLOCTEXT("Contexts", "ConsoleVariablesEditor", "Console Variables Editor"), NAME_None, FAppStyle::Get().GetStyleSetName())
	{ }

	virtual void RegisterCommands() override;
	TSharedPtr<FUICommandInfo> OpenConsoleVariablesEditorMenuItem;
};
