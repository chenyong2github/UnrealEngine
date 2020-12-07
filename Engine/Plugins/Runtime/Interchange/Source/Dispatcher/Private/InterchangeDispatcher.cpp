// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeDispatcher.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "InterchangeDispatcherConfig.h"
#include "InterchangeDispatcherLog.h"
#include "InterchangeDispatcherTask.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace UE
{
	namespace Interchange
	{

		FInterchangeDispatcher::FInterchangeDispatcher(const FString& InResultFolder)
			: NextTaskIndex(0)
			, CompletedTaskCount(0)
			, ResultFolder(InResultFolder)
		{
		}

		int32 FInterchangeDispatcher::AddTask(const FString& InJsonDescription)
		{
			FScopeLock Lock(&TaskPoolCriticalSection);
			int32 TaskIndex = TaskPool.Emplace(InJsonDescription);
			TaskPool[TaskIndex].Index = TaskIndex;
			return TaskIndex;
		}

		TOptional<FTask> FInterchangeDispatcher::GetNextTask()
		{
			FScopeLock Lock(&TaskPoolCriticalSection);

			while (TaskPool.IsValidIndex(NextTaskIndex) && TaskPool[NextTaskIndex].State != ETaskState::UnTreated)
			{
				NextTaskIndex++;
			}

			if (!TaskPool.IsValidIndex(NextTaskIndex))
			{
				return TOptional<FTask>();
			}

			TaskPool[NextTaskIndex].State = ETaskState::Running;
			return TaskPool[NextTaskIndex++];
		}

		void FInterchangeDispatcher::SetTaskState(int32 TaskIndex, ETaskState TaskState, const FString& JsonResult, const TArray<FString>& JSonMessages)
		{
			FString JsonDescription;
			{
				FScopeLock Lock(&TaskPoolCriticalSection);

				if (!ensure(TaskPool.IsValidIndex(TaskIndex)))
				{
					return;
				}

				FTask& Task = TaskPool[TaskIndex];
				Task.State = TaskState;
				Task.JsonResult = JsonResult;
				Task.JsonMessages = JSonMessages;
				JsonDescription = Task.JsonDescription;

				if (TaskState == ETaskState::ProcessOk
					|| TaskState == ETaskState::ProcessFailed)
				{
					CompletedTaskCount++;
				}

				if (TaskState == ETaskState::UnTreated)
				{
					NextTaskIndex = TaskIndex;
				}
			}

			UE_CLOG(TaskState == ETaskState::ProcessOk, LogInterchangeDispatcher, Verbose, TEXT("Json processed: %s"), *JsonDescription);
			UE_CLOG(TaskState == ETaskState::UnTreated, LogInterchangeDispatcher, Warning, TEXT("Json resubmitted: %s"), *JsonDescription);
			UE_CLOG(TaskState == ETaskState::ProcessFailed, LogInterchangeDispatcher, Error, TEXT("Json processing failure: %s"), *JsonDescription);
		}

		void FInterchangeDispatcher::GetTaskState(int32 TaskIndex, ETaskState& TaskState, FString& JsonResult, TArray<FString>& JSonMessages)
		{
			FScopeLock Lock(&TaskPoolCriticalSection);

			if (!ensure(TaskPool.IsValidIndex(TaskIndex)))
			{
				return;
			}

			FTask& Task = TaskPool[TaskIndex];
			TaskState = Task.State;
			JsonResult = Task.JsonResult;
			JSonMessages = Task.JsonMessages;
		}

		void FInterchangeDispatcher::StartProcess()
		{
			//Start the process
			SpawnHandler();
		}

		void FInterchangeDispatcher::StopProcess(bool bBlockUntilTerminated)
		{
			if (IsHandlerAlive())
			{
				if (bBlockUntilTerminated)
				{
					WorkerHandler->StopBlocking();
				}
				else
				{
					WorkerHandler->Stop();
				}
			}
		}

		void FInterchangeDispatcher::TerminateProcess()
		{
			//Empty the cache folder
			if (IFileManager::Get().DirectoryExists(*ResultFolder))
			{
				const bool RequireExists = false;
				//Delete recursively folder's content
				const bool Tree = true;
				IFileManager::Get().DeleteDirectory(*ResultFolder, RequireExists, Tree);
			}
			//Terminate the process
			CloseHandler();
		}

		void FInterchangeDispatcher::WaitAllTaskToCompleteExecution()
		{
			if (WorkerHandler.IsValid())
			{
				UE_LOG(LogInterchangeDispatcher, Error, TEXT("Cannot execute tasks before starting the process"));
			}

			bool bLogRestartError = true;
			while (!IsOver())
			{

				if (!IsHandlerAlive())
				{
					break;
				}

				FPlatformProcess::Sleep(0.1f);
			}

			if (!IsOver())
			{
				UE_LOG(LogInterchangeDispatcher, Warning,
					   TEXT("Begin local processing. (Multi Process failed to consume all the tasks)\n")
					   TEXT("See workers logs: %sPrograms/InterchangeWorker/Saved/Logs"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
			}
			else
			{
				UE_LOG(LogInterchangeDispatcher, Display, TEXT("Multi Process ended and consumed all the tasks"));
			}
		}

		bool FInterchangeDispatcher::IsOver()
		{
			FScopeLock Lock(&TaskPoolCriticalSection);
			return CompletedTaskCount == TaskPool.Num();
		}

		void FInterchangeDispatcher::SpawnHandler()
		{
			WorkerHandler = MakeUnique<FInterchangeWorkerHandler>(*this, ResultFolder);
		}

		bool FInterchangeDispatcher::IsHandlerAlive()
		{
			return WorkerHandler.IsValid() && WorkerHandler->IsAlive();
		}

		void FInterchangeDispatcher::CloseHandler()
		{
			StopProcess(false);
			WorkerHandler.Reset();
		}

	} //ns Interchange
}//ns UE