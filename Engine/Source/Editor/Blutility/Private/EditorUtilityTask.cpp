// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityTask.h"
#include "EditorUtilitySubsystem.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EditorUtilityCommon.h"

//////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "UEditorUtilityTask"

UEditorUtilityTask::UEditorUtilityTask()
{
}

void UEditorUtilityTask::Run()
{
	UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	EditorUtilitySubsystem->RegisterAndExecuteTask(this, nullptr);
}

UWorld* UEditorUtilityTask::GetWorld() const
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
		return nullptr;
	}

	return GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
}

void UEditorUtilityTask::StartExecutingTask()
{
	Cached_GIsRunningUnattendedScript = GIsRunningUnattendedScript;
	GIsRunningUnattendedScript = true;

	CreateNotification();

	BeginExecution();
	ReceiveBeginExecution();
}

void UEditorUtilityTask::FinishExecutingTask()
{
	SetTaskNotificationText(LOCTEXT("TaskComplete", "Complete"));

	if (ensure(MyTaskManager))
	{
		MyTaskManager->RemoveTaskFromActiveList(this);
	}

	if (TaskNotification.IsValid())
	{
		TaskNotification->SetComplete(true);
		TaskNotification.Reset();
	}

	GIsRunningUnattendedScript = Cached_GIsRunningUnattendedScript;

	// Notify anyone who needs to know that we're done.
	OnFinished.Broadcast(this);
}

void UEditorUtilityTask::CreateNotification()
{
	FAsyncTaskNotificationConfig NotificationConfig;
	NotificationConfig.TitleText = FText::Format(LOCTEXT("NotificationEditorUtilityTaskTitle", "Task {0}"), GetClass()->GetDisplayNameText());
	NotificationConfig.ProgressText = LOCTEXT("Running", "Running");
	NotificationConfig.bCanCancel = true;
	TaskNotification = MakeUnique<FAsyncTaskNotification>(NotificationConfig);
}

void UEditorUtilityTask::RequestCancel()
{
	if (!bCancelRequested)
	{
		bCancelRequested = true;

		SetTaskNotificationText(LOCTEXT("TaskCanceling", "Canceling"));

		CancelRequested();
		ReceiveCancelRequested();

		FinishExecutingTask();
	}
}

bool UEditorUtilityTask::WasCancelRequested() const
{
	if (TaskNotification.IsValid())
	{
		if (TaskNotification->GetPromptAction() == EAsyncTaskNotificationPromptAction::Cancel)
		{
			return true;
		}
	}

	return bCancelRequested;
}

void UEditorUtilityTask::SetTaskNotificationText(const FText& Text)
{
	UE_LOG(LogEditorUtilityBlueprint, Log, TEXT("%s: %s"), *GetPathNameSafe(this), *Text.ToString());

	if (TaskNotification.IsValid())
	{
		TaskNotification->SetProgressText(Text);
	}
}

#undef LOCTEXT_NAMESPACE