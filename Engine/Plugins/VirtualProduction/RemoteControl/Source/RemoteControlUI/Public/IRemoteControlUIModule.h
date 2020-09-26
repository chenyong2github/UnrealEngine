// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGenerateExtensions, TArray<TSharedRef<class SWidget>>& /*OutExtensions*/);

/**
 * A Remote Control module that allows exposing objects and properties from the editor.
 */
class IRemoteControlUIModule : public IModuleInterface
{
public:
	/** 
	 * Get the toolbar extension generators.
	 * Usage: Bind a handler that adds a widget to the out array parameter.
	 */
	virtual FOnGenerateExtensions& GetExtensionGenerators() = 0;
};
