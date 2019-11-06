// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithWorkerHandler.h"

#include "DatasmithDispatcher.h"
#include "DatasmithDispatcherLog.h"
#include "DatasmithCommands.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace DatasmithDispatcher
{

static FString GetWorkerExecutablePath()
{
	FString ProcessorPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Enterprise/DatasmithCADImporter/Binaries"));

#if PLATFORM_MAC
	ProcessorPath = FPaths::Combine(ProcessorPath, TEXT("Mac/DatasmithCADWorker"));
#elif PLATFORM_LINUX
	ProcessorPath = FPaths::Combine(ProcessorPath, TEXT("Linux/DatasmithCADWorker"));
#elif PLATFORM_WINDOWS
	ProcessorPath = FPaths::Combine(ProcessorPath, TEXT("Win64/DatasmithCADWorker.exe"));
#endif

	return ProcessorPath;
}

FDatasmithWorkerHandler::FDatasmithWorkerHandler(FDatasmithDispatcher& InDispatcher, const FString& InCachePath, uint32 Id)
	: Dispatcher(InDispatcher)
	, WorkerState(EWorkerState::Uninitialized)
	, ErrorState(EWorkerErrorState::Ok)
	, CachePath(InCachePath)
	, bShouldTerminate(false)
{
	CommandIO.SetNetworkInterface(&NetworkInterface);
	ThreadName = FString(TEXT("DatasmithWorkerHandler_")) + FString::FromInt(Id);
	IOThread = FThread(*ThreadName, [this]() { Run(); } );
}

FDatasmithWorkerHandler::~FDatasmithWorkerHandler()
{
	IOThread.Join();
}

void FDatasmithWorkerHandler::StartWorkerProcess()
{
	ensure(ErrorState == EWorkerErrorState::Ok);
	static const FString ProcessorPath = GetWorkerExecutablePath();
	if (FPaths::FileExists(ProcessorPath))
	{
		int32 ListenPort = NetworkInterface.GetListeningPort();
		if (ListenPort == 0)
		{
			ErrorState = EWorkerErrorState::ConnectionFailed_NotBound;
			return;
		}

		FString CommandToProcess;
		CommandToProcess += TEXT(" -ServerPID ") + FString::FromInt(FPlatformProcess::GetCurrentProcessId());
		CommandToProcess += TEXT(" -ServerPort ") + FString::FromInt(ListenPort);
		CommandToProcess += TEXT(" -CacheDir \"") + CachePath + TEXT('"');
		UE_LOG(LogDatasmithDispatcher, Display, TEXT("CommandToProcess: %s"), *CommandToProcess);

		uint32 WorkerProcessId = 0;
		WorkerHandle = FPlatformProcess::CreateProc(*ProcessorPath, *CommandToProcess, true, false, false, &WorkerProcessId, 0, nullptr, nullptr);
	}

	if (!WorkerHandle.IsValid())
	{
		ErrorState = EWorkerErrorState::WorkerProcess_CantCreate;
		return;
	}
}

void FDatasmithWorkerHandler::ValidateConnection()
{
	if (!NetworkInterface.IsValid())
	{
		UE_LOG(LogDatasmithDispatcher, Error, TEXT("NetworkInterface lost"));
		WorkerState = EWorkerState::Closing;
		ErrorState = EWorkerErrorState::ConnectionLost;
	}
	else if (WorkerHandle.IsValid() && !FPlatformProcess::IsProcRunning(WorkerHandle))
	{
		UE_LOG(LogDatasmithDispatcher, Error, TEXT("Worker lost"));
		WorkerState = EWorkerState::Closing;
		ErrorState = EWorkerErrorState::WorkerProcess_Lost;
	}
}

// void FDatasmithWorkerHandler::RestartProcessor()
// {
// 	FPlatformProcess::TerminateProc(WorkerHandle, true);
// 	StartWorker();
// }

void FDatasmithWorkerHandler::Run()
{
	WorkerState = EWorkerState::Uninitialized;
	RunInternal();
	UE_CLOG(ErrorState != EWorkerErrorState::Ok, LogDatasmithDispatcher, Error, TEXT("ErrorState != OK on exit (%d)"), uint32(ErrorState));
	WorkerState = EWorkerState::Terminated;
}

void FDatasmithWorkerHandler::RunInternal()
{
	while (IsAlive())
	{
		switch (WorkerState)
		{
			case EWorkerState::Uninitialized:
			{
				ErrorState = EWorkerErrorState::Ok;

				StartWorkerProcess();

				if (ErrorState != EWorkerErrorState::Ok)
				{
					WorkerState = EWorkerState::Terminated;
					break;
				}

				// The Accept() call on the server blocks until a connection is initiated from a client
				static const FString SocketDescription = TEXT("DatasmithWorkerHandler");
				double AcceptTimeout_s = 300;
				if (!NetworkInterface.Accept(SocketDescription, AcceptTimeout_s))
				{
					ErrorState = EWorkerErrorState::ConnectionFailed_NoClient;
				}

				if (ErrorState != EWorkerErrorState::Ok)
				{
					WorkerState = EWorkerState::Closing;
					break;
				}

				WorkerState = EWorkerState::Idle;
				break;
			}

			case EWorkerState::Idle:
			{
// 				// Verify connection
// 				if (!ClientSocketWrapper.IsConnected() || !ClientSocketWrapper.CanWrite())
// 				{
// 					ErrorState = EWorkerErrorState::ConnectionLost;
// 					if (CurrentTask.IsSet())
// 					{
// 						Dispatcher.SetTaskState(CurrentTask->Index, ETaskState::ProcessFailed);
// 						CurrentTask.Reset();
// 					}
//
// 					// Restart processor
// 					FPlatformProcess::TerminateProc(WorkerHandle, true);
// 					WorkerState = EWorkerState::Uninitialized;
// 						// RestartProcessor();
// 				}

				// Fetch a new task
				ensureMsgf(CurrentTask.IsSet() == false, TEXT("We should not have a current task when fetching a new one"));
				CurrentTask = Dispatcher.GetNextTask();

				if (CurrentTask.IsSet())
				{
					FRunTaskCommand NewTask(CurrentTask.GetValue());

					double SendTimeout_s = 3.0;
					if (CommandIO.SendCommand(NewTask, SendTimeout_s))
					{
						UE_LOG(LogDatasmithDispatcher, Display, TEXT("New task command sent"));
						WorkerState = EWorkerState::Processing;
					}
					else
					{
						// Signal that the Task was not processed
						Dispatcher.SetTaskState(CurrentTask->Index, ETaskState::UnTreated);

						UE_LOG(LogDatasmithDispatcher, Error, TEXT("New task command issue"));
						WorkerState = EWorkerState::Closing;
						ErrorState = EWorkerErrorState::ConnectionLost_SendFailed;
					}
				}
				else if (bShouldTerminate)
				{
					UE_LOG(LogDatasmithDispatcher, Display, TEXT("Exit loop gracefully"));
					WorkerState = EWorkerState::Closing;
				}
				else
				{
					ValidateConnection();

					// consume
					if (TSharedPtr<ICommand> Command = CommandIO.GetNextCommand(0.1))
					{
						ProcessCommand(*Command);
					}
				}

				break;
			}

			case EWorkerState::Processing:
			{
				if (TSharedPtr<ICommand> Command = CommandIO.GetNextCommand(0.5))
				{
					ProcessCommand(*Command);

					bool bProcessingOver = CurrentTask.IsSet() == false;
					if (bProcessingOver)
					{
						WorkerState = bShouldTerminate ? EWorkerState::Closing : EWorkerState::Idle;
					}
				}
				else
				{
					ValidateConnection();
					if (ErrorState == EWorkerErrorState::WorkerProcess_Lost)
					{
						WorkerState = EWorkerState::Restarting;
					}
				}
				break;
			}

			case EWorkerState::Restarting:
			{
				// TODO:
				// close the network interface but fetch all data before
				// StartWorkerProcess();
				// reconnect network (Accept...)

				WorkerState = EWorkerState::Closing; // Not implemented yet
				break;
			}

			case EWorkerState::Closing:
			{
				// try to close the process gracefully
				bool CloseByCommand = NetworkInterface.IsValid() &&
					(!WorkerHandle.IsValid() || FPlatformProcess::IsProcRunning(WorkerHandle));

				if (CloseByCommand)
				{
					FTerminateCommand Terminate;
					CommandIO.SendCommand(Terminate, 0);
				}

				if (WorkerHandle.IsValid())
				{
					int32 CloseTimeout_s = CloseByCommand ? 1 : 0;

					bool bClosed = false;
					for (int32 i = 0; i < 10 * CloseTimeout_s; ++i)
					{
						if (!FPlatformProcess::IsProcRunning(WorkerHandle))
						{
							bClosed = true;
							break;
						}
						FPlatformProcess::Sleep(0.1);
					}

					if (!bClosed)
					{
						FPlatformProcess::TerminateProc(WorkerHandle, true);
					}
				}

				CommandIO.Disconnect(0);

				// Process commands still in input queue
				while (TSharedPtr<ICommand> Command = CommandIO.GetNextCommand(0))
				{
					ProcessCommand(*Command);
				}

				WorkerState = EWorkerState::Terminated;
				break;
			}

			default:
			{
				ensureMsgf(false, TEXT("missing case handling"));
			}
		}
	}
}


void FDatasmithWorkerHandler::Stop()
{
	bShouldTerminate = true;
}

bool FDatasmithWorkerHandler::IsAlive() const
{
	return WorkerState != EWorkerState::Terminated;
}

void FDatasmithWorkerHandler::ProcessCommand(ICommand& Command)
{
	switch (Command.GetType())
	{
		case ECommandId::Ping:
			ProcessCommand(StaticCast<FPingCommand&>(Command));
			break;

		case ECommandId::NotifyEndTask:
			ProcessCommand(StaticCast<FCompletedTaskCommand&>(Command));
			break;

		default:
			break;
	}
}

void FDatasmithWorkerHandler::ProcessCommand(FPingCommand& PingCommand)
{
	DatasmithDispatcher::FBackPingCommand BackPing;
	CommandIO.SendCommand(BackPing, 0);
}

void FDatasmithWorkerHandler::ProcessCommand(FCompletedTaskCommand& CompletedTaskCommand)
{
	if (!CurrentTask.IsSet())
	{
		return;
	}

	FString CurrentPath = FPaths::GetPath(CurrentTask->FileName);
	for (const FString& ExternalReferenceFile : CompletedTaskCommand.ExternalReferences)
	{
		Dispatcher.AddTask(FPaths::Combine(CurrentPath, ExternalReferenceFile));
	}
	FString CurrentFileName = FPaths::GetCleanFilename(CurrentTask->FileName);
	Dispatcher.SetTaskState(CurrentTask->Index, CompletedTaskCommand.ProcessResult);
	Dispatcher.LinkCTFileToUnrealCacheFile(CurrentFileName, CompletedTaskCommand.SceneGraphFileName, CompletedTaskCommand.GeomFileName);
	CurrentTask.Reset();
}
} // ns DatasmithDispatcher
