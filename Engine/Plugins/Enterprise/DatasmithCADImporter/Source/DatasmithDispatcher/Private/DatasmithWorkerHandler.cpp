// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithWorkerHandler.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "DatasmithDispatcher.h"
#include "DatasmithDispatcherLog.h"
#include "DatasmithCommands.h"


namespace DatasmithDispatcher
{
FString FDatasmithWorkerHandler::GetExePath()
{
	FString ProcessorPath;
#if PLATFORM_MAC
	ProcessorPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Mac/DatasmithCADWorker"));
#elif PLATFORM_LINUX
	ProcessorPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Linux/DatasmithCADWorker"));
#else
	ProcessorPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64/DatasmithCADWorker.exe"));
#endif

	if (FPaths::FileExists(ProcessorPath))
	{
		return ProcessorPath;
	}
	return FString();
}

FDatasmithWorkerHandler::FDatasmithWorkerHandler(FDatasmithDispatcher& InDispatcher, const FString& InCachePath)
	: Dispatcher(InDispatcher)
	, WorkerProcessId(0)
	, ServerSocket(TEXT("127.0.0.1"))
	, ClientListener()
	, CachePath(InCachePath)
	, bShouldTerminate(false)
{
}

bool FDatasmithWorkerHandler::StartWorker()
{
	FString ProcessorPath = GetExePath();
	if (!ProcessorPath.Len())
	{
		return false;
	}

	FString WorkerAbsolutePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*ProcessorPath);
	FPaths::NormalizeDirectoryName(WorkerAbsolutePath);

	FString CommandToProcess = TEXT(" -Listen ") + FString::FromInt(ServerSocket.GetPort()) + TEXT(" -CacheDir \"") + GetCachePath() + TEXT("\"");

	WorkerHandle = FPlatformProcess::CreateProc(*WorkerAbsolutePath, *CommandToProcess, true, false, false, &WorkerProcessId, 0, nullptr, nullptr);
	//WorkerHandle = FPlatformProcess::CreateProc(*WorkerAbsolutePath, *CommandToProcess, false, false, false, &WorkerProcessId, 0, nullptr, nullptr);

	if (!WorkerHandle.IsValid())
	{
		return false;
	}

	FSocket *ClientSocket = ServerSocket.Accept();
	if (!ClientSocket)
	{
		return false;
	}

	ClientListener.SetSocket(ClientSocket);
	return true;
}


void FDatasmithWorkerHandler::RestartProcessor()
{
	FPlatformProcess::TerminateProc(WorkerHandle, true);
	StartWorker();
}

bool FDatasmithWorkerHandler::InitializeSocket()
{
	ServerSocket.Bind();
	return ServerSocket.IsOpen();
}

bool FDatasmithWorkerHandler::Run()
{

	if (!InitializeSocket())
	{
		return false;
	}

	if(!ServerSocket.Listen())
	{
		return false;
	}

	if (!StartWorker())
	{
		return false;
	}

	DatasmithDispatcher::FDatasmithCommandManager CommandManager(ClientListener);

	int test = 0;
	bool bTaskIsRunning = true;

	while (NeedToRun())
	{

		if (!ClientListener.IsConnected())
		{
			if (CurrentTask.IsSet())
			{
				Dispatcher.SetTaskState(CurrentTask->Index, EProcessState::ProcessFailed);
				CurrentTask.Reset();
			}
			RestartProcessor();
			bTaskIsRunning = true;
			continue;
		}

		// If No task running, launch one
		uint32 DataSize;
		ClientListener.HasPendingData(DataSize);
		if (!bTaskIsRunning && !DataSize)
		{
			CurrentTask = Dispatcher.GetTask();
			if (CurrentTask.IsSet())
			{
				FDatasmithRunTaskCommand AddJob(CurrentTask.GetValue().FileName, CurrentTask.GetValue().Index);
				AddJob.Write(ClientListener);
				bTaskIsRunning = true;
				continue;
			}
		}

		if (DataSize)
		{
			if (ICommand* Command = CommandManager.GetNextCommand())
			{
				switch (Command->GetType())
				{
				case Ping:
					PingProcess(StaticCast<FDatasmithPingCommand*>(Command));
					bTaskIsRunning = false;
					break;
				case BackPing:
					BackPingProcess(StaticCast<FDatasmithBackPingCommand*>(Command));
					break;
				case NotifyEndTask:
				{
					EndTaskProcess(StaticCast<FDatasmithNotifyEndTaskCommand*>(Command));
					bTaskIsRunning = false;
					break;
				}

				case ImportParams:
				case RunTask:
				default:
					break;
				}
			}
		}

		FWindowsPlatformProcess::Sleep(0.001);
	}

	Kill();
	return true;
}

void FDatasmithWorkerHandler::PingProcess(FDatasmithPingCommand* PingCommand)
{
	DatasmithDispatcher::FDatasmithBackPingCommand NewCommand;
	NewCommand.Write(ClientListener);
}

void FDatasmithWorkerHandler::BackPingProcess(FDatasmithBackPingCommand* PingCommand)
{
	return ;
}

void FDatasmithWorkerHandler::EndTaskProcess(FDatasmithNotifyEndTaskCommand* RunTaskCommand)
{
	if (!CurrentTask.IsSet())
	{
		return;
	}

	FString CurrentPath = FPaths::GetPath(CurrentTask->FileName);
	for (const FString& ExternalReferenceFile : RunTaskCommand->GetExternalReferences())
	{
		Dispatcher.AddTask(FPaths::Combine(CurrentPath, ExternalReferenceFile));
	}
	FString CurrentFileName = FPaths::GetCleanFilename(CurrentTask->FileName);
	Dispatcher.SetTaskState(CurrentTask->Index, RunTaskCommand->GetProcessResult());
	Dispatcher.LinkCTFileToUnrealCacheFile(CurrentFileName, RunTaskCommand->GetSceneGraphFile(), RunTaskCommand->GetGeomFile());
	CurrentTask.Reset();
}

void FDatasmithWorkerHandler::NotifyKill()
{
	bShouldTerminate = true;
}

bool FDatasmithWorkerHandler::NeedToRun()
{
	return !bShouldTerminate;
}

void FDatasmithWorkerHandler::Kill()
{
	ClientListener.Close();
	ServerSocket.Close();
	FPlatformProcess::TerminateProc(WorkerHandle, true);
}
}
