// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"


/**
 * Advanced Widgets module
 */
class ADVANCEDWIDGETS_API FAdvancedWidgetsModule : public IModuleInterface
{
public:

	/**
	 * Called right after the plugin DLL has been loaded and the plugin object has been created
	 */
	virtual void StartupModule();

	/**
	 * Called before the plugin is unloaded, right before the plugin object is destroyed.
	 */
	virtual void ShutdownModule();

	/** Advanced Widgets app identifier string */
	static const FName AdvancedWidgetsAppIdentifier;
};
