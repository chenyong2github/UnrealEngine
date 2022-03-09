// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSessionBrowserController.h"

#include "ConcertServerStyle.h"
#include "IConcertServer.h"
#include "IConcertSyncServer.h"
#include "Framework/Docking/TabManager.h"
#include "SConcertServerSessionBrowser.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/AsyncTaskNotification.h"
#include "SessionBrowser/ConcertSessionItem.h"
#include "Textures/SlateIcon.h"
#include "Widgets/ConcertServerTabs.h"
#include "Widgets/ConcertServerWindowController.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

int32 FConcertServerSessionBrowserController::GetNumConnectedClients(const FGuid& SessionId) const
{
	TSharedPtr<IConcertServerSession> Session = ServerInstance->GetConcertServer()->GetLiveSession(SessionId);
	return ensure(Session) ? Session->GetSessionClients().Num() : 0;
}

void FConcertServerSessionBrowserController::Init(const FConcertComponentInitParams& Params)
{
	ServerInstance = Params.Server;
	Owner = Params.WindowController;
	FGlobalTabmanager::Get()->RegisterTabSpawner(
			ConcertServerTabs::GetSessionBrowserTabId(),
			FOnSpawnTab::CreateRaw(this, &FConcertServerSessionBrowserController::SpawnSessionBrowserTab)
		)
		.SetDisplayName(LOCTEXT("SessionBrowserTabTitle", "Session Browser"))
		.SetTooltipText(LOCTEXT("SessionBrowserTooltipText", "A section to browse, start, archive, and restore server sessions."))
		.SetIcon(FSlateIcon(FConcertServerStyle::GetStyleSetName(), TEXT("Concert.MultiUser")));
}

TArray<FConcertServerInfo> FConcertServerSessionBrowserController::GetServers() const
{
	return { ServerInstance->GetConcertServer()->GetServerInfo() };
}

TArray<IConcertSessionBrowserController::FActiveSessionInfo> FConcertServerSessionBrowserController::GetActiveSessions() const
{
	const FConcertServerInfo& ServerInfo = ServerInstance->GetConcertServer()->GetServerInfo();
	const TArray<TSharedPtr<IConcertServerSession>> ServerSessions = ServerInstance->GetConcertServer()->GetLiveSessions();
	
	TArray<FActiveSessionInfo> Result;
	Result.Reserve(ServerSessions.Num());
	for (const TSharedPtr<IConcertServerSession>& LiveSession : ServerSessions)
	{
		FActiveSessionInfo Info{ ServerInfo, LiveSession->GetSessionInfo(), LiveSession->GetSessionClients() };
		Result.Add(Info);
	}

	return Result;
}

TArray<IConcertSessionBrowserController::FArchivedSessionInfo> FConcertServerSessionBrowserController::GetArchivedSessions() const
{
	const FConcertServerInfo& ServerInfo = ServerInstance->GetConcertServer()->GetServerInfo();
	const TArray<FConcertSessionInfo> ConcertSessionInfos = ServerInstance->GetConcertServer()->GetArchivedSessionInfos();
	
	TArray<FArchivedSessionInfo> Result;
	Result.Reserve(ConcertSessionInfos.Num());
	for (const FConcertSessionInfo& SessionInfo : ConcertSessionInfos)
	{
		Result.Emplace(ServerInfo, SessionInfo);
	}

	return Result;
}

TOptional<FConcertSessionInfo> FConcertServerSessionBrowserController::GetActiveSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const
{
	const TSharedPtr<IConcertServerSession> LiveSession = ServerInstance->GetConcertServer()->GetLiveSession(SessionId);
	return LiveSession ? LiveSession->GetSessionInfo() : TOptional<FConcertSessionInfo>{};
}

TOptional<FConcertSessionInfo> FConcertServerSessionBrowserController::GetArchivedSessionInfo(const FGuid& AdminEndpoint, const FGuid& SessionId) const
{
	return ServerInstance->GetConcertServer()->GetArchivedSessionInfo(SessionId);
}

void FConcertServerSessionBrowserController::CreateSession(const FGuid& ServerAdminEndpointId, const FString& SessionName)
{
	FConcertSessionInfo SessionInfo = ServerInstance->GetConcertServer()->CreateSessionInfo();
	SessionInfo.SessionName = SessionName;
	SessionInfo.Settings.Initialize();
	FConcertSessionVersionInfo VersionInfo;
	VersionInfo.Initialize();
	SessionInfo.VersionInfos.Emplace(VersionInfo);
	
	FText FailureReason = FText::GetEmpty();
	const bool bSuccess = ServerInstance->GetConcertServer()->CreateSession(SessionInfo, FailureReason) != nullptr;
	
	NotifyUserOfFinishedSessionAction(bSuccess,
		bSuccess ? FText::Format(LOCTEXT("CreatedSessionFmt", "Created Session '{0}'"), FText::FromString(SessionName)) : FText::Format(LOCTEXT("FailedToCreateSessionFmt", "Failed to create Session '{0}'"), FText::FromString(SessionName)),
		FailureReason
		);
}

