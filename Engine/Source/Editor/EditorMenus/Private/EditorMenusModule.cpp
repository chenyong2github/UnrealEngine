// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IEditorMenusModule.h"


/**
 * Implements the Editor menus module.
 */
class FEditorMenusModule
	: public IEditorMenusModule
{
public:
	
	// IEditorMenusModule interface

	virtual void StartupModule( ) override
	{
	}

	virtual void ShutdownModule( ) override
	{
	}

	// End IModuleInterface interface
};


IMPLEMENT_MODULE(FEditorMenusModule, EditorMenus)
