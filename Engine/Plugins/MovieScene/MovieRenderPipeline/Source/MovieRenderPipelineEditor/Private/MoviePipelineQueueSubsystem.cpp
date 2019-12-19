// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueueSubsystem.h"

UMoviePipelineExecutorBase* UMoviePipelineQueueSubsystem::RenderQueueWithExecutor(TSubclassOf<UMoviePipelineExecutorBase> InExecutorType)
{
	if(!ensureMsgf(!IsRendering(), TEXT("RenderQueueWithExecutor cannot be called while already rendering!")))
	{
		return nullptr;
	}
	
	ActiveExecutor = NewObject<UMoviePipelineExecutorBase>(this, InExecutorType);
	ActiveExecutor->OnExecutorFinished().AddUObject(this, &UMoviePipelineQueueSubsystem::OnExecutorFinished);
	ActiveExecutor->Execute(GetQueue());
	
	return ActiveExecutor;
}

void UMoviePipelineQueueSubsystem::OnExecutorFinished(UMoviePipelineExecutorBase* InPipelineExecutor, bool bSuccess)
{
	ActiveExecutor = nullptr;
}

