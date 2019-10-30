// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieRenderPipelineEditorModule.h"
class UMoviePipelineShotConfig;
class UMovieRenderPipelineConfig;
class UMoviePipelineExecutorBase;

class FMovieRenderPipelineEditorModule : public IMovieRenderPipelineEditorModule
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
private:
	void RegisterSettings();
	void UnregisterSettings();
	void PerformTestPipelineRender(const TArray<FString>& Args);
	void OnTestPipelineExecutorFinished(UMoviePipelineExecutorBase* InExecutor);

private:
	TArray<UMovieRenderPipelineConfig*> GenerateTestPipelineConfigs(FSoftObjectPath InSequence);
private:
	class UMoviePipeline* ActiveMoviePipeline;
	class UMovieRenderPipelineConfig* PipelineConfig;
	UMoviePipelineExecutorBase* Executor;
};
