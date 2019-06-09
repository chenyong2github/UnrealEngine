// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertServer.h"
#include "IConcertTransportModule.h"
#include "ConcertSettings.h"

#include "UObject/StrongObjectPtr.h"

class FConcertServerSession;
class IConcertServerEventSink;

class FConcertServerPaths
{
public:
	/**
	 * Constructs paths usable by the server.
	 * @param InRole The context in which the server exist (Disaster Recovery, MultiUsers, etc).
	 * @param InBaseWorkingDir The base directory path where the live session data will be stored. If empty, use a default one.
	 * @param InBaseSavedDir The base directory path where the archived session will be stored. If empty, use a default one.
	 */
	explicit FConcertServerPaths(const FString& InRole, const FString& InBaseWorkingDir, const FString& InBaseSavedDir);

	/** Get the working directory. This is were the live sessions store their files */
	const FString& GetWorkingDir() const
	{
		return WorkingDir;
	}

	/** Return the working directory for a specific session */
	FString GetSessionWorkingDir(const FGuid& InSessionId) const
	{
		return WorkingDir / InSessionId.ToString();
	}

	/** Get the saved directory. This is were the archived sessions store their files */
	const FString& GetSavedDir() const
	{
		return SavedDir;
	}

	/** Return the saved directory for a specific session */
	FString GetSessionSavedDir(const FGuid& InSessionId) const
	{
		return SavedDir / InSessionId.ToString();
	}

	/** Returns the 'base' working directory as passed to the constructor.*/
	FString GetBaseWorkingDir() const
	{
		return BaseWorkingDir;
	}

	/** Returns the 'base' saved directory as passed to the constructor.*/
	FString GetBaseSavedDir() const
	{
		return BaseSavedDir;
	}

private:
	/** Get the working directory (BaseWorkingDir/Concert/Role). This is were the active sessions store their files */
	const FString WorkingDir;

	/** Get the directory where the sessions are saved (BaseSavedDir/Concert/Role). */
	const FString SavedDir;

	/** The base working directory as passed to the constructor. */
	const FString BaseWorkingDir;

	/** The base saved directory as passed to the constructor. */
	const FString BaseSavedDir;
};

/** Implements Concert interface */
class FConcertServer : public IConcertServer
{
public: 
	FConcertServer(const FString& InRole, IConcertServerEventSink* InEventSink, const TSharedPtr<IConcertEndpointProvider>& InEndpointProvider);
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
	virtual bool RenameSession(const FGuid& SessionId, const FString& NewName, FText& OutFailureReason) override;
	virtual bool DestroySession(const FGuid& SessionId, FText& OutFailureReason) override;
	virtual TArray<FConcertSessionClientInfo> GetSessionClients(const FGuid& SessionId) const override;

private:
	/**  */
	void HandleDiscoverServersEvent(const FConcertMessageContext& Context);

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
	void RecoverSessions();

	/**
	 * Migrate the live sessions from the working directory (before sessions being recovered into live one) to the archived directory.
	 * Expected to happen at start up, before RecoverSessions(), if UConcertServerConfig::bAutoArchiveOnReboot is true.
	 */
	void ArchiveOfflineSessions();

	/** */
	bool CanJoinSession(const TSharedPtr<IConcertServerSession>& ServerSession, const FConcertSessionSettings& SessionSettings, const FConcertSessionVersionInfo& SessionVersionInfo, FText* OutFailureReason = nullptr);

	/** 
	 * Validate that the request come form the owner of the session that he want to delete/rename, etc.
	 */
	bool IsRequestFromSessionOwner(const TSharedPtr<IConcertServerSession>& Session, const FString& FromUserName, const FString& FromDeviceName);

	/**  */
	TSharedPtr<IConcertServerSession> CreateLiveSession(const FConcertSessionInfo& SessionInfo);

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

	/** Cached root paths used by this server */
	TUniquePtr<const FConcertServerPaths> Paths;

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
};
