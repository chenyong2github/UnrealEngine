// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FMovieRenderPipelineSettingsModule : public IModuleInterface
{
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FMovieRenderPipelineSettingsModule, MovieRenderPipelineSettings);
