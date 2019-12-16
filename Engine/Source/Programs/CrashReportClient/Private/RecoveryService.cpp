// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RecoveryService.h"

#if CRASH_REPORT_WITH_RECOVERY

#include "HAL/FileManager.h"
#include "CrashReportClient.h" // For CrashReportClientLog
#include "Interfaces/IPluginManager.h"
#include "IMessagingModule.h"
#include "ConcertSettings.h"
#include "ConcertSyncSessionFlags.h"
#include "IConcertServer.h"
#include "IConcertSession.h"
#include "IConcertSyncServer.h"
#include "IConcertSyncServerModule.h"
#include "ConcertMessageData.h"
#include "Runtime/Launch/Resources/Version.h"

static const TCHAR RecoveryServiceName[] = TEXT("Disaster Recovery Service");

bool FRecoveryService::CollectFiles(const FString& DestDir, bool bMetaDataOnly, bool bAnonymizeMetaData)
{
	auto LogError = [](const TCHAR* Reason)
	{
		UE_LOG(CrashReportClientLog, Error, TEXT("Failed to collect recovery session file(s). %s"), Reason);
	};

	if (!Server)
	{
		LogError(TEXT("The recovery service is not running."));
		return false;
	}
	else if (!IFileManager::Get().DirectoryExists(*DestDir))
	{
		LogError(TEXT("The destination folder doesn't exist."));
		return false;
	}

	FGuid ExportedSessionId = GetRecoverySessionId();
	if (!ExportedSessionId.IsValid())
	{
		LogError(TEXT("The session session could not be found."));
		return false;
	}

	FText ErrorMsg;
	FConcertSessionFilter Filter;
	Filter.bMetaDataOnly = bMetaDataOnly;
	if (!Server->GetConcertServer()->ExportSession(ExportedSessionId, Filter, DestDir, bAnonymizeMetaData, ErrorMsg))
	{
		LogError(TEXT("Server failed to export the session."));
		return false;
	}

	return true;
}

bool FRecoveryService::Startup()
{
#if UE_BUILD_SHIPPING && (!defined(PLATFORM_SUPPORTS_MESSAGEBUS) || !PLATFORM_SUPPORTS_MESSAGEBUS)
	#error PLATFORM_SUPPORTS_MESSAGEBUS was explicitly defined in CrashReportClient.Target.cs for shipping configuration. MessageBus is required by Concert. Ensure it is still enabled.
#endif
	if (!IMessagingModule::Get().GetDefaultBus())
	{
		UE_LOG(CrashReportClientLog, Error, TEXT("MessageBus is not enabled in this configuration. Recovery service will be disabled!"));
		return false;
	}

	if (!IConcertSyncServerModule::IsAvailable())
	{
		UE_LOG(CrashReportClientLog, Error, TEXT("ConcertSyncServer Module is missing. Recovery service will be disabled!"));
		return false;
	}

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("UdpMessaging");
	if (!Plugin || !Plugin->IsEnabled())
	{
		// The UdpMessaging plugin should be added to the {appname}.Target.cs build file.
		UE_LOG(CrashReportClientLog, Error, TEXT("The 'UDP Messaging' plugin is disabled. The Concert server only supports UDP protocol. Recovery service will be disabled!"));
		return false;
	}

	// Setup the disaster recovery server configuration
	UConcertServerConfig* ServerConfig = IConcertSyncServerModule::Get().ParseServerSettings(FCommandLine::Get());
	ServerConfig->bAutoArchiveOnReboot = true; // If server crashed, was killed, etc, ensure the recovery session is archived (expected by recovery flow).
	ServerConfig->EndpointSettings.RemoteEndpointTimeoutSeconds = 0;
	ServerConfig->bMountDefaultSessionRepository = false; // Let the client mount its own repository to support concurrent recovery server and prevent them from concurrently accessing non-sharable database files.

	FConcertSessionFilter AutoArchiveSessionFilter;
	AutoArchiveSessionFilter.bIncludeIgnoredActivities = true;

	// Start disaster recovery server.
	Server = IConcertSyncServerModule::Get().CreateServer(TEXT("DisasterRecovery"), AutoArchiveSessionFilter);
	Server->Startup(ServerConfig, EConcertSyncSessionFlags::Default_DisasterRecoverySession);

	UE_LOG(CrashReportClientLog, Display, TEXT("%s Initialized (Name: %s, Version: %d.%d, Role: %s)"), RecoveryServiceName, *Server->GetConcertServer()->GetServerInfo().ServerName, ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, *Server->GetConcertServer()->GetRole());
	return true;
}

void FRecoveryService::Shutdown()
{
	if (Server)
	{
		Server->Shutdown();
		Server.Reset();
		UE_LOG(CrashReportClientLog, Display, TEXT("%s Shutdown"), RecoveryServiceName);
	}
}

FGuid FRecoveryService::GetRecoverySessionId() const
{
	// As long as the Concert server is up, the session would remain live (it's going to be archived when the server shutdown or reboot).
	for (TSharedPtr<IConcertServerSession>& Session : Server->GetConcertServer()->GetSessions())
	{
		// As convention, the disaster recovery session names starts with the server name, followed by the project name, and date time.
		if (Session->GetName().StartsWith(Server->GetConcertServer()->GetServerInfo().ServerName))
		{
			return Session->GetId();
		}
	}

	return FGuid();// Not found.
}

#endif
