// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SessionHistory/AbstractSessionHistoryController.h"

class FConcertSyncSessionDatabase;
class IConcertSyncServer;
class IConcertServerSession;

class FServerSessionHistoryController : public FAbstractSessionHistoryController
{
public:

	FServerSessionHistoryController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer);
	
protected:

	//~ Begin FAbstractSessionHistoryController Interface
	virtual void GetActivities(int64 MaximumNumberOfActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, TArray<FConcertSessionActivity>& OutFetchedActivities) const override;
	virtual bool GetPackageEvent(const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const override;
	virtual TFuture<TOptional<FConcertSyncTransactionEvent>> GetTransactionEvent(const FConcertSessionActivity& Activity) const override;
	//~ End FAbstractSessionHistoryController Interface

private:

	TSharedRef<IConcertServerSession> InspectedSession;
	TSharedRef<IConcertSyncServer> SyncServer;

	TFuture<TOptional<FConcertSyncTransactionEvent>> FindOrRequestTransactionEvent(const FConcertSyncSessionDatabase& Database, const int64 TransactionEventId) const;
};
