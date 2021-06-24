// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADWorkerImpl.h"

#include "CoreTechFileParser.h"
#include "DatasmithCommands.h"
#include "DatasmithDispatcherConfig.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

using namespace DatasmithDispatcher;


FDatasmithCADWorkerImpl::FDatasmithCADWorkerImpl(int32 InServerPID, int32 InServerPort, const FString& InEnginePluginsPath, const FString& InCachePath)
	: ServerPID(InServerPID)
	, ServerPort(InServerPort)
	, EnginePluginsPath(InEnginePluginsPath)
	, CachePath(InCachePath)
	, PingStartCycle(0)
{
}

bool FDatasmithCADWorkerImpl::Run()
{
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("connect to %d..."), ServerPort);
	bool bConnected = NetworkInterface.Connect(TEXT("Datasmith CAD Worker"), ServerPort, Config::ConnectTimeout_s);
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("connected to %d %s"), ServerPort, bConnected ? TEXT("OK") : TEXT("FAIL"));
	if (bConnected)
	{
		CommandIO.SetNetworkInterface(&NetworkInterface);
	}
	else
	{
		UE_LOG(LogDatasmithCADWorker, Error, TEXT("Server connection failure. exit"));
		return false;
	}

	InitiatePing();

	bool bIsRunning = true;
	while (bIsRunning)
	{
		if (TSharedPtr<ICommand> Command = CommandIO.GetNextCommand(1.0))
		{
			switch(Command->GetType())
			{
				case ECommandId::Ping:
					ProcessCommand(*StaticCast<FPingCommand*>(Command.Get()));
					break;

				case ECommandId::BackPing:
					ProcessCommand(*StaticCast<FBackPingCommand*>(Command.Get()));
					break;

				case ECommandId::RunTask:
					ProcessCommand(*StaticCast<FRunTaskCommand*>(Command.Get()));
					break;

				case ECommandId::ImportParams:
					ProcessCommand(*StaticCast<FImportParametersCommand*>(Command.Get()));
					break;

				case ECommandId::Terminate:
					UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Terminate command received. Exiting."));
					bIsRunning = false;
					break;

				case ECommandId::NotifyEndTask:
				default:
					break;
			}
		}
		else
		{
			if (bIsRunning)
			{
				bIsRunning = ServerPID == 0 ? true : FPlatformProcess::IsApplicationRunning(ServerPID);
				UE_CLOG(!bIsRunning, LogDatasmithCADWorker, Error, TEXT("Worker failure: server lost"));
			}
		}
	}

	UE_CLOG(!bIsRunning, LogDatasmithCADWorker, Verbose, TEXT("Worker loop exit..."));
	CommandIO.Disconnect(0);
	return true;
}

void FDatasmithCADWorkerImpl::InitiatePing()
{
	PingStartCycle = FPlatformTime::Cycles64();
	FPingCommand Ping;
	CommandIO.SendCommand(Ping, Config::SendCommandTimeout_s);
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FPingCommand& PingCommand)
{
	FBackPingCommand BackPing;
	CommandIO.SendCommand(BackPing, Config::SendCommandTimeout_s);
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FBackPingCommand& BackPingCommand)
{
	if (PingStartCycle)
	{
		double ElapsedTime_s = FGenericPlatformTime::ToSeconds(FPlatformTime::Cycles64() - PingStartCycle);
		UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Ping %f s"), ElapsedTime_s);
	}
	PingStartCycle = 0;
}

void FDatasmithCADWorkerImpl::ProcessCommand(const FImportParametersCommand& ImportParametersCommand)
{
	ImportParameters = ImportParametersCommand.ImportParameters;
}

