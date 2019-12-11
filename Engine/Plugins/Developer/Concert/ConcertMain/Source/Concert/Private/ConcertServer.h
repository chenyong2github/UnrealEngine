// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertServer.h"
#include "IConcertTransportModule.h"
#include "ConcertSettings.h"
#include "ConcertServerSessionRepositories.h"
#include "UObject/StrongObjectPtr.h"

class FConcertServerSession;
class IConcertServerEventSink;

/** Implements Concert interface */
class FConcertServer : public IConcertServer
{
public: 
	FConcertServer(const FString& InRole, const FConcertSessionFilter& InAutoArchiveSessionFilter, IConcertServerEventSink* InEventSink, const TSharedPtr<IConcertEndpointProvider>& InEndpointProvider);
	virtual ~FConcertServer();

	virtual const FString& GetRole() const override;

	virtual void Configure(const UConcertServerConfig* InSettings) override;
	virtual bool IsConfigured() const override;
	virtual const UConcertServerConfig* GetConfiguration() const override;
	virtual const FConcertServerInfo& GetServerInfo() const override;

	virtual bool IsStarted() const override;
	virtual void Startup() override;
	virtual void Shutdown() override;

	virtual FGuid GetLiveSessionIdByName(const FString& InName) const override;
	virtual FGuid GetArchivedSessionIdByName(const FString& InName) const override;

	virtual FConcertSessionInfo CreateSessionInfo() const override;
	virtual TArray<FConcertSessionInfo> GetSessionsInfo() const override;
	virtual TArray<TSharedPtr<IConcertServerSession>> GetSessions() const override;
	virtual TSharedPtr<IConcertServerSession> GetSession(const FGuid& SessionId) const override;
	virtual TSharedPtr<IConcertServerSession> CreateSession(const FConcertSessionInfo& SessionInfo, FText& OutFailureReason) override;
	virtual TSharedPtr<IConcertServerSession> RestoreSession(const FGuid& SessionId, const FConcertSessionInfo& SessionInfo, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason) override;
	virtual FGuid ArchiveSession(const FGuid& SessionId, const FString& ArchiveNameOverride, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason) override;
	virtual bool ExportSession(const FGuid& SessionId, const FConcertSessionFilter& SessionFilter, const FString& DestDir, bool bAnonymizeData, FText& OutFailureReason) override;
	virtual bool RenameSession(const FGuid& SessionId, const FString& NewName, FText& OutFailureReason) override;
	virtual bool DestroySession(const FGuid& SessionId, FText& OutFailureReason) override;
	virtual TArray<FConcertSessionClientInfo> GetSessionClients(const FGuid& SessionId) const override;

private:
	/** Returns the root dir where the servers keeps the its internally created repositories (When the caller doesn't provide the paths). */
	const FString& GetSessionRepositoriesRootDir() const;

	const FConcertServerSessionRepository& GetSessionRepository(const FGuid& SessionId) const;
	FString GetSessionSavedDir(const FGuid& SessionId) const;
	FString GetSessionWorkingDir(const FGuid& SessionId) const;

	EConcertSessionRepositoryMountResponseCode MountSessionRepository(FConcertServerSessionRepository& Repository, bool bCreateIfNotExist, bool bCleanWorkingDir, bool bCleanExpiredSessions, bool bSearchByPaths);
	bool UnmountSessionRepository(FConcertServerSessionRepository& InOutRepository, bool bDropped);

