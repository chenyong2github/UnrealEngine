// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServerSessionHistoryController.h"

#include "ConcertSyncSessionDatabase.h"
#include "IConcertSession.h"
#include "IConcertSyncServer.h"

FServerSessionHistoryController::FServerSessionHistoryController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer)
	: InspectedSession(MoveTemp(InspectedSession))
	, SyncServer(MoveTemp(SyncServer))
{}

void FServerSessionHistoryController::GetActivities(int64 MaximumNumberOfActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, TArray<FConcertSessionActivity>& OutFetchedActivities) const
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		OutEndpointClientInfoMap.Reset();
		OutFetchedActivities.Reset();

		int64 LastActivityId = INDEX_NONE;
		Database->GetActivityMaxId(LastActivityId);
		const int64 FirstActivityIdToFetch = FMath::Max<int64>(1, LastActivityId - MaximumNumberOfActivities);
		Database->EnumerateActivitiesInRange(FirstActivityIdToFetch, MaximumNumberOfActivities, [this, &Database, &OutEndpointClientInfoMap, &OutFetchedActivities](FConcertSyncActivity&& InActivity)
		{
			if (!OutEndpointClientInfoMap.Contains(InActivity.EndpointId))
			{
				FConcertSyncEndpointData EndpointData;
				if (Database->GetEndpoint(InActivity.EndpointId, EndpointData))
				{
					OutEndpointClientInfoMap.Add(InActivity.EndpointId, EndpointData.ClientInfo);
				}
			}

			FStructOnScope ActivitySummary;
			if (InActivity.EventSummary.GetPayload(ActivitySummary))
			{
				OutFetchedActivities.Emplace(MoveTemp(InActivity), MoveTemp(ActivitySummary));
			}

			return true;
		});
	}
}

bool FServerSessionHistoryController::GetPackageEvent(const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		return Database->GetPackageEventMetaData(Activity.Activity.EventId, OutPackageEvent.PackageRevision, OutPackageEvent.PackageInfo);
	}
	
	return false;
}

TFuture<TOptional<FConcertSyncTransactionEvent>> FServerSessionHistoryController::GetTransactionEvent(const FConcertSessionActivity& Activity) const
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		return FindOrRequestTransactionEvent(*Database, Activity.Activity.EventId);
	}
	
	return MakeFulfilledPromise<TOptional<FConcertSyncTransactionEvent>>().GetFuture(); // Not found.
}

TFuture<TOptional<FConcertSyncTransactionEvent>> FServerSessionHistoryController::FindOrRequestTransactionEvent(const FConcertSyncSessionDatabase& Database, const int64 TransactionEventId) const
{
	FConcertSyncTransactionEvent TransactionEvent;
	return Database.GetTransactionEvent(TransactionEventId, TransactionEvent, false)
		?	MakeFulfilledPromise<TOptional<FConcertSyncTransactionEvent>>(MoveTemp(TransactionEvent)).GetFuture()
		:	MakeFulfilledPromise<TOptional<FConcertSyncTransactionEvent>>().GetFuture();
}
