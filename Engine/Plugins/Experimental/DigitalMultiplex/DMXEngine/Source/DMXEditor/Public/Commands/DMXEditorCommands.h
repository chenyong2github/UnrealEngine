// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class DMXEDITOR_API FDMXEditorCommandsImpl 
	: public TCommands<FDMXEditorCommandsImpl>
{
public:
	FDMXEditorCommandsImpl();

	virtual ~FDMXEditorCommandsImpl() {}

	//~ Begin TCommands implementation
	virtual void RegisterCommands() override;
	//~ End TCommands implementation

	// Go to node documentation
	TSharedPtr< FUICommandInfo > GoToDocumentation;

	// Create entites
	TSharedPtr< FUICommandInfo > AddNewEntityController;
	TSharedPtr< FUICommandInfo > AddNewEntityFixtureType;
	TSharedPtr< FUICommandInfo > AddNewEntityFixturePatch;
};

class DMXEDITOR_API FDMXEditorCommands
{
public:
	static void Register();

	static const FDMXEditorCommandsImpl& Get();

	static void Unregister();
};
