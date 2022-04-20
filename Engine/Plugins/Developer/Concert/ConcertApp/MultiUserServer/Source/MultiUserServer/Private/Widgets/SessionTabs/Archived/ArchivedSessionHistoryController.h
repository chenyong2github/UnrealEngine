// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SessionTabs/ServerSessionHistoryControllerBase.h"

class FArchivedSessionHistoryController : public FServerSessionHistoryControllerBase
{
public:
	
	FArchivedSessionHistoryController(FGuid SessionId, TSharedRef<IConcertSyncServer> SyncServer, SSessionHistory::FArguments Arguments);
	virtual ~FArchivedSessionHistoryController() override;
	
protected:

	//~ Begin FServerSessionHistoryControllerBase Interface
	virtual TOptional<FConcertSyncSessionDatabaseNonNullPtr> GetSessionDatabase(const FGuid& InSessionId) const override;
	//~ End FServerSessionHistoryControllerBase Interface

private:

	const TSharedRef<IConcertSyncServer> SyncServer;

	void OnActivityListColumnVisibilitySettingsUpdated(const FColumnVisibilitySnapshot& NewValue);
};
