// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapScreensRunnable.h"
#include "MagicLeapScreensPlugin.h"

FScreensRunnable::FScreensRunnable()
	: FMagicLeapRunnable({ }, TEXT("FScreensRunnable"))
{
}

bool FScreensRunnable::PeekCompletedTasks(FScreensTask& OutTask) const
{
	return CompletedTasks.Peek(OutTask);
}

bool FScreensRunnable::CompletedTaskQueueIsEmpty() const
{
	return CompletedTasks.IsEmpty();
}

bool FScreensRunnable::ProcessCurrentTask()
{
	switch (CurrentTask.TaskType)
	{
	case FScreensTask::ETaskType::None: return true;
	case FScreensTask::ETaskType::GetHistory: return GetWatchHistory();
	case FScreensTask::ETaskType::AddToHistory: return AddToHistory();
	case FScreensTask::ETaskType::UpdateHistoryEntry: return UpdateWatchHistoryEntry();
	case FScreensTask::ETaskType::UpdateInfoEntry: return UpdateInfoEntry();
	}
	return false;
}

bool FScreensRunnable::AddToHistory()
{
	check(CurrentTask.WatchHistory.Num() != 0);
	FScreensTask Task = GetMagicLeapScreensPlugin().AddToWatchHistory(CurrentTask.WatchHistory[0]);
	Task.EntryRequestDelegate = CurrentTask.EntryRequestDelegate;
	CurrentTask = Task;
	check(CurrentTask.WatchHistory.Num() > 0);
	return CurrentTask.bSuccess;
}

bool FScreensRunnable::UpdateWatchHistoryEntry()
{
	check(CurrentTask.WatchHistory.Num() != 0);
	FScreensTask Task = GetMagicLeapScreensPlugin().UpdateWatchHistoryEntry(CurrentTask.WatchHistory[0]);
	Task.EntryRequestDelegate = CurrentTask.EntryRequestDelegate;
	CurrentTask = Task;
	check(CurrentTask.WatchHistory.Num() > 0);
	return CurrentTask.bSuccess;
}

bool FScreensRunnable::UpdateInfoEntry()
{
	FScreensTask Task = GetMagicLeapScreensPlugin().UpdateScreensTransform(CurrentTask.ScreenTransform);
	Task.TransformRequestDelegate = CurrentTask.TransformRequestDelegate;
	CurrentTask = Task;
	return CurrentTask.bSuccess;
}

bool FScreensRunnable::GetWatchHistory()
{
	FScreensTask Task = GetMagicLeapScreensPlugin().GetWatchHistoryEntries();
	Task.HistoryRequestDelegate = CurrentTask.HistoryRequestDelegate;
	CurrentTask = Task;
	return CurrentTask.bSuccess;
}

