// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaSourceManagerEditor, Log, All);

/**
* Interface for the MediaSourceManagerEditor module.
*/
class MEDIASOURCEMANAGEREDITOR_API IMediaSourceManagerEditorModule
	: public IModuleInterface
{
public:

};