uint64 DefineMaximumAllowedDuration(const CADLibrary::FFileDescription& FileDescription, CADLibrary::FImportParameters ImportParameters)
{
	FFileStatData FileStatData = IFileManager::Get().GetStatData(*FileDescription.Path);
	double MaxTimePerMb = 5e-6;
	double SafetyCoeficient = (ImportParameters.StitchingTechnique == CADLibrary::EStitchingTechnique::StitchingNone) ? 5 : 15;
	uint64 MinMaximumAllowedDuration = (ImportParameters.StitchingTechnique == CADLibrary::EStitchingTechnique::StitchingNone) ? 30 : 90;
	
	if (FileDescription.Extension.StartsWith(TEXT("sld"))) // SW
	{
		MaxTimePerMb = 1e-5;
	}
	else if (FileDescription.Extension == TEXT("3dxml") || FileDescription.Extension == TEXT("3drep")) // Catia V5 CGR
	{
		MaxTimePerMb = 1e-5;
	}
	else if (FileDescription.Extension == TEXT("cgr")) 
	{
		MaxTimePerMb = 5e-7;
	}
	else if (FileDescription.Extension.StartsWith(TEXT("ig"))) // Iges
	{
		MaxTimePerMb = 1e-6;
	}

	uint64 MaximumDuration = ((double)FileStatData.FileSize) * MaxTimePerMb * SafetyCoeficient;
	return FMath::Max(MaximumDuration, MinMaximumAllowedDuration);
}


void FDatasmithCADWorkerImpl::ProcessCommand(const FRunTaskCommand& RunTaskCommand)
{
	const CADLibrary::FFileDescription& FileToProcess = RunTaskCommand.JobFileDescription;
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Process %s %s"), *FileToProcess.Name, *FileToProcess.Configuration);

	FCompletedTaskCommand CompletedTask;

	bProcessIsRunning = true;
	int64 MaxDuration = DefineMaximumAllowedDuration(FileToProcess, ImportParameters);

	FThread TimeCheckerThread = FThread(TEXT("TimeCheckerThread"), [&]() { CheckDuration(FileToProcess, MaxDuration); });

	CADLibrary::FCoreTechFileParser FileParser(ImportParameters, EnginePluginsPath, CachePath);
	CADLibrary::ECoreTechParsingResult ProcessResult = FileParser.ProcessFile(FileToProcess);

	bProcessIsRunning = false;
	TimeCheckerThread.Join();

	CompletedTask.ProcessResult = ProcessResult;

	if (CompletedTask.ProcessResult == ETaskState::ProcessOk)
	{
		CompletedTask.ExternalReferences = FileParser.GetExternalRefSet();
		CompletedTask.SceneGraphFileName = FileParser.GetSceneGraphFile();
		CompletedTask.GeomFileName = FileParser.GetMeshFileName();
		CompletedTask.WarningMessages = FileParser.GetWarningMessages();
	}

	CommandIO.SendCommand(CompletedTask, Config::SendCommandTimeout_s);

	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("End of Process %s %s saved in %s"), *FileToProcess.Name, *FileToProcess.Configuration, *CompletedTask.GeomFileName);
}

void FDatasmithCADWorkerImpl::CheckDuration(const CADLibrary::FFileDescription& FileToProcess, const int64 MaxDuration)
{
	if (!ImportParameters.bEnableTimeControl)
	{
		return;
	}

	const uint64 StartTime = FPlatformTime::Cycles64();
	const uint64 MaxCycles = MaxDuration / FPlatformTime::GetSecondsPerCycle64() + StartTime;

	while(bProcessIsRunning)
	{
		FPlatformProcess::Sleep(0.1f);
		if (FPlatformTime::Cycles64() > MaxCycles)
		{
			UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("Time exceeded to process %s %s. The maximum allowed duration is %ld s"), *FileToProcess.Name, *FileToProcess.Configuration, MaxDuration);
			FPlatformMisc::RequestExit(true);
		}
	}
	double Duration = (FPlatformTime::Cycles64() - StartTime) * FPlatformTime::GetSecondsPerCycle64();
	UE_LOG(LogDatasmithCADWorker, Verbose, TEXT("    Processing Time: %f s"), Duration);
}
