// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDMXControlConsoleModule.h"

class FMenuBuilder;
class FSpawnTabArgs;
class SDockTab;


class FDMXControlConsoleModule
	: public IDMXControlConsoleModule
{
public:
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~End IModuleInterface interface

private:
	/** Registers Control Console actions in Level Editor Commands */
	static void RegisterControlConsoleActions();

	/** Registers an extender for the Level Editor Toolbar DMX Menu */
	static void RegisterDMXMenuExtender();

	/** Extends the the Level Editor Toolbar DMX Menu */
	static void ExtendDMXMenu(FMenuBuilder& MenuBuilder);

	/** Opens the ControlConsole */
	static void OpenControlConsole();

	/** Spawns the ControlConsole Tab */
	static TSharedRef<SDockTab> OnSpawnControlConsoleTab(const FSpawnTabArgs& InSpawnTabArgs);

	/** Name of the ControlConsole Tab  */
	static const FName ControlConsoleTabName;
};
