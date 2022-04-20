// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveServerSessionHistoryController.h"

#include "ConcertSyncSessionDatabase.h"
#include "IConcertSession.h"
#include "IConcertSyncServer.h"
#include "MultiUserServerUserSettings.h"

namespace UE::MultiUserServer::Private
{
	static SSessionHistory::FArguments MakeLiveTabSessionHistoryArguments()
	{
		return SSessionHistory::FArguments()
			.ColumnVisibilitySnapshot(UMultiUserServerUserSettings::GetUserSettings()->GetLiveActivityBrowserColumnVisibility())
			.SaveColumnVisibilitySnapshot_Lambda([](const FColumnVisibilitySnapshot& Snapshot)
			{
				UMultiUserServerUserSettings::GetUserSettings()->SetLiveActivityBrowserColumnVisibility(Snapshot);
			});
	}
}

FLiveServerSessionHistoryController::FLiveServerSessionHistoryController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer)
	: FServerSessionHistoryControllerBase(InspectedSession->GetId(), UE::MultiUserServer::Private::MakeLiveTabSessionHistoryArguments())
	, SyncServer(MoveTemp(SyncServer))
{
	ReloadActivities();
	UMultiUserServerUserSettings::GetUserSettings()->OnLiveActivityBrowserColumnVisibility().AddRaw(this, &FLiveServerSessionHistoryController::OnActivityListColumnVisibilitySettingsUpdated);

	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(GetSessionId()))
	{
		Database->OnActivityProduced().AddRaw(this, &FLiveServerSessionHistoryController::OnSessionProduced);
	}
}

FLiveServerSessionHistoryController::~FLiveServerSessionHistoryController()
{
	if (UMultiUserServerUserSettings* Settings = UMultiUserServerUserSettings::GetUserSettings(); IsValid(Settings))
	{
		Settings->OnLiveActivityBrowserColumnVisibility().RemoveAll(this);
	}
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

void FLiveServerSessionHistoryController::OnActivityListColumnVisibilitySettingsUpdated(const FColumnVisibilitySnapshot& NewValue)
{
	GetSessionHistory()->OnColumnVisibilitySettingsChanged(NewValue);
}
