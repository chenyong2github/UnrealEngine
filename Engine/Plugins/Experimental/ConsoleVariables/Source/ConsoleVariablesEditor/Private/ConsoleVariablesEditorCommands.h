// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"


class FConsoleVariablesEditorCommands
	: public TCommands<FConsoleVariablesEditorCommands>
{
public:
	FConsoleVariablesEditorCommands()
		: TCommands<FConsoleVariablesEditorCommands>(TEXT("ConsoleVariablesEditor"),
			NSLOCTEXT("Contexts", "ConsoleVariablesEditor", "Console Variables Editor"), NAME_None, FEditorStyle::GetStyleSetName())
	{ }

	virtual void RegisterCommands() override;
	TSharedPtr<FUICommandInfo> OpenConsoleVariablesEditorMenuItem;
};
