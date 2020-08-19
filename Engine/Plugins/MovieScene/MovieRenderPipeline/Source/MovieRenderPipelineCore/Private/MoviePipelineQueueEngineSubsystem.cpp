// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueueEngineSubsystem.h"
#include "Modules/ModuleManager.h"
#include "MoviePipeline.h"

UMoviePipelineExecutorBase* UMoviePipelineQueueEngineSubsystem::RenderQueueWithExecutor(TSubclassOf<UMoviePipelineExecutorBase> InExecutorType)
{
	RenderQueueWithExecutorInstance(NewObject<UMoviePipelineExecutorBase>(this, InExecutorType));
	return ActiveExecutor;
}

void UMoviePipelineQueueEngineSubsystem::RenderQueueWithExecutorInstance(UMoviePipelineExecutorBase* InExecutor)
{
	if(!ensureMsgf(!IsRendering(), TEXT("RenderQueueWithExecutor cannot be called while already rendering!")))
	{
		FFrame::KismetExecutionMessage(TEXT("Render already in progress."), ELogVerbosity::Error);
		return;
	}

	if (!InExecutor)
	{
		FFrame::KismetExecutionMessage(TEXT("Invalid executor supplied."), ELogVerbosity::Error);
		return;
	}

	ActiveExecutor = InExecutor;
	ActiveExecutor->OnExecutorFinished().AddUObject(this, &UMoviePipelineQueueEngineSubsystem::OnExecutorFinished);
	ActiveExecutor->Execute(GetQueue());
}

void UMoviePipelineQueueEngineSubsystem::OnExecutorFinished(UMoviePipelineExecutorBase* InPipelineExecutor, bool bSuccess)
{
	ActiveExecutor = nullptr;
}

