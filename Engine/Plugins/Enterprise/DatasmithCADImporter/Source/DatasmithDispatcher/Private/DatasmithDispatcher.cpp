// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithDispatcher.h"

#include "Misc/ScopeLock.h"
#include "HAL/Thread.h"
#include "Containers/Array.h"
#include "Misc/Optional.h"

namespace DatasmithDispatcher
{
	FDatasmithDispatcher::FDatasmithDispatcher(const TCHAR* InCacheDir, int32 InNumberOfWorkers, TMap<FString, FString>& OutCADFileToUnrealFileMap, TMap<FString, FString>& OutCADFileToUnrealGeomMap)
		: ProcessCacheFolder(InCacheDir)
		, CADFileToUnrealFileMap(OutCADFileToUnrealFileMap)
		, CADFileToUnrealGeomMap(OutCADFileToUnrealGeomMap)
		, NumberOfWorkers(InNumberOfWorkers)
		, LastLaunchTask(0)
		, NumOfFinishedTask(0)
 	{
		WorkerHandlerSet.Reserve(NumberOfWorkers);
		for (int32 Index = 0; Index < NumberOfWorkers; Index++)
		{
			WorkerHandlerSet.Emplace(*this, ProcessCacheFolder);
		}
	}

	FDatasmithDispatcher::~FDatasmithDispatcher()
	{
	}


	void FDatasmithDispatcher::RunHandlers()
	{
		WorkersRunning.Reserve(NumberOfWorkers);
		int32 WorkerIndex = 0;
		for (FDatasmithWorkerHandler& Handler : WorkerHandlerSet)
		{
			FString ProcessName = TEXT("DatasmithWorker ") + FString::FromInt(WorkerIndex);
			WorkerIndex++;
			WorkersRunning.Emplace(*ProcessName, [&]() { Handler.Run(); });
		}
	}

	void FDatasmithDispatcher::Close()
	{
		for (FDatasmithWorkerHandler& Handler : WorkerHandlerSet)
		{
			Handler.NotifyKill();
		}

		for (FThread& Process : WorkersRunning)
		{
			Process.Join();
		}
	}

	void FDatasmithDispatcher::Clear()
	{
		TaskPool.Empty();
		LastLaunchTask = 0;
		NumOfFinishedTask = 0;
	}


	void FDatasmithDispatcher::Process()
	{
		RunHandlers();

		while (!IsOver())
		{
			FWindowsPlatformProcess::Sleep(0.1);
		} 

		Close();
	}

	void FDatasmithDispatcher::AddTask(const FString& InFile)
	{
		FScopeLock Lock(&TaskPoolCriticalSection);
		for (const FTask& Task : TaskPool)
		{
			if (Task.FileName == InFile)
			{
				return;
			}
		}

		int32 TaskIndex = TaskPool.Emplace(InFile);
		TaskPool[TaskIndex].Index = TaskIndex;
	}

	TOptional<FTask> FDatasmithDispatcher::GetTask()
	{
		FScopeLock Lock(&TaskPoolCriticalSection);

		if (!TaskPool.IsValidIndex(LastLaunchTask))
		{
			return TOptional<FTask>();
		}

		TaskPool[LastLaunchTask].State = Running;
		return TaskPool[LastLaunchTask++];
	}

	bool FDatasmithDispatcher::IsOver() 
	{
		FScopeLock Lock(&TaskPoolCriticalSection);
		return NumOfFinishedTask == TaskPool.Num();
	}

	void FDatasmithDispatcher::SetTaskState(int32 TaskIndex, EProcessState TaskState) 
	{
		FScopeLock Lock(&TaskPoolCriticalSection);

		if (TaskIndex < 0)
		{
			return;
		}
		if (TaskIndex < TaskPool.Num())
		{
			TaskPool[TaskIndex].State = TaskState;
			if (TaskState & (ProcessOk | ProcessFailed | FileNotFound))
			{
				NumOfFinishedTask++;
			}
		}
	}

	void FDatasmithDispatcher::LinkCTFileToUnrealCacheFile(const FString& CTFile, const FString& UnrealSceneGraphFile, const FString& UnrealMeshFile)
	{
		FScopeLock Lock(&TaskPoolCriticalSection);
		if (!UnrealSceneGraphFile.IsEmpty())
		{
			CADFileToUnrealFileMap.Add(CTFile, UnrealSceneGraphFile);
		}
		if (!UnrealMeshFile.IsEmpty())
		{
			CADFileToUnrealGeomMap.Add(CTFile, UnrealMeshFile);
		}

	}
}
