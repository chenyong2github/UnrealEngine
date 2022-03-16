// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertSessionPackageViewerController.h"

#include "ConcertSyncSessionDatabase.h"
#include "IConcertSyncServer.h"
#include "SConcertSessionPackageViewer.h"
#include "Concert/Private/ConcertServerSession.h"

FConcertSessionPackageViewerController::FConcertSessionPackageViewerController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer)
	: InspectedSession(MoveTemp(InspectedSession))
	, SyncServer(MoveTemp(SyncServer))
	, PackageViewer(MakePackageViewer())
{
	ReloadActivities();
	
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		Database->OnActivityProduced().AddRaw(this, &FConcertSessionPackageViewerController::OnSessionProduced);
	}
}

FConcertSessionPackageViewerController::~FConcertSessionPackageViewerController()
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		Database->OnActivityProduced().RemoveAll(this);
	}
}

void FConcertSessionPackageViewerController::ReloadActivities() const
{
	PackageViewer->ResetActivityList();
	
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		struct FPackageActivity
		{
			FConcertSessionActivity Activity;
			FConcertSyncPackageEventData PackageData;
		};
		
		TMap<FName, FPackageActivity> LatestPackageActivities;
		Database->EnumeratePackageActivities([this, &LatestPackageActivities](FConcertSyncActivity&& BasePart, FConcertSyncPackageEventData& EventData)
		{
			FStructOnScope ActivitySummary;
			if (BasePart.EventSummary.GetPayload(ActivitySummary))
			{
				FPackageActivity Activity{ { BasePart, ActivitySummary }, EventData };
				const bool bWasRenamed = Activity.PackageData.MetaData.PackageInfo.PackageUpdateType == EConcertPackageUpdateType::Renamed;
				if (bWasRenamed)
				{
					LatestPackageActivities.Remove(EventData.MetaData.PackageInfo.PackageName);
					LatestPackageActivities.Emplace(EventData.MetaData.PackageInfo.NewPackageName, MoveTemp(Activity));
				}
				else
				{
					LatestPackageActivities.Emplace(EventData.MetaData.PackageInfo.PackageName, MoveTemp(Activity));
				}
			}
			return EBreakBehavior::Continue;
		});

		for (auto ActivityIt = LatestPackageActivities.CreateIterator(); ActivityIt; ++ActivityIt)
		{
			PackageViewer->AppendActivity(MoveTemp(ActivityIt->Value.Activity));
		}
	}
}

TSharedRef<SConcertSessionPackageViewer> FConcertSessionPackageViewerController::MakePackageViewer() const
{
	return SNew(SConcertSessionPackageViewer)
		.GetClientInfo_Raw(this, &FConcertSessionPackageViewerController::GetClientInfo)
		.GetPackageEvent_Raw(this, &FConcertSessionPackageViewerController::GetPackageEvent);
}

TOptional<FConcertClientInfo> FConcertSessionPackageViewerController::GetClientInfo(FGuid ClientId) const
{
	const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId());
	FConcertSyncEndpointData Result;
	if (Database && Database->GetEndpoint(ClientId, Result))
	{
		return Result.ClientInfo;
	}
	return {};
}

bool FConcertSessionPackageViewerController::GetPackageEvent(const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const
{
	if (const TOptional<FConcertSyncSessionDatabaseNonNullPtr> Database = SyncServer->GetLiveSessionDatabase(InspectedSession->GetId()))
	{
		return Database->GetPackageEventMetaData(Activity.Activity.EventId, OutPackageEvent.PackageRevision, OutPackageEvent.PackageInfo);
	}
	return false;
}

void FConcertSessionPackageViewerController::OnSessionProduced(const FConcertSyncActivity& ProducedActivity) const
{
	if (ProducedActivity.EventType == EConcertSyncActivityEventType::Package)
	{
		ReloadActivities();
	}
}
