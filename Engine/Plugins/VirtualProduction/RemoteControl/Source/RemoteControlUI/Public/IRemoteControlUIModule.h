// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGenerateExtensions, TArray<TSharedRef<class SWidget>>& /*OutExtensions*/);

/**
 * Filter queried in order to determine if a property should be displayed.
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnDisplayExposeIcon, TSharedRef<IPropertyHandle> /*PropertyHandle*/);

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

	/**
	 * Add a property filter  that indicates if the property handle should be displayed or not.
	 * When queried, returning true will allow the expose icon to be displayed in the details panel, false will hide it.
	 * @Note This filter will be queried after the RemoteControlModule's own filters.
	 */
	virtual FGuid AddPropertyFilter(FOnDisplayExposeIcon OnDisplayExposeIcon) = 0;

	/**
	 * Remove a property filter using its id.
	 */
	virtual void RemovePropertyFilter(const FGuid& FilterId) = 0;
};
