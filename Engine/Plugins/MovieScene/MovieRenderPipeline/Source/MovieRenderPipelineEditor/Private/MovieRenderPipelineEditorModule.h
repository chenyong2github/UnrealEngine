// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieRenderPipelineEditorModule.h"
class UMoviePipelineShotConfig;
class UMoviePipelineMasterConfig;
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
	void OnTestPipelineExecutorFinished(UMoviePipelineExecutorBase* InExecutor, bool bSuccess);

private:
	TArray<UMoviePipelineMasterConfig*> GenerateTestPipelineConfigs();
private:
	class UMoviePipeline* ActiveMoviePipeline;
	class UMoviePipelineMasterConfig* PipelineConfig;
	UMoviePipelineExecutorBase* Executor;
};
