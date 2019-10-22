// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithWorkerHandler.h"

#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Thread.h"

namespace DatasmithDispatcher
{


	//Wrapper class to launch processors
	class DATASMITHDISPATCHER_API FDatasmithDispatcher
	{

	public:
		//Constructor
		FDatasmithDispatcher(const TCHAR* InCacheDir, int32 InNumberOfWorkers, TMap<FString, FString>& CADFileToUnrealFileMap, TMap<FString, FString>& CADFileToUnrealGeomMap);
		~FDatasmithDispatcher();

		/**
		 * Spawns worker handlers
		 */
		void RunHandlers();
		void Close();

		void Process();
		void Clear();

		void AddTask(const FString& FileName); // Thread safe with FScopeLock Lock(&TaskPoolCriticalSection);

		TOptional<FTask> GetTask();   // Thread safe with FScopeLock Lock(&TaskPoolCriticalSection);
		bool IsOver(); // Thread safe with FScopeLock Lock(&TaskPoolCriticalSection);

		void SetTaskState(int32 TaskIndex, EProcessState TaskState);   // Thread safe
		void LinkCTFileToUnrealCacheFile(const FString& CTFile, const FString& UnrealSceneGraphFile, const FString& UnrealGeomFile);   // Thread safe


	private:
		FCriticalSection TaskPoolCriticalSection;

		FString ProcessCacheFolder;
		TMap<FString, FString>& CADFileToUnrealFileMap;
		TMap<FString, FString>& CADFileToUnrealGeomMap;

		//Started
		bool bStarted;

		int32 NumberOfWorkers;
		TArray<FDatasmithWorkerHandler> WorkerHandlerSet;
		TArray<FThread> WorkersRunning;

		TArray<FTask> TaskPool;
		int32 LastLaunchTask;
		int32 NumOfFinishedTask;


	}; // FDatasmithDispatcher

} // NS DatasmithDispatcher
