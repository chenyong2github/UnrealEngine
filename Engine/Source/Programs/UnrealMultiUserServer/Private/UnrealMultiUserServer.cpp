// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertSettings.h"
#include "ConcertSyncServerLoop.h"

#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(UnrealMultiUserServer, "UnrealMultiUserServer");

/**
 * Application entry point
 *
 * @param	ArgC	Command-line argument count
 * @param	ArgV	Argument strings
 */
INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FConcertSyncServerLoopInitArgs ServerLoopInitArgs;
	ServerLoopInitArgs.SessionFlags = EConcertSyncSessionFlags::Default_MultiUserSession;
	ServerLoopInitArgs.ServiceRole = TEXT("MultiUser");
	ServerLoopInitArgs.ServiceFriendlyName = TEXT("Multi-User Editing Server");

	return ConcertSyncServerLoop(ArgC, ArgV, ServerLoopInitArgs);
}
