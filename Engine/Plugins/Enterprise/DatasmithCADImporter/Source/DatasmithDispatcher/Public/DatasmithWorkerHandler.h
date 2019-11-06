// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithCommands.h"
#include "DatasmithDispatcherNetworking.h"
#include "DatasmithDispatcherTask.h"

#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"


namespace DatasmithDispatcher
{

class FDatasmithDispatcher;

//Handle a Worker by socket communication
class FDatasmithWorkerHandler
{
	enum class EWorkerState
	{
		Uninitialized,
		Idle, // Initialized, available for processing
		Processing, // Currently processing a task
		Restarting, // can occur when a processing is aborted.
		Closing, // in the process of terminating
		Terminated, // aka. Not Alive
	};

	enum class EWorkerErrorState
	{
		Ok,
		ConnectionFailed_NotBound,
		ConnectionFailed_NoClient,
		ConnectionLost,
		ConnectionLost_SendFailed,
		WorkerProcess_CantCreate,
		WorkerProcess_Lost,
	};

public:
	FDatasmithWorkerHandler(FDatasmithDispatcher& InDispatcher, const FString& InCachePath, uint32 Id);
	~FDatasmithWorkerHandler();

	void Run();
	bool IsAlive() const;
	void Stop();

private:
	void RunInternal();
	void StartWorkerProcess();
	void ValidateConnection();

// 	void RestartProcessor();

	void ProcessCommand(ICommand& Command);
	void ProcessCommand(FPingCommand& PingCommand);
	void ProcessCommand(FCompletedTaskCommand& RunTaskCommand);

private:
	FDatasmithDispatcher& Dispatcher;

	// Send and receive commands
	FNetworkServerNode NetworkInterface;
	FCommandQueue CommandIO;
	FThread IOThread;
	FString ThreadName;
	// FEvent* todo;

	// External process
	FProcHandle WorkerHandle;
	TAtomic<EWorkerState> WorkerState;
	EWorkerErrorState ErrorState;

	// self
	FString CachePath;
	TOptional<FTask> CurrentTask;
	bool bShouldTerminate;

};
} // namespace DatasmithDispatcher
