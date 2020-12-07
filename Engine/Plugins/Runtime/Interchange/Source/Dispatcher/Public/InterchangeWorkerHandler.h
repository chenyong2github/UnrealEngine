// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InterchangeCommands.h"
#include "InterchangeDispatcherNetworking.h"
#include "InterchangeDispatcherTask.h"

#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"

namespace UE
{
	namespace Interchange
	{
		class FInterchangeDispatcher;

		//Handle a Worker by socket communication
		class INTERCHANGEDISPATCHER_API FInterchangeWorkerHandler
		{
			enum class EWorkerState
			{
				Uninitialized,
				Idle, // Initialized, available for processing
				Processing, // Currently processing a task
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
			FInterchangeWorkerHandler(FInterchangeDispatcher& InDispatcher, FString& InResultFolder);
			~FInterchangeWorkerHandler();

			void Run();
			bool IsAlive() const;
			void Stop();
			void StopBlocking();

		private:
			void RunInternal();
			void StartWorkerProcess();
			void ValidateConnection();

			void ProcessCommand(ICommand& Command);
			void ProcessCommand(FPingCommand& PingCommand);
			void ProcessCommand(FCompletedTaskCommand& RunTaskCommand);
			const TCHAR* EWorkerErrorStateAsString(EWorkerErrorState e);

		private:
			FInterchangeDispatcher& Dispatcher;

			// Send and receive commands
			FNetworkServerNode NetworkInterface;
			FCommandQueue CommandIO;
			FThread IOThread;
			FString ThreadName;

			// External process
			FProcHandle WorkerHandle;
			TAtomic<EWorkerState> WorkerState;
			EWorkerErrorState ErrorState;

			// self
			FString ResultFolder;
			TOptional<FTask> CurrentTask;
			bool bShouldTerminate;

		};
	} //ns Interchange
}//ns UE
