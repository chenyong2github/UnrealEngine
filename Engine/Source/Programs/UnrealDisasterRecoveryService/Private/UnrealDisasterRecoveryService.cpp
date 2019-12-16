// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertSettings.h"
#include "ConcertSyncServerLoop.h"

#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(UnrealDisasterRecoveryService, "UnrealDisasterRecoveryService");

/**
 * Application entry point
 *
 * @param	ArgC	Command-line argument count
 * @param	ArgV	Argument strings
 */
INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	uint32 EditorProcessId = 0;

	FConcertSyncServerLoopInitArgs ServerLoopInitArgs;
	ServerLoopInitArgs.IdealFramerate = 30;
	ServerLoopInitArgs.SessionFlags = EConcertSyncSessionFlags::Default_DisasterRecoverySession;
	ServerLoopInitArgs.ServiceRole = TEXT("DisasterRecovery");
	ServerLoopInitArgs.ServiceFriendlyName = TEXT("Disaster Recovery Service");
	ServerLoopInitArgs.ServiceAutoArchiveSessionFilter.bIncludeIgnoredActivities = true;
	ServerLoopInitArgs.bShowConsole = false;

	ServerLoopInitArgs.GetServerConfigFunc = [&EditorProcessId]() -> const UConcertServerConfig*
	{
		FParse::Value(FCommandLine::Get(), TEXT("-EDITORPID="), EditorProcessId);
		if (EditorProcessId)
		{
			UE_LOG(LogSyncServer, Display, TEXT("Watching Editor process %d"), EditorProcessId);
		}
		else
		{
			UE_LOG(LogSyncServer, Error, TEXT("Invalid -EditorPID argument. Cannot continue!"));
			return nullptr;
		}

		UConcertServerConfig* ServerConfig = IConcertSyncServerModule::Get().ParseServerSettings(FCommandLine::Get());
		ServerConfig->bAutoArchiveOnReboot = true; // If server crashed, was killed, etc, ensure the recovery session is archived (expected by recovery flow).
		ServerConfig->EndpointSettings.RemoteEndpointTimeoutSeconds = 0;
		ServerConfig->bMountDefaultSessionRepository = false; // Let the client mount its own repository to support concurrent service and avoid them to acces the same non-sharable database files.
		return ServerConfig;
	};

	FTicker::GetCoreTicker().AddTicker(TEXT("CheckEditorHealth"), 1.0f, [&EditorProcessId](float)
	{
		if (!FPlatformProcess::IsApplicationRunning(EditorProcessId))
		{
			UE_LOG(LogSyncServer, Warning, TEXT("Editor process %d lost! Requesting exit."), EditorProcessId);
			FPlatformMisc::RequestExit(false);
		}
		return true;
	});

	return ConcertSyncServerLoop(ArgC, ArgV, ServerLoopInitArgs);
}
