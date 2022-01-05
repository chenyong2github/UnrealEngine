// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueueSubsystem.h"
#include "LevelSequenceEditorModule.h"
#include "Modules/ModuleManager.h"

UMoviePipelineExecutorBase* UMoviePipelineQueueSubsystem::RenderQueueWithExecutor(TSubclassOf<UMoviePipelineExecutorBase> InExecutorType)
{
	if(!ensureMsgf(!IsRendering(), TEXT("RenderQueueWithExecutor cannot be called while already rendering!")))
	{
		return nullptr;
	}

	ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditor");
	if (LevelSequenceEditorModule)
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().AddUObject(this, &UMoviePipelineQueueSubsystem::OnSequencerContextBinding);
	}
		
	ActiveExecutor = NewObject<UMoviePipelineExecutorBase>(this, InExecutorType);
	ActiveExecutor->OnExecutorFinished().AddUObject(this, &UMoviePipelineQueueSubsystem::OnExecutorFinished);
	ActiveExecutor->Execute(GetQueue());
	return ActiveExecutor;
}

void UMoviePipelineQueueSubsystem::OnSequencerContextBinding(bool& bAllowBinding)
{
	if (ActiveExecutor && ActiveExecutor->IsRendering())
	{
		bAllowBinding = false;
	}
}

void UMoviePipelineQueueSubsystem::OnExecutorFinished(UMoviePipelineExecutorBase* InPipelineExecutor, bool bSuccess)
{
	ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditorModule");
	if (LevelSequenceEditorModule)
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().RemoveAll(this);
	}

	ActiveExecutor = nullptr;
}

