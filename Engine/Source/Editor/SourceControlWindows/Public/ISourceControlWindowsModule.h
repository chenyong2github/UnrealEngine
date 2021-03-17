// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * SourceControlWindows module interface
 */
class ISourceControlWindowsModule : public IModuleInterface
{
public:
	/**
	 * Get reference to the SourceControlWindows module instance
	 */
	static inline ISourceControlWindowsModule& Get()
	{
		return FModuleManager::LoadModuleChecked<ISourceControlWindowsModule>("SourceControlWindows");
	}

	virtual void ShowChangelistsTab() = 0;
	virtual bool CanShowChangelistsTab() const = 0;
};
