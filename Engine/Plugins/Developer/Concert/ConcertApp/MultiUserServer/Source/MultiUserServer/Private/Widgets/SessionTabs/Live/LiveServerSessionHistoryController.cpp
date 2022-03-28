// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveServerSessionHistoryController.h"

#include "ConcertSyncSessionDatabase.h"
#include "IConcertSession.h"
#include "IConcertSyncServer.h"

FLiveServerSessionHistoryController::FLiveServerSessionHistoryController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer)
	: FServerSessionHistoryControllerBase(InspectedSession->GetId())
	, SyncServer(MoveTemp(SyncServer))
{
	ReloadActivities();

	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(GetSessionId()))
	{
		Database->OnActivityProduced().AddRaw(this, &FLiveServerSessionHistoryController::OnSessionProduced);
	}
}

FLiveServerSessionHistoryController::~FLiveServerSessionHistoryController()
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(GetSessionId()))
	{
		Database->OnActivityProduced().RemoveAll(this);
	}
}

TOptional<FConcertSyncSessionDatabaseNonNullPtr> FLiveServerSessionHistoryController::GetSessionDatabase(const FGuid& InSessionId) const
{
	return SyncServer->GetLiveSessionDatabase(InSessionId);
}

void FLiveServerSessionHistoryController::OnSessionProduced(const FConcertSyncActivity& ProducedActivity)
{
	ReloadActivities();
}
