// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SessionTabs/ServerSessionHistoryControllerBase.h"

struct FConcertSyncActivity;
class FConcertSyncSessionDatabase;
class IConcertSyncServer;
class IConcertServerSession;

class FLiveServerSessionHistoryController : public FServerSessionHistoryControllerBase
{
public:

	FLiveServerSessionHistoryController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer);
	virtual ~FLiveServerSessionHistoryController() override;
	
protected:

	//~ Begin FServerSessionHistoryControllerBase Interface
	virtual TOptional<FConcertSyncSessionDatabaseNonNullPtr> GetSessionDatabase(const FGuid& InSessionId) const override;
	//~ End FServerSessionHistoryControllerBase Interface

private:

	TSharedRef<IConcertSyncServer> SyncServer;
	
	void OnSessionProduced(const FConcertSyncActivity& ProducedActivity);
};