void FConcertServerSessionBrowserController::ArchiveSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& ArchiveName, const FConcertSessionFilter& SessionFilter)
{
	FText FailureReason = FText::GetEmpty();
	const bool bSuccess = ServerInstance->GetConcertServer()->ArchiveSession(SessionId, ArchiveName, SessionFilter, FailureReason).IsValid();
	
	NotifyUserOfFinishedSessionAction(bSuccess,
		bSuccess ? FText::Format(LOCTEXT("ArchivedSessionFmt", "Archived Session '{0}'"),  FText::FromString(ArchiveName)) : FText::Format(LOCTEXT("FailedToArchivedSessionFmt", "Failed to archive Session '{0}'"), FText::FromString(ArchiveName)),
		FailureReason
		);
}

void FConcertServerSessionBrowserController::RestoreSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& RestoredName, const FConcertSessionFilter& SessionFilter)
{
	if (const TOptional<FConcertSessionInfo> SessionInfo = GetArchivedSessionInfo(ServerAdminEndpointId, SessionId))
	{
		FText FailureReason = FText::GetEmpty();
		const bool bSuccess = ServerInstance->GetConcertServer()->RestoreSession(SessionId, *SessionInfo, SessionFilter, FailureReason).IsValid();
		NotifyUserOfFinishedSessionAction(bSuccess,
			bSuccess ? FText::Format(LOCTEXT("RestoreSessionFmt", "Restored Session '{0}'"),  FText::FromString(RestoredName)) : FText::Format(LOCTEXT("FailedToRestoreSessionFmt", "Failed to restore Session '{0}'"), FText::FromString(RestoredName)),
			FailureReason
			);
	}
}

TSharedRef<SDockTab> FConcertServerSessionBrowserController::SpawnSessionBrowserTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.Label(LOCTEXT("SessionBrowserTabTitle", "Sessions"))
		.TabRole(MajorTab)
		[
			SAssignNew(ConcertBrowser, SConcertServerSessionBrowser, SharedThis(this))
				.DoubleClickSession(this, &FConcertServerSessionBrowserController::OpenSession)
		];

	FGlobalTabmanager::Get()->SetMainTab(DockTab);
	return DockTab;
}

void FConcertServerSessionBrowserController::OpenSession(TSharedPtr<FConcertSessionItem> SessionItem)
{
	Owner.Pin()->OpenSessionTab(SessionItem->SessionId);
}

void FConcertServerSessionBrowserController::RenameSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName)
{
	if (const TOptional<FConcertSessionInfo> SessionInfo = GetArchivedSessionInfo(ServerAdminEndpointId, SessionId))
	{
		FText FailureReason = FText::GetEmpty();
		const bool bSuccess = ServerInstance->GetConcertServer()->RenameSession(SessionId, NewName, FailureReason);
		NotifyUserOfFinishedSessionAction(bSuccess,
				bSuccess ? FText::Format(LOCTEXT("RenameSessionFmt", "Rename Session '{0}' as '{1}'"), FText::FromString(SessionInfo->SessionName), FText::FromString(NewName))
					: FText::Format(LOCTEXT("FailedToArchivedSessionFmt", "Failed to rename Session '{0}' as '{1}'"), FText::FromString(SessionInfo->SessionName), FText::FromString(NewName)),
				FailureReason
				);
	}
}

void FConcertServerSessionBrowserController::DeleteSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId)
{
	const TOptional<FConcertSessionInfo> ArchivedInfo = GetArchivedSessionInfo(ServerAdminEndpointId, SessionId);
	const TOptional<FConcertSessionInfo> LiveInfo = GetActiveSessionInfo(ServerAdminEndpointId, SessionId);
	const TOptional<FString> SessionName = ArchivedInfo ? ArchivedInfo->SessionName : LiveInfo ? LiveInfo->SessionName : TOptional<FString>{};
	if (SessionName)
	{
		FText FailureReason;
		const bool bSuccess = ServerInstance->GetConcertServer()->DestroySession(SessionId, FailureReason);
		NotifyUserOfFinishedSessionAction(bSuccess,
			bSuccess ? FText::Format(LOCTEXT("ArchivedSessionFmt", "Deleted Session '{0}'"), FText::FromString(*SessionName)) : FText::Format(LOCTEXT("FailedToArchivedSessionFmt", "Failed to archive Session '{0}'"), FText::FromString(LiveInfo->SessionName)),
			FailureReason
			);
	}
}

void FConcertServerSessionBrowserController::NotifyUserOfFinishedSessionAction(const bool bSuccess, const FText& Title, const FText& Details)
{
	FNotificationInfo NotificationInfo(Title);
	NotificationInfo.SubText = Details;
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);

	if (bSuccess)
	{
		ConcertBrowser->RefreshSessionList();
	}
}

#undef LOCTEXT_NAMESPACE 
