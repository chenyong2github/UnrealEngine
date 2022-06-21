// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaSourceManagerEditor, Log, All);

class ISlateStyle;

/**
* Interface for the MediaSourceManagerEditor module.
*/
class MEDIASOURCEMANAGEREDITOR_API IMediaSourceManagerEditorModule
	: public IModuleInterface
{
public:

	/**
	 * Get the style used by this module.
	 **/
	virtual TSharedPtr<ISlateStyle> GetStyle() = 0;

};
