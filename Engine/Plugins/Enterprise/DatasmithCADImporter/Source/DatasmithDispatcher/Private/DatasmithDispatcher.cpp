// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithDispatcher.h"

#include "CoreTechFileParser.h"

#include "Containers/Array.h"
#include "HAL/FileManager.h"
#include "HAL/Thread.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"


namespace DatasmithDispatcher
{

FDatasmithDispatcher::FDatasmithDispatcher(const FString& InCacheDir, int32 InNumberOfWorkers, TMap<FString, FString>& OutCADFileToUnrealFileMap, TMap<FString, FString>& OutCADFileToUnrealGeomMap)
	: CADFileToUnrealFileMap(OutCADFileToUnrealFileMap)
	, CADFileToUnrealGeomMap(OutCADFileToUnrealGeomMap)
	, ProcessCacheFolder(InCacheDir)
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

void FDatasmithDispatcher::Init(const CADLibrary::FImportParameters& InImportParameters)
{
	ImportParameters = InImportParameters;

	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("scene")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("cad")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("mesh")), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(ProcessCacheFolder, TEXT("body")), true);
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


void FDatasmithDispatcher::Process(bool bWithProcessor)
{
	if (bWithProcessor)
	{
		RunHandlers();  

		while (!IsOver())
		{
			FWindowsPlatformProcess::Sleep(0.1);
		} 

		Close();
	}
	else
	{
		ProcessLocal();
	}
}

void FDatasmithDispatcher::ProcessLocal()
{
#ifdef CAD_INTERFACE
	TOptional<FTask> Task = GetTask();
	while (Task.IsSet())
	{
		FString FullPath = Task->FileName;

		CADLibrary::FCoreTechFileParser FileParser(FullPath, ProcessCacheFolder, ImportParameters, true, true);
		EProcessState TaskState = FileParser.ProcessFile();
		SetTaskState(Task->Index, TaskState);

		if (TaskState == ProcessOk)
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

		Task = GetTask();
	}
#endif // CAD_INTERFACE
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

void FDatasmithDispatcher::SetTessellationOptions(float InChordTolerance, float InMaxEdgeLength, float InNormalTolerance, CADLibrary::EStitchingTechnique InStitchingTechnique)
{
	ImportParameters.ChordTolerance = InChordTolerance;
	ImportParameters.MaxEdgeLength = InMaxEdgeLength;
	ImportParameters.MaxNormalAngle = InNormalTolerance;
	ImportParameters.StitchingTechnique = InStitchingTechnique;
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