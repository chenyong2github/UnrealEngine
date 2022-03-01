// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SessionBrowser/IConcertSessionBrowserController.h"
#include "Widgets/IConcertComponent.h"

class FConcertSessionItem;
class FSpawnTabArgs;
class SDockTab;
class SConcertServerSessionBrowser;

/** Implements controller in model-view-controller pattern. */
class FConcertServerSessionBrowserController
	:
	public TSharedFromThis<FConcertServerSessionBrowserController>,
	public IConcertComponent,
	public IConcertSessionBrowserController
{
public:

	//~ Begin IConcertComponent Interface
	virtual void Init(const FConcertComponentInitParams& Params) override;
	//~ End IConcertComponent Interface
	
	//~ Begin IConcertSessionBrowserController Interface
	virtual TArray<FConcertServerInfo> GetServers() const override;
	virtual TArray<FActiveSessionInfo> GetActiveSessions() const override;
	virtual TArray<FArchivedSessionInfo> GetArchivedSessions() const override;
	virtual TOptional<FConcertSessionInfo> GetActiveSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const override;
	virtual TOptional<FConcertSessionInfo> GetArchivedSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const override;
	
	virtual void CreateSession(const FGuid& ServerAdminEndpointId, const FString& SessionName) override;
	virtual void ArchiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& ArchiveName, const FConcertSessionFilter& SessionFilter) override;
	virtual void RestoreSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& RestoredName, const FConcertSessionFilter& SessionFilter) override;
	virtual void RenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName) override { RenameSession(ServerAdminEndpointId, SessionId, NewName); }
	virtual void RenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName) override { RenameSession(ServerAdminEndpointId, SessionId, NewName); }
	virtual void DeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) override { DeleteSession(ServerAdminEndpointId, SessionId); }
	virtual void DeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) override { DeleteSession(ServerAdminEndpointId, SessionId); }
	
	// The server operator always has permission for these actions:
	virtual bool CanRenameActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const override { return true; }
	virtual bool CanRenameArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const override { return true; }
	virtual bool CanDeleteActiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const override { return true; }
	virtual bool CanDeleteArchivedSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const override { return true; }
	//~ Begin IConcertSessionBrowserController Interface

private:

	/** Server instance we're representing */
	TSharedPtr<IConcertSyncServer> ServerInstance;
	/** Used to open selected sessions */
	TWeakPtr<FConcertServerWindowController> Owner;
	
	TSharedPtr<SConcertServerSessionBrowser> ConcertBrowser;
	
	void RenameSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName);
	void DeleteSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId);
	
	TSharedRef<SDockTab> SpawnSessionBrowserTab(const FSpawnTabArgs& Args);
	void OpenSession(TSharedPtr<FConcertSessionItem> SessionItem);
	
	void NotifyUserOfFinishedSessionAction(const bool bSuccess, const FText& Title) { NotifyUserOfFinishedSessionAction(bSuccess, Title, FText::GetEmpty()); }
    /** Shared logic for displaying a notification after creating, archiving, restoring, renaming and deleting a session. */
	void NotifyUserOfFinishedSessionAction(const bool bSuccess, const FText& Title, const FText& Details);
};
