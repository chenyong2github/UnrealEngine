// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealMultiUserServerRun.h"

#include "ConcertSettings.h"
#include "ConcertSyncServerLoop.h"
#include "IConcertServerUIModule.h"

#include "Misc/Parse.h"

namespace UE::UnrealMultiUserServer
{
	static void OptionallySetupSlate(int ArgC, TCHAR* ArgV[], FConcertSyncServerLoopInitArgs& ServerLoopInitArgs)
	{
		const FString CommandLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, nullptr);

		bool bUseSlate = false;
		bUseSlate |= FParse::Bool(*CommandLine, TEXT("-WITHSLATE"), bUseSlate);
		FParse::Bool(*CommandLine, TEXT("-WITHSLATE="), bUseSlate);
		
		if (bUseSlate)
		{
			ServerLoopInitArgs.bShowConsole = false;
			ServerLoopInitArgs.PreInitServerLoop.AddLambda([&ServerLoopInitArgs]()	
			{
				TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ConcertServerUI"));
				if (!Plugin || !Plugin->IsEnabled())
				{
					UE_LOG(LogSyncServer, Error, TEXT("The 'ConcertServerUI' plugin is disabled."));
				}
				else
				{
					IConcertServerUIModule::Get().InitSlateForServer(ServerLoopInitArgs);
				}
			});
		}
	}
}

int32 RunUnrealMultiUserServer(int ArgC, TCHAR* ArgV[])
{
	FString Role(TEXT("MultiUser"));
	FConcertSyncServerLoopInitArgs ServerLoopInitArgs;
	ServerLoopInitArgs.SessionFlags = EConcertSyncSessionFlags::Default_MultiUserSession;
	ServerLoopInitArgs.ServiceRole = Role;
	ServerLoopInitArgs.ServiceFriendlyName = TEXT("Multi-User Editing Server");

	ServerLoopInitArgs.GetServerConfigFunc = [Role]() -> const UConcertServerConfig*
	{
		UConcertServerConfig* ServerConfig = IConcertSyncServerModule::Get().ParseServerSettings(FCommandLine::Get());
		if (ServerConfig->WorkingDir.IsEmpty())
		{
			ServerConfig->WorkingDir = FPaths::ProjectIntermediateDir() / Role;
		}
		if (ServerConfig->ArchiveDir.IsEmpty())
		{
			ServerConfig->ArchiveDir = FPaths::ProjectSavedDir() / Role;
		}
		return ServerConfig;
	};

	UE::UnrealMultiUserServer::OptionallySetupSlate(ArgC, ArgV, ServerLoopInitArgs);
	return ConcertSyncServerLoop(ArgC, ArgV, ServerLoopInitArgs);
}
