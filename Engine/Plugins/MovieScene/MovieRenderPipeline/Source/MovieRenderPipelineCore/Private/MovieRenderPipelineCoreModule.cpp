// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineCoreModule.h"
#include "Modules/ModuleInterface.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "MovieRenderPipelineDataTypes.h"

void FMovieRenderPipelineCoreModule::StartupModule()
{
	// Look to see if they supplied arguments on the command line indicating they wish to render a movie.
	if (IsTryingToRenderMovieFromCommandLine(SequenceAssetValue, SettingsAssetValue, MoviePipelineLocalExecutorClassType, MoviePipelineClassType))
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Detected that the user intends to render a movie. Waiting until engine loop init is complete to ensure "))
		// Register a hook to wait until the engine has finished loading to increase the likelihood that the desired classes are loaded.
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMovieRenderPipelineCoreModule::InitializeCommandLineMovieRender);
	}
}

void FMovieRenderPipelineCoreModule::ShutdownModule()
{
}

FDelegateHandle FMovieRenderPipelineCoreModule::RegisterEngineRenderPass(FOnCreateEngineRenderPass InOnCreateEngineRenderPass)
{
	EngineRenderPassDelegates.Add(InOnCreateEngineRenderPass);
	FDelegateHandle Handle = EngineRenderPassDelegates.Last().GetHandle();

	return Handle;
}

void FMovieRenderPipelineCoreModule::UnregisterEngineRenderPass(FDelegateHandle InHandle)
{
	EngineRenderPassDelegates.RemoveAll([=](const FOnCreateEngineRenderPass& Delegate) { return Delegate.GetHandle() == InHandle; });
}

IMPLEMENT_MODULE(FMovieRenderPipelineCoreModule, MovieRenderPipelineCore);
DEFINE_LOG_CATEGORY(LogMovieRenderPipeline); 