	/**  */
	void HandleDiscoverServersEvent(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_MountSessionRepositoryResponse> HandleMountSessionRepositoryRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_GetSessionRepositoriesResponse> HandleGetSessionRepositoriesRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_DropSessionRepositoriesResponse> HandleDropSessionRepositoriesRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_SessionInfoResponse> HandleCreateSessionRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_SessionInfoResponse> HandleFindSessionRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_SessionInfoResponse> HandleRestoreSessionRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_ArchiveSessionResponse> HandleArchiveSessionRequest(const FConcertMessageContext& Context);

	/**  */
	TFuture<FConcertAdmin_RenameSessionResponse> HandleRenameSessionRequest(const FConcertMessageContext& Context);

	/**  */
	FConcertAdmin_RenameSessionResponse RenameSessionInternal(const FConcertAdmin_RenameSessionRequest& Request, bool bCheckPermission);

	/**  */
	TFuture<FConcertAdmin_DeleteSessionResponse> HandleDeleteSessionRequest(const FConcertMessageContext& Context);

	/**  */
	FConcertAdmin_DeleteSessionResponse DeleteSessionInternal(const FConcertAdmin_DeleteSessionRequest& Request, bool bCheckPermission);

	/**  */
	TFuture<FConcertAdmin_GetAllSessionsResponse> HandleGetAllSessionsRequest(const FConcertMessageContext& Context);

	/** */
	TFuture<FConcertAdmin_GetSessionsResponse> HandleGetLiveSessionsRequest(const FConcertMessageContext& Context);

	/** */
	TFuture<FConcertAdmin_GetSessionsResponse> HandleGetArchivedSessionsRequest(const FConcertMessageContext& Context);

	/** */
	TFuture<FConcertAdmin_GetSessionClientsResponse> HandleGetSessionClientsRequest(const FConcertMessageContext& Context);

	/** */
	TFuture<FConcertAdmin_GetSessionActivitiesResponse> HandleGetSessionActivitiesRequest(const FConcertMessageContext& Context);

	/** Recover the sessions found in the working directory into live session, build the list of archived sessions and rotate them, keeping only the N most recent. */
	void RecoverSessions(const FConcertServerSessionRepository& InRepository, bool bCleanupExpiredSessions);

	/**
	 * Migrate the live sessions from the working directory (before sessions being recovered into live one) to the archived directory.
	 * Expected to happen at start up, before RecoverSessions(), if UConcertServerConfig::bAutoArchiveOnReboot is true.
	 */
	void ArchiveOfflineSessions(const FConcertServerSessionRepository& InRepository);

	/** */
	bool CanJoinSession(const TSharedPtr<IConcertServerSession>& ServerSession, const FConcertSessionSettings& SessionSettings, const FConcertSessionVersionInfo& SessionVersionInfo, FText* OutFailureReason = nullptr);

	/** 
	 * Validate that the request come form the owner of the session that he want to delete/rename, etc.
	 */
	bool IsRequestFromSessionOwner(const TSharedPtr<IConcertServerSession>& Session, const FString& FromUserName, const FString& FromDeviceName);

	/**  */
	TSharedPtr<IConcertServerSession> CreateLiveSession(const FConcertSessionInfo& SessionInfo, const FConcertServerSessionRepository& InRepository);

	/**  */
	bool DestroyLiveSession(const FGuid& LiveSessionId, const bool bDeleteSessionData);

	/**  */
	FGuid ArchiveLiveSession(const FGuid& LiveSessionId, const FString& ArchivedSessionNameOverride, const FConcertSessionFilter& SessionFilter);

	/**  */
	bool CreateArchivedSession(const FConcertSessionInfo& SessionInfo);

	/**  */
	bool DestroyArchivedSession(const FGuid& ArchivedSessionId, const bool bDeleteSessionData);

	/**  */
	TSharedPtr<IConcertServerSession> RestoreArchivedSession(const FGuid& ArchivedSessionId, const FConcertSessionInfo& NewSessionInfo, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason);

	/** The role of this server (eg, MultiUser, DisasterRecovery, etc) */
	FString Role;

	/** All session repositories used by this server. */
	TArray<FConcertServerSessionRepository> MountedSessionRepositories;

	/** The server default repository created from the server settings. */
	TOptional<FConcertServerSessionRepository> DefaultSessionRepository;

	/** The session filter to apply when auto-archiving sessions */
	FConcertSessionFilter AutoArchiveSessionFilter;

	/** Sink functions for events that this server can emit */
	IConcertServerEventSink* EventSink;

	/** Factory for creating Endpoint */
	TSharedPtr<IConcertEndpointProvider> EndpointProvider;
	
	/** Administration endpoint for the server (i.e. creating, joining sessions) */
	TSharedPtr<IConcertLocalEndpoint> ServerAdminEndpoint;
	
	/** Server and Instance Info */
	FConcertServerInfo ServerInfo;

	/** Map of Live Sessions */
	TMap<FGuid, TSharedPtr<FConcertServerSession>> LiveSessions;

	/** Map of Archived Sessions */
	TMap<FGuid, FConcertSessionInfo> ArchivedSessions;

	/** Server settings object we were configured with */
	TStrongObjectPtr<const UConcertServerConfig> Settings;

	/** The root directory containing the session repositories. */
	FString SessionRepositoryRootDir;
};
