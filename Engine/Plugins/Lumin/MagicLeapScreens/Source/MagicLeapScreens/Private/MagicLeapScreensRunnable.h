// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapRunnable.h"
#include "MagicLeapScreensTypes.h"
#include "Containers/Array.h"

struct FScreensTask : public FMagicLeapTask
{
	enum class ETaskType : uint32
	{
		None,
		GetHistory,
		RemoveFromHistory,
		AddToHistory,
		UpdateHistoryEntry,
		UpdateInfoEntry,
	};

	enum class ETaskRequestType : uint32
	{
		Request,
		Response
	};

	ETaskType TaskType;
	ETaskRequestType TaskRequestType;
	TArray<FMagicLeapScreensWatchHistoryEntry> WatchHistory;
	FMagicLeapScreenTransform ScreenTransform;

	FMagicLeapScreensEntryRequestResultDelegate EntryRequestDelegate;
	FMagicLeapScreensHistoryRequestResultDelegate HistoryRequestDelegate;
	FMagicLeapScreenTransformRequestResultDelegate TransformRequestDelegate;

	FScreensTask()
		: TaskType(ETaskType::None)
		, TaskRequestType(ETaskRequestType::Request)
		, WatchHistory(TArray<FMagicLeapScreensWatchHistoryEntry>())
	{
	}
};

class FScreensRunnable : public FMagicLeapRunnable<FScreensTask>
{
public:
	FScreensRunnable();
	bool ProcessCurrentTask() override;
	bool PeekCompletedTasks(FScreensTask& OutTask) const;
	bool CompletedTaskQueueIsEmpty() const;
private:
	bool AddToHistory();
	bool UpdateWatchHistoryEntry();
	bool UpdateInfoEntry();
	bool GetWatchHistory();
};

