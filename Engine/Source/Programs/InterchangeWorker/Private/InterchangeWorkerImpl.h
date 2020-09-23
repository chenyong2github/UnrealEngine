// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "InterchangeWorker.h"

#include "InterchangeCommands.h"
#include "InterchangeDispatcherNetworking.h"

#include "HAL/PlatformTime.h"


struct FFileStatData;
struct FImportParameters;

class FInterchangeWorkerImpl
{
public:
	FInterchangeWorkerImpl(int32 InServerPID, int32 InServerPort, FString& InResultFolder);
	bool Run();

private:
	void InitiatePing();
	void ProcessCommand(const InterchangeDispatcher::FPingCommand& PingCommand);
	void ProcessCommand(const InterchangeDispatcher::FBackPingCommand& BackPingCommand);
	void ProcessCommand(const InterchangeDispatcher::FRunTaskCommand& TerminateCommand);

	InterchangeDispatcher::ETaskState LoadFbxFile(const InterchangeDispatcher::FJsonLoadSourceCmd& LoadSourceCommand, FString& OutJSonResult, TArray<FString>& OutJSonMessages) const;

private:
	InterchangeDispatcher::FNetworkClientNode NetworkInterface;
	InterchangeDispatcher::FCommandQueue CommandIO;

	int32 ServerPID;
	int32 ServerPort;
	uint64 PingStartCycle;
	FString ResultFolder;
};
