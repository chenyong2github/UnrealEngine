// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Modules/ModuleInterface.h"

class SDockTab;
class SNaniteTools;
class UNaniteToolsArguments;
class FSpawnTabArgs;

DECLARE_LOG_CATEGORY_EXTERN(LogNaniteTools, Log, All);

/**
 * Struct Viewer module
 */
class FNaniteToolsModule : public IModuleInterface
{
public:
	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule();

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule();

private:
	TSharedRef<SDockTab> CreateTab(const FSpawnTabArgs& Args);

	void AssignToolWindow(const TSharedRef<SNaniteTools>& InToolWindow)
	{
		ToolWindow = InToolWindow;
	}

	TWeakPtr<class SNaniteTools> ToolWindow;
};
