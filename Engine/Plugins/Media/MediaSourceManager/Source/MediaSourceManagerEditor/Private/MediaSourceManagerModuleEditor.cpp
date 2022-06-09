// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerEditorModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerEditorModule"

DEFINE_LOG_CATEGORY(LogMediaSourceManagerEditor);

/**
 * Implements the MediaSourceManagerEditor module.
 */
class FMediaSourceManagerEditorModule
	: public IMediaSourceManagerEditorModule
{
public:
	//~ IMediaSourceManagerEditorModule interface
	

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FMediaSourceManagerEditorModule, MediaSourceManagerEditor);

#undef LOCTEXT_NAMESPACE
