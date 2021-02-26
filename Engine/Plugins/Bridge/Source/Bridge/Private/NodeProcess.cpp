// Copyright Epic Games, Inc. All Rights Reserved.
#include "NodeProcess.h"
#include "Misc/Paths.h"

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
	FString MainFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(GetPluginPath(), TEXT("ThirdParty\\Win"), TEXT("node-bifrost.exe")));
	return *MainFilePath;
#else 
	FString MainFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(GetPluginPath(), TEXT("ThirdParty\\Mac"), TEXT("node-bifrost")));
	return *MainFilePath;
#endif
}

void FNodeProcessManager::StartNodeProcess()
{
	const FString ProcessURL = GetProcessURL();

#if PLATFORM_WINDOWS
	/*const TCHAR* pathVar = TEXT("NODE_OPTIONS");
	const TCHAR* pathValue = TEXT("");
	FPlatformMisc::SetEnvironmentVar(pathVar, pathValue);*/

	NodeProcessHandle = FPlatformProcess::CreateProc(*ProcessURL, NULL, false, true, true, &OutProcessId, 0, NULL, NULL, NULL);
	if (!NodeProcessHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("There was a problem in creating the Node process"));
	}
	int32 ReturnCode;	
	FPlatformProcess::GetProcReturnCode(NodeProcessHandle, &ReturnCode);
	if (ReturnCode < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Error : Process return %s"), *FString::FromInt(ReturnCode));
	}
#elif PLATFORM_MAC
	// Set env variable for logged in user
 	const TCHAR *loginVar = TEXT("LOGIN");
	const FString userName = FMacPlatformMisc::GetEnvironmentVariable(loginVar);
	
	const FString path = "/Users/" + userName + "/bin:/usr/local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbni";
	
	const TCHAR *pathVar = TEXT("PATH");
	const TCHAR *pathValue = *path;
	FMacPlatformMisc::SetEnvironmentVar(pathVar, pathValue);

	// Start node process
	NodeProcessHandle = FPlatformProcess::CreateProc(*ProcessURL, NULL, true, true, true, &OutProcessId, 0, NULL, NULL, NULL); 

	if (!NodeProcessHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("There was a problem in creating the Node process"));
	}
	int32 ReturnCode;	
	FPlatformProcess::GetProcReturnCode(NodeProcessHandle, &ReturnCode);
	if (ReturnCode < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Error : Process return %s"), *FString::FromInt(ReturnCode));
	}
#endif
}

void FNodeProcessManager::KillNodeProcess()
{
#if PLATFORM_WINDOWS
	if (OutProcessId > 0) {
		FPlatformProcess::TerminateProc(NodeProcessHandle, true);
	}
#elif PLATFORM_MAC
	if (OutProcessId > 0) {
		std::string KillProcessCommand = "kill -9 " + std::to_string(OutProcessId);
		system(KillProcessCommand.c_str());
	}
#endif
}

void FNodeProcessManager::HandleBrowserUrlChanged(const FText& Url)
{
	UE_LOG(LogTemp, Error, TEXT("URL changed"));
}
