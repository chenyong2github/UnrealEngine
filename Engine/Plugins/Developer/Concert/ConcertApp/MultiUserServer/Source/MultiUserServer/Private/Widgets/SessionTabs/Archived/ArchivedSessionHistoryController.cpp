// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchivedSessionHistoryController.h"

#include "MultiUserServerUserSettings.h"

namespace UE::MultiUserServer::Private
{
	static SSessionHistory::FArguments MakeArchivedTabSessionHistoryArguments(SSessionHistory::FArguments&& InArgs)
	{
		return InArgs
			.ColumnVisibilitySnapshot(UMultiUserServerUserSettings::GetUserSettings()->GetArchivedActivityBrowserColumnVisibility())
			.SaveColumnVisibilitySnapshot_Lambda([](const FColumnVisibilitySnapshot& Snapshot)
			{
				UMultiUserServerUserSettings::GetUserSettings()->SetArchivedActivityBrowserColumnVisibility(Snapshot);
			});
	}
}

FArchivedSessionHistoryController::FArchivedSessionHistoryController(FGuid SessionId, TSharedRef<IConcertSyncServer> SyncServer, SSessionHistory::FArguments Arguments)
	: FServerSessionHistoryControllerBase(MoveTemp(SessionId), UE::MultiUserServer::Private::MakeArchivedTabSessionHistoryArguments(MoveTemp(Arguments)))
	, SyncServer(MoveTemp(SyncServer))
{
	ReloadActivities();
	UMultiUserServerUserSettings::GetUserSettings()->OnArchivedActivityBrowserColumnVisibility().AddRaw(this, &FArchivedSessionHistoryController::OnActivityListColumnVisibilitySettingsUpdated);
}

FArchivedSessionHistoryController::~FArchivedSessionHistoryController()
{
	if (UMultiUserServerUserSettings* Settings = UMultiUserServerUserSettings::GetUserSettings(); IsValid(Settings))
	{
		Settings->OnArchivedActivityBrowserColumnVisibility().RemoveAll(this);
	}
}

TOptional<FConcertSyncSessionDatabaseNonNullPtr> FArchivedSessionHistoryController::GetSessionDatabase(const FGuid& InSessionId) const
{
	return SyncServer->GetArchivedSessionDatabase(InSessionId);
}

void FArchivedSessionHistoryController::OnActivityListColumnVisibilitySettingsUpdated(const FColumnVisibilitySnapshot& NewValue)
{
	GetSessionHistory()->OnColumnVisibilitySettingsChanged(NewValue);
}
