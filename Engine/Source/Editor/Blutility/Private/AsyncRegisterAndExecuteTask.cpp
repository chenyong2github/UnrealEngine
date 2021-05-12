// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncRegisterAndExecuteTask.h"
#include "EditorUtilitySubsystem.h"
#include "Editor.h"

//----------------------------------------------------------------------//
// UAsyncRegisterAndExecuteTask
//----------------------------------------------------------------------//

UAsyncRegisterAndExecuteTask::UAsyncRegisterAndExecuteTask()
{
}

UAsyncRegisterAndExecuteTask* UAsyncRegisterAndExecuteTask::RegisterAndExecuteTask(UEditorUtilityTask* Task, UEditorUtilityTask* OptionalParentTask)
{
	UAsyncRegisterAndExecuteTask* AsyncTask = NewObject<UAsyncRegisterAndExecuteTask>();
	AsyncTask->Start(Task, OptionalParentTask);

	return AsyncTask;
}

void UAsyncRegisterAndExecuteTask::Start(UEditorUtilityTask* Task, UEditorUtilityTask* OptionalParentTask)
{
	Task->OnFinished.AddUObject(this, &UAsyncRegisterAndExecuteTask::HandleFinished);

	UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	EditorUtilitySubsystem->RegisterAndExecuteTask(Task, OptionalParentTask);
}

void UAsyncRegisterAndExecuteTask::HandleFinished(UEditorUtilityTask* Task)
{
	OnFinished.Broadcast(Task);
}
