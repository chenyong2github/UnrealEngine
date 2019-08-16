// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertServer.h"

#include "ConcertUtil.h"
#include "ConcertLogger.h"
#include "ConcertSettings.h"
#include "ConcertServerSession.h"
#include "ConcertLogGlobal.h"
#include "IConcertServerEventSink.h"

#include "Misc/App.h"
#include "Misc/Paths.h"

#include "Runtime/Launch/Resources/Version.h"

#define LOCTEXT_NAMESPACE "ConcertServer"

namespace ConcertServerUtil
{

FString GetArchiveName(const FString& SessionName, const FConcertSessionSettings& Settings)
{
	if (Settings.ArchiveNameOverride.IsEmpty())
	{
		return FString::Printf(TEXT("%s_%s"), *SessionName, *FDateTime::UtcNow().ToString());
	}
	else
	{
		return Settings.ArchiveNameOverride;
	}
}

}


FConcertServerPaths::FConcertServerPaths(const FString& InRole, const FString& InWorkingDir, const FString& InSavedDir)
	: WorkingDir(InWorkingDir.Len() ? InWorkingDir / InRole : (FPaths::ProjectIntermediateDir() / TEXT("Concert") / InRole))
	, SavedDir(InSavedDir.Len() ? InSavedDir / InRole : (FPaths::ProjectSavedDir() / TEXT("Concert") / InRole))
	, BaseWorkingDir(InWorkingDir)
	, BaseSavedDir(InSavedDir)
{
}

FConcertServer::FConcertServer(const FString& InRole, IConcertServerEventSink* InEventSink, const TSharedPtr<IConcertEndpointProvider>& InEndpointProvider)
	: Role(InRole)
	, EventSink(InEventSink)
	, EndpointProvider(InEndpointProvider)
{
	check(EventSink);
}

FConcertServer::~FConcertServer()
{
	// if ServerAdminEndpoint is valid, then Shutdown wasn't called
	check(!ServerAdminEndpoint.IsValid());
}

const FString& FConcertServer::GetRole() const
{
	return Role;
}

void FConcertServer::Configure(const UConcertServerConfig* InSettings)
{
	ServerInfo.Initialize();
	check(InSettings != nullptr);
	Settings = TStrongObjectPtr<const UConcertServerConfig>(InSettings);

	Paths = MakeUnique<FConcertServerPaths>(GetRole(), InSettings->WorkingDir, InSettings->ArchiveDir);

	if (!InSettings->ServerName.IsEmpty())
	{
		ServerInfo.ServerName = InSettings->ServerName;
	}

	if (InSettings->ServerSettings.bIgnoreSessionSettingsRestriction)
	{
		ServerInfo.ServerFlags |= EConcertSeverFlags::IgnoreSessionRequirement;
	}
}

bool FConcertServer::IsConfigured() const
{
	// if the instance id hasn't been set yet, then Configure wasn't called.
	return Settings && ServerInfo.InstanceInfo.InstanceId.IsValid();
}

const UConcertServerConfig* FConcertServer::GetConfiguration() const
{
	return Settings.Get();
}

const FConcertServerInfo& FConcertServer::GetServerInfo() const
{
	return ServerInfo;
}

bool FConcertServer::IsStarted() const
{
	return ServerAdminEndpoint.IsValid();
}

