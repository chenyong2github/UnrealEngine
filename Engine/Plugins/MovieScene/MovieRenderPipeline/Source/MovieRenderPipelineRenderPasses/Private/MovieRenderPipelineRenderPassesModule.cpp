// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineDeferredPasses.h"

class FMovieRenderPipelineRenderPassesModule : public IModuleInterface
{

	static TSharedRef<MoviePipeline::FMoviePipelineEnginePass> CreateDeferredEnginePass()
	{
		return MakeShared<MoviePipeline::FDeferredRenderEnginePass>();
	}

	virtual void StartupModule() override
	{
		FMovieRenderPipelineCoreModule& CoreModule = FModuleManager::Get().LoadModuleChecked<FMovieRenderPipelineCoreModule>("MovieRenderPipelineCore");
		DeferredEnginePassHandle = CoreModule.RegisterEngineRenderPass(FOnCreateEngineRenderPass::CreateStatic(&FMovieRenderPipelineRenderPassesModule::CreateDeferredEnginePass));
	}

	virtual void ShutdownModule() override
	{
		FMovieRenderPipelineCoreModule* CoreModule = (FMovieRenderPipelineCoreModule*)FModuleManager::Get().LoadModule("MovieRenderPipelineCore");
		if (CoreModule)
		{
			CoreModule->UnregisterEngineRenderPass(DeferredEnginePassHandle);
		}

	}
private:

	FDelegateHandle DeferredEnginePassHandle;
};

IMPLEMENT_MODULE(FMovieRenderPipelineRenderPassesModule, MovieRenderPipelineRenderPasses);

