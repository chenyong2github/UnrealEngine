// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SDockTab;
class FSpawnTabArgs;

class FUserInterfaceCommand
{
public:

	/** Executes the command. */
	static void Run();

protected:

	/**
	 * Initializes the Slate application.
	 *
	 * @param LayoutInit The path to the layout configuration file.
	 */
	static void InitializeSlateApplication(const FString& LayoutIni);

	/**
	 * Shuts down the Slate application.
	 *
	 * @param LayoutInit The path to the layout configuration file.
	 */
	static void ShutdownSlateApplication(const FString& LayoutIni);

private:

	/** Callback for spawning tabs. */
	TSharedRef<SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier) const;
};
