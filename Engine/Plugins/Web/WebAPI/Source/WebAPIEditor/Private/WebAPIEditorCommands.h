// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FWebAPIEditorCommands
	: public TCommands<FWebAPIEditorCommands>
{
public:
	FWebAPIEditorCommands()
		: TCommands<FWebAPIEditorCommands>(TEXT("WebAPIEditor"),
			NSLOCTEXT("Contexts", "WebAPIEditor", "WebAPI Definition Editor"),
			NAME_None,
			FEditorStyle::GetStyleSetName())
	{ }

	/** Generate command to execute code generation. */
	TSharedPtr<FUICommandInfo> Generate;

	/** Registers all associated commands. */
	virtual void RegisterCommands() override;
};
