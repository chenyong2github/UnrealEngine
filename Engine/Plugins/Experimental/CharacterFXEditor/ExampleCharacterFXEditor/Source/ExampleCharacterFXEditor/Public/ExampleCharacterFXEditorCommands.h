// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditorCommands.h"

class FExampleCharacterFXEditorCommands : public TBaseCharacterFXEditorCommands<FExampleCharacterFXEditorCommands>
{
public:

	FExampleCharacterFXEditorCommands();

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> OpenCharacterFXEditor;

	const static FString BeginAttributeEditorToolIdentifier;
	TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;
};
