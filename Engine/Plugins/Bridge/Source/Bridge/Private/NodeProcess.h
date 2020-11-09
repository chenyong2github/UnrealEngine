// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if PLATFORM_WINDOWS
#include "GenericPlatform/GenericPlatformProcess.h"
#elif PLATFORM_MAC
#include "Mac/MacPlatformMisc.h"
#endif

class FNodeProcessManager
{
private:
	FNodeProcessManager() = default;	
	static TSharedPtr<FNodeProcessManager> NodeProcessManager;

	const FString BridgePluginName = TEXT("Bridge");
	uint32 OutProcessId = 0;
	FProcHandle NodeProcessHandle;

	const FString GetProcessURL() const;
	const FString GetPluginPath() const;	

public:
	static TSharedPtr<FNodeProcessManager> Get();
	void StartNodeProcess();
	void KillNodeProcess() ;
	bool bIsNodeRunning = false ;
	void HandleBrowserUrlChanged(const FText& Url);
};
