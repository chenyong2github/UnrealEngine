// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskEntry.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskEntry::FTaskEntry(const TraceServices::FTaskInfo& TaskInfo)
	: Id(TaskInfo.Id)
	, DebugName(TaskInfo.DebugName)
	, bTracked(TaskInfo.bTracked)
	, ThreadToExecuteOn(TaskInfo.ThreadToExecuteOn)
	, CreatedTimestamp(TaskInfo.CreatedTimestamp)
	, CreatedThreadId(TaskInfo.CreatedThreadId)
	, LaunchedTimestamp(TaskInfo.LaunchedTimestamp)
	, LaunchedThreadId(TaskInfo.LaunchedThreadId)
	, ScheduledTimestamp(TaskInfo.ScheduledTimestamp)
	, ScheduledThreadId(TaskInfo.ScheduledThreadId)
	, StartedTimestamp(TaskInfo.StartedTimestamp)
	, StartedThreadId(TaskInfo.StartedThreadId)
	, FinishedTimestamp(TaskInfo.FinishedTimestamp)
	, CompletedTimestamp(TaskInfo.CompletedTimestamp)
	, CompletedThreadId(TaskInfo.CompletedThreadId)
{
	NumNested = TaskInfo.NestedTasks.Num();
	NumSubsequents = TaskInfo.Subsequents.Num();
	NumPrerequisites = TaskInfo.Prerequisites.Num();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
