// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IConcertSyncServer.h"
#include "IConcertServerEventSink.h"

class IConcertServerSession;
class FConcertServerWorkspace;
class FConcertServerSequencerManager;
class FConcertSyncServerLiveSession;
class FConcertSyncServerArchivedSession;
class FConcertSyncSessionDatabase;

struct FConcertSessionFilter;

/**
 * Implementation for a Concert Sync Server.
 */
class FConcertSyncServer : public IConcertSyncServer, public IConcertServerEventSink
{
public:
	FConcertSyncServer(const FString& InRole, const FConcertSessionFilter& InAutoArchiveSessionFilter);
	virtual ~FConcertSyncServer();

	//~ IConcertSyncServer interface
	virtual void Startup(const UConcertServerConfig* InServerConfig, const EConcertSyncSessionFlags InSessionFlags) override;
	virtual void Shutdown() override;
	virtual IConcertServerRef GetConcertServer() const override;

	//~ IConcertServerEventSink interface
	virtual void GetSessionsFromPath(const IConcertServer& InServer, const FString& InPath, TArray<FConcertSessionInfo>& OutSessionInfos, TArray<FDateTime>* OutSessionCreationTimes = nullptr) override;
	virtual void OnLiveSessionCreated(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) override;
	virtual void OnLiveSessionDestroyed(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) override;
	virtual void OnArchivedSessionCreated(const IConcertServer& InServer, const FString& InArchivedSessionRoot, const FConcertSessionInfo& InArchivedSessionInfo) override;
	virtual void OnArchivedSessionDestroyed(const IConcertServer& InServer, const FGuid& InArchivedSessionId) override;
	virtual bool ArchiveSession(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession, const FString& InArchivedSessionRoot, const FConcertSessionInfo& InArchivedSessionInfo, const FConcertSessionFilter& InSessionFilter) override;
	virtual bool ArchiveSession(const IConcertServer& InServer, const FString& InLiveSessionWorkingDir, const FString& InArchivedSessionRoot, const FConcertSessionInfo& InArchivedSessionInfo, const FConcertSessionFilter& InSessionFilter) override;
	virtual bool ExportSession(const IConcertServer& InServer, const FGuid& InSessionId, const FString& DestDir, const FConcertSessionFilter& InSessionFilter, bool bAnonymizeData) override;
	virtual bool RestoreSession(const IConcertServer& InServer, const FGuid& InArchivedSessionId, const FString& InLiveSessionRoot, const FConcertSessionInfo& InLiveSessionInfo, const FConcertSessionFilter& InSessionFilter) override;
	virtual bool GetSessionActivities(const IConcertServer& InServer, const FGuid& SessionId, int64 FromActivityId, int64 ActivityCount, TArray<FConcertSessionSerializedPayload>& OutActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, bool bIncludeDetails) override;
	virtual void OnLiveSessionRenamed(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) override;
	virtual void OnArchivedSessionRenamed(const IConcertServer& InServer, const FString& InArchivedSessionRoot, const FConcertSessionInfo& InArchivedSessionInfo) override;

private:
	void CreateWorkspace(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession);
	void DestroyWorkspace(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession);

	void CreateSequencerManager(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession);
	void DestroySequencerManager(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession);

	void CreateLiveSession(const TSharedRef<IConcertServerSession>& InSession);
	void DestroyLiveSession(const TSharedRef<IConcertServerSession>& InSession);

	void CreateArchivedSession(const FString& InArchivedSessionRoot, const FConcertSessionInfo& InArchivedSessionInfo);
	void DestroyArchivedSession(const FGuid& InArchivedSessionId);

	bool GetSessionActivities(const FConcertSyncSessionDatabase& Database, int64 FromActivityId, int64 ActivityCount, TArray<FConcertSessionSerializedPayload>& OutActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, bool bIncludeDetails);

	/** Server for Concert */
	IConcertServerRef ConcertServer;

	/** Flags controlling what features are enabled for sessions within this server */
	EConcertSyncSessionFlags SessionFlags;

	/** Map of live session IDs to their associated workspaces */
	TMap<FGuid, TSharedPtr<FConcertServerWorkspace>> LiveSessionWorkspaces;

	/** Map of live session IDs to their associated sequencer managers */
	TMap<FGuid, TSharedPtr<FConcertServerSequencerManager>> LiveSessionSequencerManagers;

	/** Map of live session IDs to their associated session data */
	TMap<FGuid, TSharedPtr<FConcertSyncServerLiveSession>> LiveSessions;

	/** Map of archived session IDs to their associated session data */
	TMap<FGuid, TSharedPtr<FConcertSyncServerArchivedSession>> ArchivedSessions;
};
