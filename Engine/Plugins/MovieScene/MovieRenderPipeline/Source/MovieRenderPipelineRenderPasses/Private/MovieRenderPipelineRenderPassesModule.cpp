// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FMovieRenderPipelineRenderPassesModule : public IModuleInterface
{
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FMovieRenderPipelineRenderPassesModule, MovieRenderPipelineRenderPasses);

