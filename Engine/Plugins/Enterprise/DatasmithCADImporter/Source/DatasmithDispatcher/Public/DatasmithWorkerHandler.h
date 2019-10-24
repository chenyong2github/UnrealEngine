// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "DatasmithCommands.h"
#include "DatasmithDispatcherSocket.h"
#include "Misc/Timespan.h"


#if PLATFORM_MAC
#include "Mac/MacPlatformProcess.h"
#elif PLATFORM_LINUX
#include "Unix/UnixPlatformProcess.h"
#else
#include "Windows/WindowsPlatformProcess.h"
#endif



class FSocket;

namespace DatasmithDispatcher
{
class FDatasmithBackPingCommand;
class FDatasmithDispatcher;
class FDatasmithNotifyEndTaskCommand;
class FDatasmithPingCommand;

//Handle a Worker by socket communication
class FDatasmithWorkerHandler
{

public:
	FDatasmithWorkerHandler(FDatasmithDispatcher& InDispatcher, const FString& InCachePath);

	bool Run();
	void NotifyKill();

private:
	const FString& GetCachePath() const
	{
		return CachePath;
	}

	FString GetExePath();

	bool InitializeSocket();
	bool StartWorker();
	void Kill();

	void RestartProcessor();

	bool NeedToRun();

	void PingProcess(FDatasmithPingCommand* PingCommand);
	void BackPingProcess(FDatasmithBackPingCommand* PingCommand);
	void EndTaskProcess(FDatasmithNotifyEndTaskCommand* RunTaskCommand);


private:
	FDatasmithDispatcher& Dispatcher;
	uint32 WorkerProcessId;

	FDatasmithDispatcherSocket ServerSocket;  
	/**
		*  Template socket used to accept connections
		*/
	FDatasmithDispatcherSocket ClientListener; 

	FString CachePath;

	TOptional<FTask> CurrentTask;

	FProcHandle WorkerHandle;

	bool bShouldTerminate;
	//FTimespan Stopwatch;
	//FTimespan StartPingTime;
	//FTimespan StartTime;

};
} // namespace DatasmithDispatcher
