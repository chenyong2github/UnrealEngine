// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchivedSessionHistoryController.h"

FArchivedSessionHistoryController::FArchivedSessionHistoryController(FGuid SessionId, TSharedRef<IConcertSyncServer> SyncServer, SSessionHistory::FArguments Arguments)
	: FServerSessionHistoryControllerBase(MoveTemp(SessionId), MoveTemp(Arguments))
	, SyncServer(MoveTemp(SyncServer))
{
	ReloadActivities();
}

TOptional<FConcertSyncSessionDatabaseNonNullPtr> FArchivedSessionHistoryController::GetSessionDatabase(const FGuid& InSessionId) const
{
	return SyncServer->GetArchivedSessionDatabase(InSessionId);
}
