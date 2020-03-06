// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FUserInterfaceCommand
{
public:
	/** Executes the command. */
	static void Run();

protected:
	/**
	 * Initializes the Slate application.
	 */
	static void InitializeSlateApplication();

	/**
	 * Shuts down the Slate application.
	 */
	static void ShutdownSlateApplication();
};