void FConcertServer::Startup()
{
	check(IsConfigured());
	check (Paths.IsValid());
	if (!ServerAdminEndpoint.IsValid() && EndpointProvider.IsValid())
	{
		// Create the server administration endpoint
		ServerAdminEndpoint = EndpointProvider->CreateLocalEndpoint(TEXT("Admin"), Settings->EndpointSettings, &FConcertLogger::CreateLogger);
		ServerInfo.AdminEndpointId = ServerAdminEndpoint->GetEndpointContext().EndpointId;

		// Make it discoverable
		ServerAdminEndpoint->SubscribeEventHandler<FConcertAdmin_DiscoverServersEvent>(this, &FConcertServer::HandleDiscoverServersEvent);
		
		// Add Session connection handling
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_CreateSessionRequest, FConcertAdmin_SessionInfoResponse>(this, &FConcertServer::HandleCreateSessionRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_FindSessionRequest, FConcertAdmin_SessionInfoResponse>(this, &FConcertServer::HandleFindSessionRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_RestoreSessionRequest, FConcertAdmin_SessionInfoResponse>(this, &FConcertServer::HandleRestoreSessionRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_ArchiveSessionRequest, FConcertAdmin_ArchiveSessionResponse>(this, &FConcertServer::HandleArchiveSessionRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_RenameSessionRequest, FConcertAdmin_RenameSessionResponse>(this, &FConcertServer::HandleRenameSessionRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_DeleteSessionRequest, FConcertAdmin_DeleteSessionResponse>(this, &FConcertServer::HandleDeleteSessionRequest);

		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_GetAllSessionsRequest, FConcertAdmin_GetAllSessionsResponse>(this, &FConcertServer::HandleGetAllSessionsRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_GetLiveSessionsRequest, FConcertAdmin_GetSessionsResponse>(this, &FConcertServer::HandleGetLiveSessionsRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_GetArchivedSessionsRequest, FConcertAdmin_GetSessionsResponse>(this, &FConcertServer::HandleGetArchivedSessionsRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_GetSessionClientsRequest, FConcertAdmin_GetSessionClientsResponse>(this, &FConcertServer::HandleGetSessionClientsRequest);
		ServerAdminEndpoint->RegisterRequestHandler<FConcertAdmin_GetSessionActivitiesRequest, FConcertAdmin_GetSessionActivitiesResponse>(this, &FConcertServer::HandleGetSessionActivitiesRequest);

		if (Settings->bCleanWorkingDir)
		{
			ConcertUtil::DeleteDirectoryTree(*Paths->GetWorkingDir(), *Paths->GetBaseWorkingDir());
		}
		else
		{
			if (Settings->bAutoArchiveOnReboot)
			{
				// Migrate live sessions files (session is not restored yet) to its archive form and directory.
				ArchiveOfflineSessions();
			}

			// Build the list of archive/live sessions and rotate the list of archive to prevent having too many of them.
			RecoverSessions();
		}
	}
}

void FConcertServer::Shutdown()
{
	// Server Query
	if (ServerAdminEndpoint.IsValid())
	{
		// Discovery
		ServerAdminEndpoint->UnsubscribeEventHandler<FConcertAdmin_DiscoverServersEvent>();

		// Session connection
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_CreateSessionRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_FindSessionRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_RestoreSessionRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_ArchiveSessionRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_RenameSessionRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_DeleteSessionRequest>();

		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_GetAllSessionsRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_GetLiveSessionsRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_GetArchivedSessionsRequest>();
		ServerAdminEndpoint->UnregisterRequestHandler<FConcertAdmin_GetSessionClientsRequest>();

		ServerAdminEndpoint.Reset();
	}

	// Destroy the live sessions
	{
		const bool bAutoArchiveOnShutdown = true;

		TArray<FGuid> LiveSessionIds;
		LiveSessions.GetKeys(LiveSessionIds);
		for (const FGuid& LiveSessionId : LiveSessionIds)
		{
			bool bDeleteSessionData = true;
			if (bAutoArchiveOnShutdown)
			{
				bDeleteSessionData = ArchiveLiveSession(LiveSessionId, FString(), FConcertSessionFilter()).IsValid();
			}
			DestroyLiveSession(LiveSessionId, bDeleteSessionData);
		}
		LiveSessions.Reset();
	}

	// Destroy the archived sessions
	{
		TArray<FGuid> ArchivedSessionIds;
		ArchivedSessions.GetKeys(ArchivedSessionIds);
		for (const FGuid& ArchivedSessionId : ArchivedSessionIds)
		{
			DestroyArchivedSession(ArchivedSessionId, /*bDeleteSessionData*/false);
		}
		ArchivedSessions.Reset();
	}
}

FGuid FConcertServer::GetLiveSessionIdByName(const FString& InName) const
{
	for (const auto& LiveSessionPair : LiveSessions)
	{
		if (LiveSessionPair.Value->GetName() == InName)
		{
			return LiveSessionPair.Key;
		}
	}
	return FGuid();
}

FGuid FConcertServer::GetArchivedSessionIdByName(const FString& InName) const
{
	for (const auto& ArchivedSessionPair : ArchivedSessions)
	{
		if (ArchivedSessionPair.Value.SessionName == InName)
		{
			return ArchivedSessionPair.Key;
		}
	}
	return FGuid();
}

FConcertSessionInfo FConcertServer::CreateSessionInfo() const
{
	FConcertSessionInfo SessionInfo;
	SessionInfo.ServerInstanceId = ServerInfo.InstanceInfo.InstanceId;
	SessionInfo.OwnerInstanceId = ServerInfo.InstanceInfo.InstanceId;
	SessionInfo.OwnerUserName = FApp::GetSessionOwner();
	SessionInfo.OwnerDeviceName = FPlatformProcess::ComputerName();
	SessionInfo.SessionId = FGuid::NewGuid();
	return SessionInfo;
}

TSharedPtr<IConcertServerSession> FConcertServer::CreateSession(const FConcertSessionInfo& SessionInfo, FText& OutFailureReason)
{
	if (!SessionInfo.SessionId.IsValid() || SessionInfo.SessionName.IsEmpty())
	{
		OutFailureReason = LOCTEXT("Error_CreateSession_EmptySessionIdOrName", "Empty session ID or name");
		UE_LOG(LogConcert, Error, TEXT("An attempt to create a session was made, but the session info was missing an ID or name!"));
		return nullptr;
	}

	if (!Settings->ServerSettings.bIgnoreSessionSettingsRestriction && SessionInfo.VersionInfos.Num() == 0)
	{
		OutFailureReason = LOCTEXT("Error_CreateSession_EmptyVersionInfo", "Empty version info");
		UE_LOG(LogConcert, Error, TEXT("An attempt to create a session was made, but the session info was missing version info!"));
		return nullptr;
	}

	if (LiveSessions.Contains(SessionInfo.SessionId))
	{
		OutFailureReason = FText::Format(LOCTEXT("Error_CreateSession_AlreadyExists", "Session '{0}' already exists"), FText::AsCultureInvariant(SessionInfo.SessionId.ToString()));
		UE_LOG(LogConcert, Error, TEXT("An attempt to create a session with ID '%s' was made, but that session already exists!"), *SessionInfo.SessionId.ToString());
		return nullptr;
	}

	if (GetLiveSessionIdByName(SessionInfo.SessionName).IsValid())
	{
		OutFailureReason = FText::Format(LOCTEXT("Error_CreateSession_AlreadyExists", "Session '{0}' already exists"), FText::AsCultureInvariant(SessionInfo.SessionName));
		UE_LOG(LogConcert, Error, TEXT("An attempt to create a session with name '%s' was made, but that session already exists!"), *SessionInfo.SessionName);
		return nullptr;
	}

	return CreateLiveSession(SessionInfo);
}

TSharedPtr<IConcertServerSession> FConcertServer::RestoreSession(const FGuid& SessionId, const FConcertSessionInfo& SessionInfo, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason)
{
	if (!SessionInfo.SessionId.IsValid() || SessionInfo.SessionName.IsEmpty())
	{
		OutFailureReason = LOCTEXT("Error_RestoreSession_EmptySessionIdOrName", "Empty session ID or name");
		UE_LOG(LogConcert, Error, TEXT("An attempt to restore a session was made, but the session info was missing an ID or name!"));
		return nullptr;
	}

	if (!Settings->ServerSettings.bIgnoreSessionSettingsRestriction && SessionInfo.VersionInfos.Num() == 0)
	{
		OutFailureReason = LOCTEXT("Error_RestoreSession_EmptyVersionInfo", "Empty version info");
		UE_LOG(LogConcert, Error, TEXT("An attempt to restore a session was made, but the session info was missing version info!"));
		return nullptr;
	}

	if (LiveSessions.Contains(SessionInfo.SessionId))
	{
		OutFailureReason = FText::Format(LOCTEXT("Error_RestoreSession_AlreadyExists", "Session '{0}' already exists"), FText::AsCultureInvariant(SessionInfo.SessionId.ToString()));
		UE_LOG(LogConcert, Error, TEXT("An attempt to restore a session with ID '%s' was made, but that session already exists!"), *SessionInfo.SessionId.ToString());
		return nullptr;
	}

	if (GetLiveSessionIdByName(SessionInfo.SessionName).IsValid())
	{
		OutFailureReason = FText::Format(LOCTEXT("Error_RestoreSession_AlreadyExists", "Session '{0}' already exists"), FText::AsCultureInvariant(SessionInfo.SessionName));
		UE_LOG(LogConcert, Error, TEXT("An attempt to restore a session with name '%s' was made, but that session already exists!"), *SessionInfo.SessionName);
		return nullptr;
	}

	return RestoreArchivedSession(SessionId, SessionInfo, SessionFilter, OutFailureReason);
}

void FConcertServer::RecoverSessions()
{
	check(LiveSessions.Num() == 0 && ArchivedSessions.Num() == 0);

	// Find any existing live sessions to automatically restore when recovering from an improper server shutdown
	TArray<FConcertSessionInfo> LiveSessionInfos;
	EventSink->GetSessionsFromPath(*this, Paths->GetWorkingDir(), LiveSessionInfos);

	// Restore any existing live sessions
	for (FConcertSessionInfo& LiveSessionInfo : LiveSessionInfos)
	{
		// Update the session info with new server info
		LiveSessionInfo.ServerInstanceId = ServerInfo.InstanceInfo.InstanceId;
		if (!LiveSessions.Contains(LiveSessionInfo.SessionId) && !GetLiveSessionIdByName(LiveSessionInfo.SessionName).IsValid() && CreateLiveSession(LiveSessionInfo))
		{
			UE_LOG(LogConcert, Display, TEXT("Live session '%s' (%s) was recovered."), *LiveSessionInfo.SessionName, *LiveSessionInfo.SessionId.ToString());
		}
	}

	if (Settings->NumSessionsToKeep == 0)
	{
		ConcertUtil::DeleteDirectoryTree(*Paths->GetSavedDir(), *Paths->GetBaseSavedDir());
	}
	else
	{
		// Find any existing archived sessions
		TArray<FConcertSessionInfo> ArchivedSessionInfos;
		TArray<FDateTime> ArchivedSessionLastModifiedTimes;
		EventSink->GetSessionsFromPath(*this, Paths->GetSavedDir(), ArchivedSessionInfos, &ArchivedSessionLastModifiedTimes);
		check(ArchivedSessionInfos.Num() == ArchivedSessionLastModifiedTimes.Num());

		// Trim the oldest archived sessions
		if (Settings->NumSessionsToKeep > 0 && ArchivedSessionInfos.Num() > Settings->NumSessionsToKeep)
		{
			typedef TTuple<int32, FDateTime> FSavedSessionInfo;

			// Build the list of sorted session
			TArray<FSavedSessionInfo> SortedSessions;
			for (int32 LiveSessionInfoIndex = 0; LiveSessionInfoIndex < ArchivedSessionInfos.Num(); ++LiveSessionInfoIndex)
			{
				SortedSessions.Add(MakeTuple(LiveSessionInfoIndex, ArchivedSessionLastModifiedTimes[LiveSessionInfoIndex]));
			}
			SortedSessions.Sort([](const FSavedSessionInfo& InOne, const FSavedSessionInfo& InTwo)
			{
				return InOne.Value < InTwo.Value;
			});

			// Keep the most recent sessions
			TArray<FConcertSessionInfo> ArchivedSessionsToKeep;
			{
				const int32 FirstSortedSessionIndexToKeep = SortedSessions.Num() - Settings->NumSessionsToKeep - 1;
				for (int32 SortedSessionIndex = FirstSortedSessionIndexToKeep; SortedSessionIndex < SortedSessions.Num(); ++SortedSessionIndex)
				{
					ArchivedSessionsToKeep.Add(ArchivedSessionInfos[SortedSessions[SortedSessionIndex].Key]);
				}
				SortedSessions.RemoveAt(FirstSortedSessionIndexToKeep, Settings->NumSessionsToKeep, /*bAllowShrinking*/false);
			}

			// Remove the oldest sessions
			for (const FSavedSessionInfo& SortedSession : SortedSessions)
			{
				ConcertUtil::DeleteDirectoryTree(*Paths->GetSessionWorkingDir(ArchivedSessionInfos[SortedSession.Key].SessionId), *Paths->GetBaseWorkingDir());
			}

			// Update the list of sessions to restore
			ArchivedSessionInfos = MoveTemp(ArchivedSessionsToKeep);
			ArchivedSessionLastModifiedTimes.Reset();
		}

		// Create any existing archived sessions
		for (FConcertSessionInfo& ArchivedSessionInfo : ArchivedSessionInfos)
		{
			// Update the session info with new server info
			ArchivedSessionInfo.ServerInstanceId = ServerInfo.InstanceInfo.InstanceId;
			if (!ArchivedSessions.Contains(ArchivedSessionInfo.SessionId) && !GetArchivedSessionIdByName(ArchivedSessionInfo.SessionName).IsValid() && CreateArchivedSession(ArchivedSessionInfo))
			{
				UE_LOG(LogConcert, Display, TEXT("Archived session '%s' (%s) was discovered."), *ArchivedSessionInfo.SessionName, *ArchivedSessionInfo.SessionId.ToString());
			}
		}
	}
}

void FConcertServer::ArchiveOfflineSessions()
{
	// Find existing live session files to automatically archive them when recovering from an improper server shutdown.
	TArray<FConcertSessionInfo> LiveSessionInfos;
	EventSink->GetSessionsFromPath(*this, Paths->GetWorkingDir(), LiveSessionInfos);

	// Migrate the live sessions files into their archived form.
	for (FConcertSessionInfo& LiveSessionInfo : LiveSessionInfos)
	{
		LiveSessionInfo.ServerInstanceId = ServerInfo.InstanceInfo.InstanceId;
		FConcertSessionInfo ArchivedSessionInfo = LiveSessionInfo;
		ArchivedSessionInfo.SessionId = FGuid::NewGuid();
		ArchivedSessionInfo.SessionName = ConcertServerUtil::GetArchiveName(LiveSessionInfo.SessionName, LiveSessionInfo.Settings);

		if (EventSink->ArchiveSession(*this, Paths->GetSessionWorkingDir(LiveSessionInfo.SessionId), Paths->GetSessionSavedDir(ArchivedSessionInfo.SessionId), ArchivedSessionInfo, FConcertSessionFilter()))
		{
			UE_LOG(LogConcert, Display, TEXT("Deleting %s"), *Paths->GetSessionWorkingDir(LiveSessionInfo.SessionId));
			ConcertUtil::DeleteDirectoryTree(*Paths->GetSessionWorkingDir(LiveSessionInfo.SessionId), *Paths->GetBaseWorkingDir());
			UE_LOG(LogConcert, Display, TEXT("Live session '%s' (%s) was archived on reboot."), *LiveSessionInfo.SessionName, *LiveSessionInfo.SessionId.ToString());
		}
	}
}

FGuid FConcertServer::ArchiveSession(const FGuid& SessionId, const FString& ArchiveNameOverride, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason)
{
	if (GetArchivedSessionIdByName(ArchiveNameOverride).IsValid())
	{
		OutFailureReason = FText::Format(LOCTEXT("Error_ArchiveSession_AlreadyExists", "Archived session '{0}' already exists"), FText::AsCultureInvariant(ArchiveNameOverride));
		return FGuid();
	}

	const FGuid ArchivedSessionId = ArchiveLiveSession(SessionId, ArchiveNameOverride, SessionFilter);
	if (!ArchivedSessionId.IsValid())
	{
		OutFailureReason = LOCTEXT("Error_ArchiveSession_FailedToCopy", "Could not copy session data to the archive");
		return FGuid();
	}

	return ArchivedSessionId;
}

bool FConcertServer::RenameSession(const FGuid& SessionId, const FString& NewName, FText& OutFailureReason)
{
	// NOTE: This function is exposed to the server internals and should not be directly called by connected clients. Clients
	//       send requests (see HandleRenameSessionRequest()). When this function is called, the caller is treated as an 'Admin'.

	FConcertAdmin_RenameSessionRequest Request;
	Request.SessionId = SessionId;
	Request.NewName = NewName;
	Request.UserName = TEXT("Admin");
	Request.DeviceName = FString();
	bool bCheckPermissions = false; // The caller is expected to be a server Admin, bypass permissions.

	FConcertAdmin_RenameSessionResponse Response = RenameSessionInternal(Request, bCheckPermissions);
	OutFailureReason = Response.Reason;
	return Response.ResponseCode == EConcertResponseCode::Success;
}

bool FConcertServer::DestroySession(const FGuid& SessionId, FText& OutFailureReason)
{
	// NOTE: This function is exposed to the server internals and should not be directly called by connected clients. Clients
	//       send requests (see HandleDeleteSessionRequest()). When this function is called, the caller is treated as an 'Admin'.

	FConcertAdmin_DeleteSessionRequest Request;
	Request.SessionId = SessionId;
	Request.UserName = TEXT("Admin");
	Request.DeviceName = FString();
	bool bCheckPermissions = false; // The caller is expected to be a server Admin, bypass permissions.

	FConcertAdmin_DeleteSessionResponse Response = DeleteSessionInternal(Request, bCheckPermissions);
	OutFailureReason = Response.Reason;
	return Response.ResponseCode == EConcertResponseCode::Success;
}

TArray<FConcertSessionClientInfo> FConcertServer::GetSessionClients(const FGuid& SessionId) const
{
	TSharedPtr<IConcertServerSession> ServerSession = GetSession(SessionId);
	if (ServerSession)
	{
		return ServerSession->GetSessionClients();
	}
	return TArray<FConcertSessionClientInfo>();
}

TArray<FConcertSessionInfo> FConcertServer::GetSessionsInfo() const
{
	TArray<FConcertSessionInfo> SessionsInfo;
	SessionsInfo.Reserve(LiveSessions.Num());
	for (auto& SessionPair : LiveSessions)
	{
		SessionsInfo.Add(SessionPair.Value->GetSessionInfo());
	}
	return SessionsInfo;
}

TArray<TSharedPtr<IConcertServerSession>> FConcertServer::GetSessions() const
{
	TArray<TSharedPtr<IConcertServerSession>> SessionsArray;
	SessionsArray.Reserve(LiveSessions.Num());
	for (auto& SessionPair : LiveSessions)
	{
		SessionsArray.Add(SessionPair.Value);
	}
	return SessionsArray;
}

TSharedPtr<IConcertServerSession> FConcertServer::GetSession(const FGuid& SessionId) const
{
	return LiveSessions.FindRef(SessionId);
}

void FConcertServer::HandleDiscoverServersEvent(const FConcertMessageContext& Context)
{
	const FConcertAdmin_DiscoverServersEvent* Message = Context.GetMessage<FConcertAdmin_DiscoverServersEvent>();

	if (ServerAdminEndpoint.IsValid() && Message->RequiredRole == Role && Message->RequiredVersion == VERSION_STRINGIFY(ENGINE_MAJOR_VERSION) TEXT(".") VERSION_STRINGIFY(ENGINE_MINOR_VERSION))
	{
		FConcertAdmin_ServerDiscoveredEvent DiscoveryInfo;
		DiscoveryInfo.ServerName = ServerInfo.ServerName;
		DiscoveryInfo.InstanceInfo = ServerInfo.InstanceInfo;
		DiscoveryInfo.ServerFlags = ServerInfo.ServerFlags;
		ServerAdminEndpoint->SendEvent(DiscoveryInfo, Context.SenderConcertEndpointId);
	}
}

TFuture<FConcertAdmin_SessionInfoResponse> FConcertServer::HandleCreateSessionRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_CreateSessionRequest* Message = Context.GetMessage<FConcertAdmin_CreateSessionRequest>();

	// Create a new server session
	FText CreateFailureReason;
	TSharedPtr<IConcertServerSession> NewServerSession;
	{
		FConcertSessionInfo SessionInfo = CreateSessionInfo();
		SessionInfo.OwnerInstanceId = Message->OwnerClientInfo.InstanceInfo.InstanceId;
		SessionInfo.OwnerUserName = Message->OwnerClientInfo.UserName;
		SessionInfo.OwnerDeviceName = Message->OwnerClientInfo.DeviceName;
		SessionInfo.SessionName = Message->SessionName;
		SessionInfo.Settings = Message->SessionSettings;
		SessionInfo.VersionInfos.Add(Message->VersionInfo);
		NewServerSession = CreateSession(SessionInfo, CreateFailureReason);
	}

	// We have a valid session if it succeeded
	FConcertAdmin_SessionInfoResponse ResponseData;
	if (NewServerSession)
	{
		ResponseData.SessionInfo = NewServerSession->GetSessionInfo();
		ResponseData.ResponseCode = EConcertResponseCode::Success;
	}
	else
	{
		ResponseData.ResponseCode = EConcertResponseCode::Failed;
		ResponseData.Reason = CreateFailureReason;
		UE_LOG(LogConcert, Display, TEXT("Session creation failed. (User: %s, Reason: %s)"), *Message->OwnerClientInfo.UserName, *ResponseData.Reason.ToString());
	}

	return FConcertAdmin_SessionInfoResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_SessionInfoResponse> FConcertServer::HandleFindSessionRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_FindSessionRequest* Message = Context.GetMessage<FConcertAdmin_FindSessionRequest>();

	FConcertAdmin_SessionInfoResponse ResponseData;

	// Find the session requested
	TSharedPtr<IConcertServerSession> ServerSession = GetSession(Message->SessionId);
	const TCHAR* ServerSessionNamePtr = ServerSession ? *ServerSession->GetName() : TEXT("<unknown>");
	if (CanJoinSession(ServerSession, Message->SessionSettings, Message->VersionInfo, &ResponseData.Reason))
	{
		ResponseData.ResponseCode = EConcertResponseCode::Success;
		ResponseData.SessionInfo = ServerSession->GetSessionInfo();
		UE_LOG(LogConcert, Display, TEXT("Allowing user %s to join session %s (Id: %s, Owner: %s)"), *Message->OwnerClientInfo.UserName, ServerSessionNamePtr, *Message->SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName);
	}
	else
	{
		ResponseData.ResponseCode = EConcertResponseCode::Failed;
		UE_LOG(LogConcert, Display, TEXT("Refusing user %s to join session %s (Id: %s, Owner: %s, Reason: %s)"), *Message->OwnerClientInfo.UserName, ServerSessionNamePtr, *Message->SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName, *ResponseData.Reason.ToString());
	}

	return FConcertAdmin_SessionInfoResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_SessionInfoResponse> FConcertServer::HandleRestoreSessionRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_RestoreSessionRequest* Message = Context.GetMessage<FConcertAdmin_RestoreSessionRequest>();

	// Restore the server session
	FText RestoreFailureReason;
	TSharedPtr<IConcertServerSession> NewServerSession;
	{
		FConcertSessionInfo SessionInfo = CreateSessionInfo();
		SessionInfo.OwnerInstanceId = Message->OwnerClientInfo.InstanceInfo.InstanceId;
		SessionInfo.OwnerUserName = Message->OwnerClientInfo.UserName;
		SessionInfo.OwnerDeviceName = Message->OwnerClientInfo.DeviceName;
		SessionInfo.SessionName = Message->SessionName;
		SessionInfo.Settings = Message->SessionSettings;
		SessionInfo.VersionInfos.Add(Message->VersionInfo);
		NewServerSession = RestoreSession(Message->SessionId, SessionInfo, Message->SessionFilter, RestoreFailureReason);
	}

	// We have a valid session if it succeeded
	FConcertAdmin_SessionInfoResponse ResponseData;
	if (NewServerSession)
	{
		ResponseData.SessionInfo = NewServerSession->GetSessionInfo();
		ResponseData.ResponseCode = EConcertResponseCode::Success;
	}
	else
	{
		ResponseData.ResponseCode = EConcertResponseCode::Failed;
		ResponseData.Reason = RestoreFailureReason;
		UE_LOG(LogConcert, Display, TEXT("Session restoration failed. (User: %s, Reason: %s)"), *Message->OwnerClientInfo.UserName, *ResponseData.Reason.ToString());
	}

	return FConcertAdmin_SessionInfoResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_ArchiveSessionResponse> FConcertServer::HandleArchiveSessionRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_ArchiveSessionRequest* Message = Context.GetMessage<FConcertAdmin_ArchiveSessionRequest>();

	FConcertAdmin_ArchiveSessionResponse ResponseData;

	// Find the session requested.
	TSharedPtr<IConcertServerSession> ServerSession = GetSession(Message->SessionId);
	ResponseData.SessionId = Message->SessionId;
	ResponseData.SessionName = ServerSession ? ServerSession->GetName() : TEXT("<unknown>");
	if (ServerSession)
	{
		FText FailureReason;
		const FGuid ArchivedSessionId = ArchiveSession(Message->SessionId, Message->ArchiveNameOverride, Message->SessionFilter, FailureReason);
		if (ArchivedSessionId.IsValid())
		{
			const FConcertSessionInfo& ArchivedSessionInfo = ArchivedSessions.FindChecked(ArchivedSessionId);
			ResponseData.ResponseCode = EConcertResponseCode::Success;
			ResponseData.ArchiveId = ArchivedSessionId;
			ResponseData.ArchiveName = ArchivedSessionInfo.SessionName;
			UE_LOG(LogConcert, Display, TEXT("User %s archived session %s (%s) as %s (%s)"), *Message->UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ResponseData.ArchiveName, *ResponseData.ArchiveId.ToString());
		}
		else
		{
			ResponseData.ResponseCode = EConcertResponseCode::Failed;
			ResponseData.Reason = FailureReason;
			UE_LOG(LogConcert, Display, TEXT("User %s failed to archive session %s (Id: %s, Reason: %s)"), *Message->UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ResponseData.Reason.ToString());
		}
	}
	else
	{
		ResponseData.ResponseCode = EConcertResponseCode::Failed;
		ResponseData.Reason = LOCTEXT("Error_SessionDoesNotExist", "Session does not exist.");
		UE_LOG(LogConcert, Display, TEXT("User %s failed to archive session %s (Id: %s, Reason: %s)"), *Message->UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ResponseData.Reason.ToString());
	}

	return FConcertAdmin_ArchiveSessionResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_RenameSessionResponse> FConcertServer::HandleRenameSessionRequest(const FConcertMessageContext& Context)
{
	return FConcertAdmin_RenameSessionResponse::AsFuture(RenameSessionInternal(*Context.GetMessage<FConcertAdmin_RenameSessionRequest>(), /*bCheckPermission*/true));
}

FConcertAdmin_RenameSessionResponse FConcertServer::RenameSessionInternal(const FConcertAdmin_RenameSessionRequest& Request, bool bCheckPermission)
{
	FConcertAdmin_RenameSessionResponse ResponseData;
	ResponseData.SessionId = Request.SessionId;
	ResponseData.ResponseCode = EConcertResponseCode::Failed;

	if (TSharedPtr<IConcertServerSession> ServerSession = GetSession(Request.SessionId)) // Live session?
	{
		ResponseData.OldName = ServerSession->GetName();

		if (bCheckPermission && !IsRequestFromSessionOwner(ServerSession, Request.UserName, Request.DeviceName)) // Not owner?
		{
			ResponseData.Reason = LOCTEXT("Error_Rename_InvalidPerms_NotOwner", "Not the session owner.");
			UE_LOG(LogConcert, Error, TEXT("User %s failed to rename live session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ServerSession->GetName(), *ResponseData.SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName, *ResponseData.Reason.ToString());
		}
		else if (GetLiveSessionIdByName(Request.NewName).IsValid()) // Name collision?
		{
			ResponseData.Reason = FText::Format(LOCTEXT("Error_Rename_SessionAlreadyExists", "Session '{0}' already exists"),  FText::AsCultureInvariant(Request.NewName));
			UE_LOG(LogConcert, Error, TEXT("User %s failed to rename live session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ServerSession->GetName(), *ResponseData.SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName, *ResponseData.Reason.ToString());
		}
		else
		{
			ServerSession->SetName(Request.NewName);
			EventSink->OnLiveSessionRenamed(*this, ServerSession.ToSharedRef());

			ResponseData.ResponseCode = EConcertResponseCode::Success;
			UE_LOG(LogConcert, Display, TEXT("User %s renamed live session %s from %s to %s"), *Request.UserName, *ResponseData.SessionId.ToString(), *ResponseData.OldName, *ServerSession->GetName());
		}
	}
	else if (FConcertSessionInfo* ArchivedSessionInfo = ArchivedSessions.Find(Request.SessionId)) // Archive session?
	{
		ResponseData.OldName = ArchivedSessionInfo->SessionName;

		if (bCheckPermission && (ArchivedSessionInfo->OwnerUserName != Request.UserName || ArchivedSessionInfo->OwnerDeviceName != Request.DeviceName)) // Not the owner?
		{
			ResponseData.Reason = LOCTEXT("Error_Rename_InvalidPerms_NotOwner", "Not the session owner.");
			UE_LOG(LogConcert, Display, TEXT("User %s failed to rename archived session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ArchivedSessionInfo->SessionName, *ResponseData.SessionId.ToString(), *ArchivedSessionInfo->OwnerUserName, *ResponseData.Reason.ToString());
		}
		else if (GetArchivedSessionIdByName(Request.NewName).IsValid()) // Name collision?
		{
			ResponseData.Reason = FText::Format(LOCTEXT("Error_Rename_ArchiveAlreadyExists", "Archive '{0}' already exists"), FText::AsCultureInvariant(Request.NewName));
			UE_LOG(LogConcert, Error, TEXT("User %s failed to rename archived session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ArchivedSessionInfo->SessionName, *ResponseData.SessionId.ToString(), *ArchivedSessionInfo->OwnerUserName, *ResponseData.Reason.ToString());
		}
		else
		{
			ArchivedSessionInfo->SessionName = Request.NewName;
			EventSink->OnArchivedSessionRenamed(*this, Paths->GetSessionSavedDir(Request.SessionId), *ArchivedSessionInfo);

			ResponseData.ResponseCode = EConcertResponseCode::Success;
			UE_LOG(LogConcert, Display, TEXT("User %s renamed archived session %s from %s to %s"), *Request.UserName, *ResponseData.SessionId.ToString(), *ResponseData.OldName, *Request.NewName);
		}
	}
	else // Not found?
	{
		ResponseData.Reason = LOCTEXT("Error_Rename_DoesNotExist", "Session does not exist.");
		UE_LOG(LogConcert, Display, TEXT("User %s failed to rename session (Id: %s, Reason: %s)"), *Request.UserName, *ResponseData.SessionId.ToString(), *ResponseData.Reason.ToString());
	}

	return ResponseData;
}

TFuture<FConcertAdmin_DeleteSessionResponse> FConcertServer::HandleDeleteSessionRequest(const FConcertMessageContext & Context)
{
	return FConcertAdmin_DeleteSessionResponse::AsFuture(DeleteSessionInternal(*Context.GetMessage<FConcertAdmin_DeleteSessionRequest>(), /*bCheckPermission*/true));
}

FConcertAdmin_DeleteSessionResponse FConcertServer::DeleteSessionInternal(const FConcertAdmin_DeleteSessionRequest& Request, bool bCheckPermission)
{
	FConcertAdmin_DeleteSessionResponse ResponseData;
	ResponseData.SessionId = Request.SessionId;
	ResponseData.ResponseCode = EConcertResponseCode::Failed;

	if (TSharedPtr<IConcertServerSession> ServerSession = GetSession(Request.SessionId)) // Live session?
	{
		ResponseData.SessionName = ServerSession->GetName();

		if (bCheckPermission && !IsRequestFromSessionOwner(ServerSession, Request.UserName, Request.DeviceName))
		{
			ResponseData.Reason = LOCTEXT("Error_Delete_InvalidPerms_NotOwner", "Not the session owner.");
			UE_LOG(LogConcert, Display, TEXT("User %s failed to delete live session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName, *ResponseData.Reason.ToString());
		}
		else if (!DestroyLiveSession(Request.SessionId, /*bDeleteSessionData*/true))
		{
			ResponseData.Reason = LOCTEXT("Error_Delete_SessionFailedToDestroy", "Failed to destroy session.");
			UE_LOG(LogConcert, Display, TEXT("User %s failed to delete live session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ServerSession->GetSessionInfo().OwnerUserName, *ResponseData.Reason.ToString());
		}
		else // Succeeded to delete the session.
		{
			ResponseData.ResponseCode = EConcertResponseCode::Success;
			UE_LOG(LogConcert, Display, TEXT("User %s deleted live session %s (%s)"), *Request.UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString());
		}
	}
	else if (const FConcertSessionInfo* ArchivedSessionInfo = ArchivedSessions.Find(Request.SessionId)) // Archived session?
	{
		ResponseData.SessionName = ArchivedSessionInfo->SessionName;

		if (bCheckPermission && (ArchivedSessionInfo->OwnerUserName != Request.UserName || ArchivedSessionInfo->OwnerDeviceName != Request.DeviceName)) // Not the owner?
		{
			ResponseData.Reason = LOCTEXT("Error_Delete_InvalidPerms_NotOwner", "Not the session owner.");
			UE_LOG(LogConcert, Display, TEXT("User %s failed to delete archived session '%s' (Id: %s, Owner: %s, Reason: %s)"), *Request.UserName, *ArchivedSessionInfo->SessionName, *ResponseData.SessionId.ToString(), *ArchivedSessionInfo->OwnerUserName, *ResponseData.Reason.ToString());
		}
		else if (!DestroyArchivedSession(Request.SessionId, /*bDeleteSessionData*/true))
		{
			ResponseData.Reason = LOCTEXT("Error_Delete_SessionFailedToDestroy", "Failed to destroy session.");
			UE_LOG(LogConcert, Display, TEXT("User %s failed to delete archived session '%s' (Id: %s, Reason: %s)"), *Request.UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString(), *ResponseData.Reason.ToString());
		}
		else // Succeeded to delete the session.
		{
			ResponseData.ResponseCode = EConcertResponseCode::Success;
			UE_LOG(LogConcert, Display, TEXT("User %s deleted archived session %s (%s)"), *Request.UserName, *ResponseData.SessionName, *ResponseData.SessionId.ToString());
		}
	}
	else // Not found?
	{
		ResponseData.Reason = LOCTEXT("Error_Delete_SessionDoesNotExist", "Session does not exist.");
		UE_LOG(LogConcert, Display, TEXT("User %s failed to delete session (Id: %s, Reason: %s)"), *Request.UserName, *ResponseData.SessionId.ToString(), *ResponseData.Reason.ToString());
	}

	return ResponseData;
}

TFuture<FConcertAdmin_GetAllSessionsResponse> FConcertServer::HandleGetAllSessionsRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_GetAllSessionsRequest* Message = Context.GetMessage<FConcertAdmin_GetAllSessionsRequest>();

	FConcertAdmin_GetAllSessionsResponse ResponseData;
	ResponseData.LiveSessions = GetSessionsInfo();
	for (const auto& ArchivedSessionPair : ArchivedSessions)
	{
		ResponseData.ArchivedSessions.Add(ArchivedSessionPair.Value);
	}

	return FConcertAdmin_GetAllSessionsResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_GetSessionsResponse> FConcertServer::HandleGetLiveSessionsRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_GetLiveSessionsRequest* Message = Context.GetMessage<FConcertAdmin_GetLiveSessionsRequest>();

	FConcertAdmin_GetSessionsResponse ResponseData;
	ResponseData.Sessions = GetSessionsInfo();
	
	return FConcertAdmin_GetSessionsResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_GetSessionsResponse> FConcertServer::HandleGetArchivedSessionsRequest(const FConcertMessageContext& Context)
{
	FConcertAdmin_GetSessionsResponse ResponseData;

	ResponseData.ResponseCode = EConcertResponseCode::Success;
	for (const auto& ArchivedSessionPair : ArchivedSessions)
	{
		ResponseData.Sessions.Add(ArchivedSessionPair.Value);
	}

	return FConcertAdmin_GetSessionsResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_GetSessionClientsResponse> FConcertServer::HandleGetSessionClientsRequest(const FConcertMessageContext& Context)
{
	const FConcertAdmin_GetSessionClientsRequest* Message = Context.GetMessage<FConcertAdmin_GetSessionClientsRequest>();

	FConcertAdmin_GetSessionClientsResponse ResponseData;
	ResponseData.SessionClients = GetSessionClients(Message->SessionId);
	
	return FConcertAdmin_GetSessionClientsResponse::AsFuture(MoveTemp(ResponseData));
}

TFuture<FConcertAdmin_GetSessionActivitiesResponse> FConcertServer::HandleGetSessionActivitiesRequest(const FConcertMessageContext& Context)
{
	FConcertAdmin_GetSessionActivitiesResponse ResponseData;

	const FConcertAdmin_GetSessionActivitiesRequest* Message = Context.GetMessage<FConcertAdmin_GetSessionActivitiesRequest>();
	if (EventSink->GetSessionActivities(*this, Message->SessionId, Message->FromActivityId, Message->ActivityCount, ResponseData.Activities))
	{
		ResponseData.ResponseCode = EConcertResponseCode::Success;
	}
	else // The only reason to get here is when the session is not found.
	{
		ResponseData.ResponseCode = EConcertResponseCode::Failed;
		ResponseData.Reason = LOCTEXT("Error_SessionActivities_SessionDoesNotExist", "Session does not exist.");
		UE_LOG(LogConcert, Display, TEXT("Failed to fetch activities from session (Id: %s, Reason: %s)"), *Message->SessionId.ToString(), *ResponseData.Reason.ToString());
	}

	return FConcertAdmin_GetSessionActivitiesResponse::AsFuture(MoveTemp(ResponseData));
}

bool FConcertServer::CanJoinSession(const TSharedPtr<IConcertServerSession>& ServerSession, const FConcertSessionSettings& SessionSettings, const FConcertSessionVersionInfo& SessionVersionInfo, FText* OutFailureReason)
{
	if (!ServerSession)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = LOCTEXT("Error_CanJoinSession_UnknownSession", "Unknown session");
		}
		return false;
	}

	if (Settings->ServerSettings.bIgnoreSessionSettingsRestriction)
	{
		return true;
	}

	if (!ServerSession->GetSessionInfo().Settings.ValidateRequirements(SessionSettings, OutFailureReason))
	{
		return false;
	}

	if (ServerSession->GetSessionInfo().VersionInfos.Num() > 0 && !ServerSession->GetSessionInfo().VersionInfos.Last().Validate(SessionVersionInfo, EConcertVersionValidationMode::Identical, OutFailureReason))
	{
		return false;
	}

	return true;
}

bool FConcertServer::IsRequestFromSessionOwner(const TSharedPtr<IConcertServerSession>& SessionToDelete, const FString& FromUserName, const FString& FromDeviceName)
{
	if (SessionToDelete)
	{
		const FConcertSessionInfo& SessionInfo = SessionToDelete->GetSessionInfo();
		return SessionInfo.OwnerUserName == FromUserName && SessionInfo.OwnerDeviceName == FromDeviceName;
	}
	return false;
}

TSharedPtr<IConcertServerSession> FConcertServer::CreateLiveSession(const FConcertSessionInfo& SessionInfo)
{
	check(SessionInfo.SessionId.IsValid() && !SessionInfo.SessionName.IsEmpty());
	check(!LiveSessions.Contains(SessionInfo.SessionId) && !GetLiveSessionIdByName(SessionInfo.SessionName).IsValid());

	// Strip version info when using -CONCERTIGNORE
	FConcertSessionInfo LiveSessionInfo = SessionInfo;
	if (Settings->ServerSettings.bIgnoreSessionSettingsRestriction)
	{
		UE_CLOG(LiveSessionInfo.VersionInfos.Num() > 0, LogConcert, Warning, TEXT("Clearing version information when creating session '%s' due to -CONCERTIGNORE. This session will be unversioned!"), *LiveSessionInfo.SessionName);
		LiveSessionInfo.VersionInfos.Reset();
	}

	TSharedPtr<FConcertServerSession> LiveSession = MakeShared<FConcertServerSession>(
		LiveSessionInfo,
		Settings->ServerSettings,
		EndpointProvider->CreateLocalEndpoint(LiveSessionInfo.SessionName, Settings->EndpointSettings, &FConcertLogger::CreateLogger),
		Paths->GetSessionWorkingDir(LiveSessionInfo.SessionId)
		);

	LiveSessions.Add(LiveSessionInfo.SessionId, LiveSession);
	EventSink->OnLiveSessionCreated(*this, LiveSession.ToSharedRef());
	LiveSession->Startup();

	return LiveSession;
}

bool FConcertServer::DestroyLiveSession(const FGuid& LiveSessionId, const bool bDeleteSessionData)
{
	TSharedPtr<IConcertServerSession> LiveSession = LiveSessions.FindRef(LiveSessionId);
	if (LiveSession)
	{
		EventSink->OnLiveSessionDestroyed(*this, LiveSession.ToSharedRef());
		LiveSession->Shutdown();
		LiveSessions.Remove(LiveSessionId);

		if (bDeleteSessionData)
		{
			ConcertUtil::DeleteDirectoryTree(*Paths->GetSessionWorkingDir(LiveSessionId), *Paths->GetBaseWorkingDir());
		}

		return true;
	}

	return false;
}

FGuid FConcertServer::ArchiveLiveSession(const FGuid& LiveSessionId, const FString& ArchivedSessionNameOverride, const FConcertSessionFilter& SessionFilter)
{
	TSharedPtr<IConcertServerSession> LiveSession = LiveSessions.FindRef(LiveSessionId);
	if (LiveSession)
	{
		FString ArchivedSessionName = ArchivedSessionNameOverride;
		if (ArchivedSessionName.IsEmpty())
		{
			ArchivedSessionName = ConcertServerUtil::GetArchiveName(*LiveSession->GetName(), LiveSession->GetSessionInfo().Settings);
		}
		{
			const FGuid ArchivedSessionId = GetArchivedSessionIdByName(ArchivedSessionName);
			DestroyArchivedSession(ArchivedSessionId, /*bDeleteSessionData*/true);
		}

		FConcertSessionInfo ArchivedSessionInfo = LiveSession->GetSessionInfo();
		ArchivedSessionInfo.SessionId = FGuid::NewGuid();
		ArchivedSessionInfo.SessionName = MoveTemp(ArchivedSessionName);
		if (EventSink->ArchiveSession(*this, LiveSession.ToSharedRef(), Paths->GetSessionSavedDir(ArchivedSessionInfo.SessionId), ArchivedSessionInfo, SessionFilter))
		{
			UE_LOG(LogConcert, Display, TEXT("Live session '%s' (%s) was archived as '%s' (%s)"), *LiveSession->GetName(), *LiveSession->GetId().ToString(), *ArchivedSessionInfo.SessionName, *ArchivedSessionInfo.SessionId.ToString());
			if (CreateArchivedSession(ArchivedSessionInfo))
			{
				return ArchivedSessionInfo.SessionId;
			}
		}
	}

	return FGuid();
}

bool FConcertServer::CreateArchivedSession(const FConcertSessionInfo& SessionInfo)
{
	check(SessionInfo.SessionId.IsValid() && !SessionInfo.SessionName.IsEmpty());
	check(!ArchivedSessions.Contains(SessionInfo.SessionId) && !GetArchivedSessionIdByName(SessionInfo.SessionName).IsValid());

	ArchivedSessions.Add(SessionInfo.SessionId, SessionInfo);
	EventSink->OnArchivedSessionCreated(*this, Paths->GetSessionSavedDir(SessionInfo.SessionId), SessionInfo);

	return true;
}

bool FConcertServer::DestroyArchivedSession(const FGuid& ArchivedSessionId, const bool bDeleteSessionData)
{
	if (ArchivedSessions.Contains(ArchivedSessionId))
	{
		EventSink->OnArchivedSessionDestroyed(*this, ArchivedSessionId);
		ArchivedSessions.Remove(ArchivedSessionId);

		if (bDeleteSessionData)
		{
			ConcertUtil::DeleteDirectoryTree(*Paths->GetSessionSavedDir(ArchivedSessionId), *Paths->GetBaseSavedDir());
		}

		return true;
	}

	return false;
}

TSharedPtr<IConcertServerSession> FConcertServer::RestoreArchivedSession(const FGuid& ArchivedSessionId, const FConcertSessionInfo& NewSessionInfo, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason)
{
	check(NewSessionInfo.SessionId.IsValid());

	if (const FConcertSessionInfo* ArchivedSessionInfo = ArchivedSessions.Find(ArchivedSessionId))
	{
		FString LiveSessionName = NewSessionInfo.SessionName;
		if (LiveSessionName.IsEmpty())
		{
			LiveSessionName = ArchivedSessionInfo->SessionName;
		}
		{
			const FGuid LiveSessionId = GetLiveSessionIdByName(LiveSessionName);
			DestroyLiveSession(LiveSessionId, /*bDeleteSessionData*/true);
		}

		FConcertSessionInfo LiveSessionInfo = NewSessionInfo;
		LiveSessionInfo.SessionName = MoveTemp(LiveSessionName);
		LiveSessionInfo.VersionInfos = ArchivedSessionInfo->VersionInfos;

		// Ensure the new version is compatible with the old version, and append this new version if it is different to the last used version
		// Note: Older archived sessions didn't used to have any version info stored for them, and the version info may be missing completely when using -CONCERTIGNORE
		if (Settings->ServerSettings.bIgnoreSessionSettingsRestriction)
		{
			UE_CLOG(LiveSessionInfo.VersionInfos.Num() > 0, LogConcert, Warning, TEXT("Clearing version information when restoring session '%s' due to -CONCERTIGNORE. This may lead to instability and crashes!"), *NewSessionInfo.SessionName);
			LiveSessionInfo.VersionInfos.Reset();
		}
		else if (NewSessionInfo.VersionInfos.Num() > 0)
		{
			check(NewSessionInfo.VersionInfos.Num() == 1);
			const FConcertSessionVersionInfo& NewVersionInfo = NewSessionInfo.VersionInfos[0];

			if (LiveSessionInfo.VersionInfos.Num() > 0)
			{
				if (!LiveSessionInfo.VersionInfos.Last().Validate(NewVersionInfo, EConcertVersionValidationMode::Compatible, &OutFailureReason))
				{
					UE_LOG(LogConcert, Error, TEXT("An attempt to restore session '%s' was rejected due to a versioning incompatibility: %s"), *NewSessionInfo.SessionName, *OutFailureReason.ToString());
					return nullptr;
				}

				if (!LiveSessionInfo.VersionInfos.Last().Validate(NewVersionInfo, EConcertVersionValidationMode::Identical))
				{
					LiveSessionInfo.VersionInfos.Add(NewVersionInfo);
				}
			}
			else
			{
				LiveSessionInfo.VersionInfos.Add(NewVersionInfo);
			}
		}

		if (EventSink->RestoreSession(*this, ArchivedSessionId, Paths->GetSessionWorkingDir(LiveSessionInfo.SessionId), LiveSessionInfo, SessionFilter))
		{
			UE_LOG(LogConcert, Display, TEXT("Archived session '%s' (%s) was restored as '%s' (%s)"), *ArchivedSessionInfo->SessionName, *ArchivedSessionInfo->SessionId.ToString(), *LiveSessionInfo.SessionName, *LiveSessionInfo.SessionId.ToString());
			return CreateLiveSession(LiveSessionInfo);
		}
	}

	OutFailureReason = LOCTEXT("Error_RestoreSession_FailedToCopy", "Could not copy session data from the archive");
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
