// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformProcess.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformProcess.h"
#include "Windows/WindowsPlatformMisc.h"
#elif PLATFORM_MAC
#include "Mac/MacPlatformMisc.h"
#include "Mac/MacPlatformProcess.h"
#endif

//struct FProcHandle;

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
