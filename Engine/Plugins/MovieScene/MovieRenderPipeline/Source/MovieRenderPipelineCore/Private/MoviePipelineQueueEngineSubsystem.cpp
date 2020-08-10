// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueueEngineSubsystem.h"
#include "Modules/ModuleManager.h"
#include "MoviePipeline.h"

UMoviePipelineExecutorBase* UMoviePipelineQueueEngineSubsystem::RenderQueueWithExecutor(TSubclassOf<UMoviePipelineExecutorBase> InExecutorType)
{
	if(!ensureMsgf(!IsRendering(), TEXT("RenderQueueWithExecutor cannot be called while already rendering!")))
	{
		return nullptr;
	}
		
	ActiveExecutor = NewObject<UMoviePipelineExecutorBase>(this, InExecutorType);
	ActiveExecutor->SetMoviePipelineClass(UMoviePipeline::StaticClass());
	ActiveExecutor->OnExecutorFinished().AddUObject(this, &UMoviePipelineQueueEngineSubsystem::OnExecutorFinished);
	ActiveExecutor->Execute(GetQueue());
	return ActiveExecutor;
}

void UMoviePipelineQueueEngineSubsystem::OnExecutorFinished(UMoviePipelineExecutorBase* InPipelineExecutor, bool bSuccess)
{
	ActiveExecutor = nullptr;
}

