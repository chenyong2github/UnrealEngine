// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDispatcher.h"

#include "CoreTechFileParser.h"
#include "DatasmithDispatcherTask.h"
#include "DatasmithDispatcherLog.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Algo/Count.h"

DEFINE_LOG_CATEGORY(LogDatasmithDispatcher);

namespace DatasmithDispatcher
{

FDatasmithDispatcher::FDatasmithDispatcher(const CADLibrary::FImportParameters& InImportParameters, const FString& InCacheDir, int32 InNumberOfWorkers, TMap<FString, FString>& OutCADFileToUnrealFileMap, TMap<FString, FString>& OutCADFileToUnrealGeomMap)
	: NextTaskIndex(0)
	, CompletedTaskCount(0)
	, CADFileToUnrealFileMap(OutCADFileToUnrealFileMap)
	, CADFileToUnrealGeomMap(OutCADFileToUnrealGeomMap)
	, ProcessCacheFolder(InCacheDir)
	, ImportParameters(InImportParameters)
	, NumberOfWorkers(InNumberOfWorkers)
{
	// init cache folders
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("scene")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("cad")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("mesh")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("body")), true);
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

TOptional<FTask> FDatasmithDispatcher::GetNextTask()
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

void FDatasmithDispatcher::SetTaskState(int32 TaskIndex, ETaskState TaskState)
{
	FScopeLock Lock(&TaskPoolCriticalSection);

	if (!ensure(TaskPool.IsValidIndex(TaskIndex)))
	{
		return;
	}

	TaskPool[TaskIndex].State = TaskState;

	if (TaskState == ETaskState::ProcessOk
	 || TaskState == ETaskState::ProcessFailed
	 || TaskState == ETaskState::FileNotFound)
	{
		CompletedTaskCount++;
	}

	if (TaskState == ETaskState::UnTreated)
	{
		NextTaskIndex = TaskIndex;
	}
}

void FDatasmithDispatcher::Process(bool bWithProcessor)
{
	if (bWithProcessor)
	{
		SpawnHandlers();

		while (!IsOver() && GetAliveHandlerCount() > 0)
		{
			FWindowsPlatformProcess::Sleep(0.5);
		}

		CloseHandlers();
	}

	if (!IsOver())
	{
		ProcessLocal();
	}
}

bool FDatasmithDispatcher::IsOver()
{
	FScopeLock Lock(&TaskPoolCriticalSection);
	return CompletedTaskCount == TaskPool.Num();
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

void FDatasmithDispatcher::SpawnHandlers()
{
	WorkerHandlers.Reserve(NumberOfWorkers);
	for (int32 Index = 0; Index < NumberOfWorkers; Index++)
	{
		WorkerHandlers.Emplace(*this, ProcessCacheFolder, Index);
	}
}

int32 FDatasmithDispatcher::GetAliveHandlerCount()
{
	return Algo::CountIf(WorkerHandlers, [](const FDatasmithWorkerHandler& Handler) { return Handler.IsAlive(); });
}

void FDatasmithDispatcher::CloseHandlers()
{
	for (FDatasmithWorkerHandler& Handler : WorkerHandlers)
	{
		Handler.Stop();
	}
	WorkerHandlers.Empty();
}

void FDatasmithDispatcher::ProcessLocal()
{
#ifdef CAD_INTERFACE
	while (TOptional<FTask> Task = GetNextTask())
	{
		FString FullPath = Task->FileName;

		CADLibrary::FCoreTechFileParser FileParser(FullPath, ProcessCacheFolder, ImportParameters, true, true);
		ETaskState ProcessResult = FileParser.ProcessFile();

		ETaskState TaskState = ProcessResult;
		SetTaskState(Task->Index, TaskState);

		if (TaskState == ETaskState::ProcessOk)
		{
			FString CurrentPath = FPaths::GetPath(FullPath);
			const TSet<FString>& ExternalRefSet = FileParser.GetExternalRefSet();
			if (ExternalRefSet.Num() > 0)
			{
				for (const FString& ExternalFile : ExternalRefSet)
				{
					AddTask(FPaths::Combine(CurrentPath, ExternalFile));
				}
			}
			LinkCTFileToUnrealCacheFile(FileParser.GetFileName(), FileParser.GetSceneGraphFile(), FileParser.GetMeshFile());
		}
	}
#endif // CAD_INTERFACE
}

} // ns DatasmithDispatcher