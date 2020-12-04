// Copyright Epic Games, Inc. All Rights Reserved.
#include "NodeProcess.h"
#include "Misc/Paths.h"
//#include "Lumin/LuminPlatformProcess.h"
//#include "GenericPlatform/GenericPlatformMisc.h"
#include <iostream>
#include <stdlib.h>
#include <string>

TSharedPtr<FNodeProcessManager> FNodeProcessManager::NodeProcessManager;

TSharedPtr<FNodeProcessManager> FNodeProcessManager::Get()
{
	if (!NodeProcessManager.IsValid())
	{
		NodeProcessManager = MakeShareable(new FNodeProcessManager);
	}
	return NodeProcessManager;
}

const FString FNodeProcessManager::GetPluginPath() const
{
	return FPaths::Combine(FPaths::EnginePluginsDir(), BridgePluginName);
}

const FString FNodeProcessManager::GetProcessURL() const
{
#if PLATFORM_WINDOWS
	FString MainFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(GetPluginPath(), TEXT("Content\\Win"), TEXT("index.exe")));
	return *MainFilePath;
#else 
	FString MainFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(GetPluginPath(), TEXT("Content\\Mac"), TEXT("index")));
	return *MainFilePath;
#endif
}

void FNodeProcessManager::StartNodeProcess()
{
	const FString ProcessURL = GetProcessURL();
	FString ProcessParams = TEXT("");

//#if PLATFORM_WINDOWS
	//FPlatformProcess::CreateProc(*ProcessURL, *ProcessParams, true, true, true, &OutProcessId, 0, NULL, NULL, NULL);
	/*if (!NodeProcessHandle.IsValid())
	{
		
	}
	int32 ReturnCode;	
	FPlatformProcess::GetProcReturnCode(NodeProcessHandle, &ReturnCode);
	if (ReturnCode < 0)
	{
		
	}*/

	/* std::string command = "echo " + std::to_string(OutProcessId) + " >> WIN_NODE_PROCESS_ID.txt"; */
	/* system(command.c_str()); */
//#elif PLATFORM_MAC
	// Set env variable for logged in user
 	//const TCHAR *loginVar = TEXT("LOGIN");
	//const FString userName = FMacPlatformMisc::GetEnvironmentVariable(loginVar);
	
	//const FString path = "/Users/" + userName + "/bin:/usr/local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbni";
	
	//const TCHAR *pathVar = TEXT("PATH");
	//const TCHAR *pathValue = *path;
	//FMacPlatformMisc::SetEnvironmentVar(pathVar, pathValue);

	// Start node process
	//FPlatformProcess::CreateProc(*ProcessURL, TEXT(""), true, true, true, &OutProcessId, 0, NULL, NULL, NULL); 

	/*if (!NodeProcessHandle.IsValid())
	{
		
	}
	int32 ReturnCode;	
	FPlatformProcess::GetProcReturnCode(NodeProcessHandle, &ReturnCode);
	if (ReturnCode < 0)
	{
		
	}*/


//#endif
}

void FNodeProcessManager::KillNodeProcess()
{
	if (OutProcessId > 0) {
		//FPlatformProcess::TerminateProc(NodeProcessHandle, true);
	}
}

void FNodeProcessManager::HandleBrowserUrlChanged(const FText& Url)
{
	//UE_LOG(LogTemp, Error, TEXT("URL changed"));
}
