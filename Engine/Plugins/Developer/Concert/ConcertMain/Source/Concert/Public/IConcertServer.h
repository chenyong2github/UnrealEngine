// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertSession.h"

class UConcertServerConfig;

/** Interface for Concert server */
class IConcertServer
{
public:
	virtual ~IConcertServer() = default;

	/**
	 * Get the role of this server (eg, MultiUser, DisasterRecovery, etc)
	 */
	virtual const FString& GetRole() const = 0;

	/**
	 * Configure the Concert settings and its information
	 */
	virtual void Configure(const UConcertServerConfig* ServerConfig) = 0;
	
	/** 
	 * Return true if the server has been configured.
	 */
	virtual bool IsConfigured() const = 0;

	/**
	 * Return The configuration of this server, or null if it hasn't been configured.
	 */
	virtual const UConcertServerConfig* GetConfiguration() const = 0;

	/**
	 * Get the server information set by Configure
	 */
	virtual const FConcertServerInfo& GetServerInfo() const = 0;

	/**
	 *	Returns if the server has already been started up.
	 */
	virtual bool IsStarted() const = 0;

	/**
	 * Startup the server, this can be called multiple time
	 * Configure needs to be called before startup
	 * @return true if the server was properly started or already was
	 */
	virtual void Startup() = 0;

	/**
	 * Shutdown the server, this can be called multiple time with no ill effect.
 	 * However it depends on the UObject system so need to be called before its exit.
	 */
	virtual void Shutdown() = 0;

	/**
	 * Get the ID of a live session from its name.
	 * @return The ID of the session, or an invalid GUID if it couldn't be found.
	 */
	virtual FGuid GetLiveSessionIdByName(const FString& InName) const = 0;

	/**
	 * Get the ID of an archived session from its name.
	 * @return The ID of the session, or an invalid GUID if it couldn't be found.
	 */
	virtual FGuid GetArchivedSessionIdByName(const FString& InName) const = 0;

	/**
	 * Create a session description for this server
	 */
	virtual FConcertSessionInfo CreateSessionInfo() const = 0;

	/**
	 * Get the sessions information list
	 */
	virtual	TArray<FConcertSessionInfo> GetSessionsInfo() const = 0;

	/**
	 * Get all server sessions
	 * @return array of server sessions
	 */
	virtual TArray<TSharedPtr<IConcertServerSession>> GetSessions() const = 0;

	/**
	 * Get a server session
	 * @param SessionId The ID of the session we want
	 * @return the server session or an invalid pointer if no session was found
	 */
	virtual TSharedPtr<IConcertServerSession> GetSession(const FGuid& SessionId) const = 0;

	/** 
	 * Create a new Concert server session based on the passed session info
	 * @param SessionInfo The information about the session to create.
	 * @param OutFailureReason The reason the operation fails if the function returns false, undefined otherwise.
	 * @return the created server session
	 */
	virtual TSharedPtr<IConcertServerSession> CreateSession(const FConcertSessionInfo& SessionInfo, FText& OutFailureReason) = 0;

	/**
	 * Restore an archived Concert server session based on the passed session info
	 * @param SessionId The ID of the session to restore
	 * @param SessionInfo The information about the session to create from the archive.
	 * @param SessionFilter The filter controlling which activities from the session should be restored.
	 * @param OutFailureReason The reason the operation fails if the function returns false, undefined otherwise.
	 * @return the restored server session
	 */
	virtual TSharedPtr<IConcertServerSession> RestoreSession(const FGuid& SessionId, const FConcertSessionInfo& SessionInfo, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason) = 0;

	/**
	 * Archive a Concert session on the server.
	 * @param SessionId The ID of the session to archive
	 * @param ArchiveNameOverride The name override to give to the archived session.
	 * @param SessionFilter The filter controlling which activities from the session should be archived.
	 * @param OutFailureReason The reason the operation fails if the function returns false, undefined otherwise.
	 * @return The ID of the archived session on success, or an invalid GUID otherwise.
	 */
	virtual FGuid ArchiveSession(const FGuid& SessionId, const FString& ArchiveNameOverride, const FConcertSessionFilter& SessionFilter, FText& OutFailureReason) = 0;

	/**
	 * Rename a live or archived Concert session on the server. The server automatically detects if the specified session Id is a live or an
	 * archived session.
	 * @param SessionId The ID of the session to rename
	 * @param NewName The new session name.
	 * @param OutFailureReason The reason the operation fails if the function returns false, undefined otherwise.
	 * @return True if the session was renamed.
	 */
	virtual bool RenameSession(const FGuid& SessionId, const FString& NewName, FText& OutFailureReason) = 0;

	/**
	 * Destroy a live or archived Concert server session. The server automatically detects if the specified session Id is a live or an
	 * archived session.
	 * @param SessionId The name of the session to destroy
	 * @param OutFailureReason The reason the operation fails if the function returns false, undefined otherwise.
	 * @return true if the session was found and destroyed
	 */
	virtual bool DestroySession(const FGuid& SessionId, FText& OutFailureReason) = 0;

	/**
	 * Get the list of clients for a session
	 * @param SessionId The session ID
	 * @return A list of clients connected to the session
	 */
	virtual TArray<FConcertSessionClientInfo> GetSessionClients(const FGuid& SessionId) const = 0;
};
