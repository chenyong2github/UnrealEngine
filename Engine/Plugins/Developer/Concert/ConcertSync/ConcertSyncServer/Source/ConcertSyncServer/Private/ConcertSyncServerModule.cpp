// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IConcertSyncServerModule.h"
#include "ConcertSyncServer.h"
#include "ConcertSettings.h"

/**
 * 
 */
class FConcertSyncServerModule : public IConcertSyncServerModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}

	virtual UConcertServerConfig* ParseServerSettings(const TCHAR* CommandLine) override
	{
		UConcertServerConfig* ServerConfig = NewObject<UConcertServerConfig>();

		if (CommandLine)
		{
			// Parse value overrides (if present)
			FParse::Value(CommandLine, TEXT("-CONCERTSERVER="), ServerConfig->ServerName);
			FParse::Value(CommandLine, TEXT("-CONCERTSESSION="), ServerConfig->DefaultSessionName);
			FParse::Value(CommandLine, TEXT("-CONCERTSESSIONTORESTORE="), ServerConfig->DefaultSessionToRestore);
			FParse::Value(CommandLine, TEXT("-CONCERTSAVESESSIONAS="), ServerConfig->DefaultSessionSettings.ArchiveNameOverride);
			FParse::Value(CommandLine, TEXT("-CONCERTPROJECT="), ServerConfig->DefaultSessionSettings.ProjectName);
			FParse::Value(CommandLine, TEXT("-CONCERTREVISION="), ServerConfig->DefaultSessionSettings.BaseRevision);
			FParse::Value(CommandLine, TEXT("-CONCERTWORKINGDIR="), ServerConfig->WorkingDir);
			FParse::Value(CommandLine, TEXT("-CONCERTSAVEDDIR="), ServerConfig->ArchiveDir);

			ServerConfig->ServerSettings.bIgnoreSessionSettingsRestriction |= FParse::Param(CommandLine, TEXT("CONCERTIGNORE"));
			FParse::Bool(CommandLine, TEXT("-CONCERTIGNORE="), ServerConfig->ServerSettings.bIgnoreSessionSettingsRestriction);
			
			ServerConfig->bCleanWorkingDir |= FParse::Param(CommandLine, TEXT("CONCERTCLEAN"));
			FParse::Bool(CommandLine, TEXT("-CONCERTCLEAN="), ServerConfig->bCleanWorkingDir);
		}

		return ServerConfig;
	}

	virtual TSharedRef<IConcertSyncServer> CreateServer(const FString& InRole) override
	{
		return MakeShared<FConcertSyncServer>(InRole);
	}
};

IMPLEMENT_MODULE(FConcertSyncServerModule, ConcertSyncServer);
