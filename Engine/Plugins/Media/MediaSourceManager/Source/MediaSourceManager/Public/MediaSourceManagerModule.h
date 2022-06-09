// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaSourceManager, Log, All);

/**
* Interface for the MediaSourceManager module.
*/
class MEDIASOURCEMANAGER_API IMediaSourceManagerModule
	: public IModuleInterface
{
public:

};
