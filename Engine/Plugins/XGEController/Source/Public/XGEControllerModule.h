// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DistributedBuildInterface/Public/DistributedBuildControllerInterface.h"
#include "Windows/WindowsPlatformNamedPipe.h"
#include "Containers/Queue.h"

class FXGEControllerModule : public IDistributedBuildController
{
	bool bSupported;
	bool bModuleInitialized;
	bool bControllerInitialized;

	FThreadSafeCounter NextFileID;
	FThreadSafeCounter NextTaskID;

	FProcHandle BuildProcessHandle;

	const FString ControlWorkerDirectory;
	const FString RootWorkingDirectory;
	const FString WorkingDirectory;
	const FString PipeName;
	FString XGConsolePath;

	// Taken when accessing the PendingTasks and DispatchedTasks members.
	FCriticalSection* TasksCS;

	// Queue of tasks submitted by the engine, but not yet dispatched to the controller.
	TQueue<FTask*> PendingTasks;

	// Map of tasks dispatched to the controller and running within XGE, that have not yet finished.
	TMap<uint32, FTask*> DispatchedTasks;

	bool bShutdown;
	bool bRestartWorker;
	TFuture<void> WriteOutThreadFuture;
	TFuture<void> ReadBackThreadFuture;

	FEvent* WriteOutThreadEvent;

	// We need two pipes, as the named pipe API does not support simultaneous read/write on two threads.
	FPlatformNamedPipe InputNamedPipe, OutputNamedPipe;

	volatile uint32 LastEventTime;

	inline bool AreTasksPending()
	{
		FScopeLock Lock(TasksCS);
		return !PendingTasks.IsEmpty();
	}

	inline bool AreTasksDispatchedOrPending()
	{
		FScopeLock Lock(TasksCS);
		return DispatchedTasks.Num() > 0 || !PendingTasks.IsEmpty();
	}

public:
	FXGEControllerModule();
	virtual ~FXGEControllerModule();

	virtual void StartupModule() override final;
	virtual void ShutdownModule() override final;

	virtual void InitializeController() override final;

	XGECONTROLLER_API virtual const FString GetName() override final { return FString("XGE Controller"); };

	XGECONTROLLER_API virtual bool IsSupported() override final;

	virtual FString CreateUniqueFilePath() override final;
	virtual TFuture<FDistributedBuildTaskResult> EnqueueTask(const FTaskCommandData& CommandData) override final;
	
	XGECONTROLLER_API static FXGEControllerModule& Get();

	void WriteOutThreadProc();
	void ReadBackThreadProc();

	void CleanWorkingDirectory();
};