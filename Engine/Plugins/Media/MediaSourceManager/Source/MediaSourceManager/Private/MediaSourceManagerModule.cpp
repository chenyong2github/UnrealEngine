// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerModule"

DEFINE_LOG_CATEGORY(LogMediaSourceManager);

/**
 * Implements the MediaSourceManager module.
 */
class FMediaSourceManagerModule
	: public IMediaSourceManagerModule
{
public:
	//~ IMediaSourceManagerModule interface
	

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FMediaSourceManagerModule, MediaSourceManager);

#undef LOCTEXT_NAMESPACE
