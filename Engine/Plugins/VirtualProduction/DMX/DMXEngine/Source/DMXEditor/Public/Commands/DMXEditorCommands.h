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

	// Documentation related
	TSharedPtr< FUICommandInfo > GoToDocumentation;

	// Entity Editor related
	TSharedPtr< FUICommandInfo > AddNewEntityController;
	TSharedPtr< FUICommandInfo > AddNewEntityFixtureType;
	TSharedPtr< FUICommandInfo > AddNewEntityFixturePatch;
	
	TSharedPtr< FUICommandInfo > AddNewFixtureTypeMode;
	TSharedPtr< FUICommandInfo > AddNewModeFunction;

	// Level Editor Tool Bar related
	TSharedPtr<FUICommandInfo> OpenChannelsMonitor;
	TSharedPtr<FUICommandInfo> OpenActivityMonitor;
	TSharedPtr<FUICommandInfo> OpenOutputConsole;
	TSharedPtr<FUICommandInfo> ToggleReceiveDMX;
};

class DMXEDITOR_API FDMXEditorCommands
{
public:
	static void Register();

	static const FDMXEditorCommandsImpl& Get();

	static void Unregister();
};
