// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorMenus, Log, All);

class IEditorMenusModule : public IModuleInterface
{
public:

	/**
	 * Retrieve the module instance.
	 */
	static inline IEditorMenusModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IEditorMenusModule>("EditorMenus");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("EditorMenus");
	}
};
