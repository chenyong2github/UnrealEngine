// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithDispatcher.h"

#include "DatasmithDispatcherConfig.h"
#include "DatasmithDispatcherLog.h"
#include "DatasmithDispatcherTask.h"

#include "Algo/Count.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarStaticCADTranslatorEnableCADCache(
	TEXT("r.CADTranslator.EnableCADCache"),
	1,
	TEXT("Activate to save temporary CAD processing file. These file will be use in a next import to avoid CAD file processing\n"),
	ECVF_Default);

namespace DatasmithDispatcher
{

FDatasmithDispatcher::FDatasmithDispatcher(const CADLibrary::FImportParameters& InImportParameters, const FString& InCacheDir, int32 InNumberOfWorkers, TMap<uint32, FString>& OutCADFileToUnrealFileMap, TMap<uint32, FString>& OutCADFileToUnrealGeomMap)
	: NextTaskIndex(0)
	, CompletedTaskCount(0)
	, CADFileToUnrealFileMap(OutCADFileToUnrealFileMap)
	, CADFileToUnrealGeomMap(OutCADFileToUnrealGeomMap)
	, ProcessCacheFolder(InCacheDir)
	, ImportParameters(InImportParameters)
	, NumberOfWorkers(InNumberOfWorkers)
	, NextWorkerId(0)
{
	// init cache folders
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("scene")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("cad")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("mesh")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("body")), true);

	ImportParameters.bEnableCacheUsage = (CVarStaticCADTranslatorEnableCADCache.GetValueOnAnyThread() != 0);
}

void FDatasmithDispatcher::AddTask(const CADLibrary::FFileDescription & InFileDescription)
{
	FScopeLock Lock(&TaskPoolCriticalSection);
	for (const FTask& Task : TaskPool)
	{
		if (Task.FileDescription == InFileDescription)
		{
			return;
		}
	}

	int32 TaskIndex = TaskPool.Emplace(InFileDescription);
	TaskPool[TaskIndex].Index = TaskIndex;
}

void FDatasmithDispatcher::LogWarningMessages(const TArray<FString>& WarningMessages) const
{
	for (const FString& WarningMessage : WarningMessages)
	{
		UE_LOG(LogDatasmithDispatcher, Warning, TEXT("%s"), *WarningMessage);
	}
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
	CADLibrary::FFileDescription FileDescription;
	{
		FScopeLock Lock(&TaskPoolCriticalSection);

		if (!ensure(TaskPool.IsValidIndex(TaskIndex)))
		{
			return;
		}

		FTask& Task = TaskPool[TaskIndex];
		Task.State = TaskState;
		FileDescription = Task.FileDescription;

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

	UE_CLOG(TaskState == ETaskState::ProcessOk, LogDatasmithDispatcher, Verbose, TEXT("File processed: %s"), *FileDescription.Name);
	UE_CLOG(TaskState == ETaskState::UnTreated, LogDatasmithDispatcher, Warning, TEXT("File resubmitted: %s"), *FileDescription.Name);
	UE_CLOG(TaskState == ETaskState::ProcessFailed, LogDatasmithDispatcher, Error, TEXT("File processing failure: %s"), *FileDescription.Name);
	UE_CLOG(TaskState == ETaskState::FileNotFound, LogDatasmithDispatcher, Warning, TEXT("file not found: %s"), *FileDescription.Name);
}

void FDatasmithDispatcher::Process(bool bWithProcessor)
{
	if (bWithProcessor)
	{
		SpawnHandlers();

		bool bLogRestartError = true;
		while (!IsOver())
		{
			bool bHasAliveWorker = false;
			for (FDatasmithWorkerHandler& Handler : WorkerHandlers)
			{
				// replace dead workers
				if (Handler.IsRestartable())
				{
					int32 WorkerId = GetNextWorkerId();
					if (WorkerId < NumberOfWorkers + Config::MaxRestartAllowed)
					{
						Handler.~FDatasmithWorkerHandler();
						new (&Handler) FDatasmithWorkerHandler(*this, ImportParameters, ProcessCacheFolder, WorkerId);
						UE_LOG(LogDatasmithDispatcher, Warning, TEXT("Restarting worker (new worker: %d)"), WorkerId);
					}
					else if (bLogRestartError)
					{
						bLogRestartError = false;
						UE_LOG(LogDatasmithDispatcher, Warning, TEXT("Worker not restarted (Limit reached)"));
					}
				}

				bHasAliveWorker = bHasAliveWorker || Handler.IsAlive();
			}

			if (!bHasAliveWorker)
			{
				break;
			}

			FWindowsPlatformProcess::Sleep(0.1);
		}

		CloseHandlers();
	}

	if (!IsOver())
	{
		UE_LOG(LogDatasmithDispatcher, Warning,
			TEXT("Begin local processing. (Multi Process failed to consume all the tasks)\n")
			TEXT("See workers logs: %sPrograms/DatasmithCADWorker/Saved/Logs"), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
		ProcessLocal();
	}
	else
	{
		UE_LOG(LogDatasmithDispatcher, Display, TEXT("Multi Process ended and consumed all the tasks"));
	}
}

bool FDatasmithDispatcher::IsOver()
{
	FScopeLock Lock(&TaskPoolCriticalSection);
	return CompletedTaskCount == TaskPool.Num();
}

void FDatasmithDispatcher::LinkCTFileToUnrealCacheFile(const CADLibrary::FFileDescription& CTFileDescription, const FString& UnrealSceneGraphFile, const FString& UnrealMeshFile)
{
	FScopeLock Lock(&TaskPoolCriticalSection);

	uint32 FileHash = GetTypeHash(CTFileDescription);
	if (!UnrealSceneGraphFile.IsEmpty())
	{
		CADFileToUnrealFileMap.Add(FileHash, UnrealSceneGraphFile);
	}
	if (!UnrealMeshFile.IsEmpty())
	{
		CADFileToUnrealGeomMap.Add(FileHash, UnrealMeshFile);
	}
}

int32 FDatasmithDispatcher::GetNextWorkerId()
{
	return NextWorkerId++;
}

void FDatasmithDispatcher::SpawnHandlers()
{
	WorkerHandlers.Reserve(NumberOfWorkers);
	for (int32 Index = 0; Index < NumberOfWorkers; Index++)
	{
		WorkerHandlers.Emplace(*this, ImportParameters, ProcessCacheFolder, GetNextWorkerId());
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
		CADLibrary::FFileDescription& FileDescription = Task->FileDescription;

		CADLibrary::FCoreTechFileParser FileParser(ImportParameters, *FPaths::EnginePluginsDir(), ProcessCacheFolder);
		ETaskState ProcessResult = FileParser.ProcessFile(FileDescription);

		ETaskState TaskState = ProcessResult;
		SetTaskState(Task->Index, TaskState);

		if (TaskState == ETaskState::ProcessOk)
		{
			TSet<CADLibrary::FFileDescription>& ExternalRefSet = FileParser.GetExternalRefSet();
			if (ExternalRefSet.Num() > 0)
			{
				for (CADLibrary::FFileDescription& ExternalFile : ExternalRefSet)
				{
					ExternalFile.MainCadFilePath = FileDescription.MainCadFilePath;
					AddTask(ExternalFile);
				}
			}
			LinkCTFileToUnrealCacheFile(FileParser.GetCADFileDescription(), FileParser.GetSceneGraphFile(), FileParser.GetMeshFileName());
		}
	}
#endif // CAD_INTERFACE
}

} // ns DatasmithDispatcher