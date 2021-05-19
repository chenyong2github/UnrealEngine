// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"


class FOptimusToolsCommands : public TCommands<FOptimusToolsCommands>
{
public:
	FOptimusToolsCommands()
	    : TCommands<FOptimusToolsCommands>(TEXT("OptimusTools"), NSLOCTEXT("Contexts", "OptimusTools", "Optimus Tools"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;
	static const FOptimusToolsCommands& Get();

	// Modeling tools commands
	TSharedPtr<FUICommandInfo> ToggleModelingToolsMode;


};
