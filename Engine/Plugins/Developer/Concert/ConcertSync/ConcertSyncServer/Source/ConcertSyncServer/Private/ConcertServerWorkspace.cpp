// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertServerWorkspace.h"

#include "IConcertSession.h"
#include "ConcertSyncServerLiveSession.h"
#include "ConcertServerSyncCommandQueue.h"
#include "ConcertSyncSessionDatabase.h"
#include "ConcertTransactionEvents.h"
#include "ConcertServerDataStore.h"
#include "ConcertLogGlobal.h"

#include "Algo/Transform.h"

FConcertServerWorkspace::FConcertServerWorkspace(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession)
{
	BindSession(InLiveSession);
}

FConcertServerWorkspace::~FConcertServerWorkspace()
{
	UnbindSession();
}

void FConcertServerWorkspace::BindSession(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession)
{
	check(InLiveSession->IsValidSession());

	UnbindSession();
	LiveSession = InLiveSession;

	// Create Sync Command Queue
	SyncCommandQueue = MakeShared<FConcertServerSyncCommandQueue>();

	// Create Locked Resources
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableLocking))
	{
		LockedResources = MakeUnique<FLockedResources>();
	}

	// Create Data Store
	DataStore = MakeUnique<FConcertServerDataStore>(LiveSession.ToSharedRef());

	// Register Tick events
	LiveSession->GetSession().OnTick().AddRaw(this, &FConcertServerWorkspace::HandleTick);

	// Register Client Change events
	LiveSession->GetSession().OnSessionClientChanged().AddRaw(this, &FConcertServerWorkspace::HandleSessionClientChanged);

	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncRequestedEvent>(this, &FConcertServerWorkspace::HandleSyncRequestedEvent);

	LiveSession->GetSession().RegisterCustomEventHandler<FConcertPackageUpdateEvent>(this, &FConcertServerWorkspace::HandlePackageUpdateEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertPlaySessionEvent>(this, &FConcertServerWorkspace::HandlePlaySessionEvent);

	LiveSession->GetSession().RegisterCustomEventHandler<FConcertTransactionFinalizedEvent>(this, &FConcertServerWorkspace::HandleTransactionFinalizedEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertTransactionSnapshotEvent>(this, &FConcertServerWorkspace::HandleTransactionSnapshotEvent);

	LiveSession->GetSession().RegisterCustomRequestHandler<FConcertResourceLockRequest, FConcertResourceLockResponse>(this, &FConcertServerWorkspace::HandleResourceLockRequest);
}

void FConcertServerWorkspace::UnbindSession()
{
	if (LiveSession.IsValid())
	{
		// Destroy Sync Command Queue
		SyncCommandQueue.Reset();

		// Destroy Locked Resources
		LockedResources.Reset();

		// Destroy Data Store
		DataStore.Reset();

		// Unregister Tick events
		LiveSession->GetSession().OnTick().RemoveAll(this);

		// Unregister Client Change events
		LiveSession->GetSession().OnSessionClientChanged().RemoveAll(this);

		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncRequestedEvent>(this);

		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertPackageUpdateEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertPlaySessionEvent>(this);

		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertTransactionFinalizedEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertTransactionSnapshotEvent>(this);

		LiveSession->GetSession().UnregisterCustomRequestHandler<FConcertResourceLockRequest>();
		LiveSession.Reset();
	}
}

void FConcertServerWorkspace::HandleTick(IConcertServerSession& InSession, float InDeltaTime)
{
	check(&LiveSession->GetSession() == &InSession);

	static const double SyncFrameLimitSeconds = 1.0 / 60;
	SyncCommandQueue->ProcessQueue(SyncFrameLimitSeconds);

	for (auto ManualSyncEndpointIter = ManualSyncEndpoints.CreateIterator(); ManualSyncEndpointIter; ++ManualSyncEndpointIter)
	{
		if (SyncCommandQueue->IsQueueEmpty(*ManualSyncEndpointIter))
		{
			LiveSession->GetSession().SendCustomEvent(FConcertWorkspaceSyncCompletedEvent(), *ManualSyncEndpointIter, EConcertMessageFlags::ReliableOrdered);
			SyncCommandQueue->SetCommandProcessingMethod(*ManualSyncEndpointIter, FConcertServerSyncCommandQueue::ESyncCommandProcessingMethod::ProcessAll);
			ManualSyncEndpointIter.RemoveCurrent();
		}
	}
}

void FConcertServerWorkspace::HandleSessionClientChanged(IConcertServerSession& InSession, EConcertClientStatus InClientStatus, const FConcertSessionClientInfo& InClientInfo)
{
	check(&LiveSession->GetSession() == &InSession);

	if (InClientStatus == EConcertClientStatus::Connected || InClientStatus == EConcertClientStatus::Updated)
	{
		FConcertSyncEndpointData SyncEndpointData;
		SyncEndpointData.ClientInfo = InClientInfo.ClientInfo;
		SetEndpoint(InClientInfo.ClientEndpointId, SyncEndpointData);
	}

	if (InClientStatus == EConcertClientStatus::Connected)
	{
		SyncCommandQueue->RegisterEndpoint(InClientInfo.ClientEndpointId);

		// Add the connection activity
		if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableConnectionHistory))
		{
			FConcertSyncConnectionActivity ConnectionActivity;
			ConnectionActivity.EndpointId = InClientInfo.ClientEndpointId;
			ConnectionActivity.EventData.ConnectionEventType = EConcertSyncConnectionEventType::Connected;
			ConnectionActivity.EventSummary.SetTypedPayload(FConcertSyncConnectionActivitySummary::CreateSummaryForEvent(ConnectionActivity.EventData));
			AddConnectionActivity(ConnectionActivity);
		}
	}
	else if (InClientStatus == EConcertClientStatus::Disconnected)
	{
		UnlockAllWorkspaceResources(InClientInfo.ClientEndpointId);
		HandleEndPlaySessions(InClientInfo.ClientEndpointId);

		// Add the disconnection activity
		if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableConnectionHistory))
		{
			FConcertSyncConnectionActivity ConnectionActivity;
			ConnectionActivity.EndpointId = InClientInfo.ClientEndpointId;
			ConnectionActivity.EventData.ConnectionEventType = EConcertSyncConnectionEventType::Disconnected;
			ConnectionActivity.EventSummary.SetTypedPayload(FConcertSyncConnectionActivitySummary::CreateSummaryForEvent(ConnectionActivity.EventData));
			AddConnectionActivity(ConnectionActivity);
		}

		LiveSyncEndpoints.Remove(InClientInfo.ClientEndpointId);
		ManualSyncEndpoints.Remove(InClientInfo.ClientEndpointId);
		SyncCommandQueue->UnregisterEndpoint(InClientInfo.ClientEndpointId);
	}
}

void FConcertServerWorkspace::HandleSyncRequestedEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncRequestedEvent& Event)
{
	if (Event.bEnableLiveSync)
	{
		LiveSyncEndpoints.AddUnique(Context.SourceEndpointId);
	}
	else
	{
		LiveSyncEndpoints.Remove(Context.SourceEndpointId);
	}

	// Resolve the activity range to sync
	int64 FirstActivityIdToSync = 0;
	int64 NumActivitiesToSync = 0;
	{
		int64 ActivityMaxId = 0;
		LiveSession->GetSessionDatabase().GetActivityMaxId(ActivityMaxId);
		if (!Event.bEnableLiveSync)
		{
			ActivityMaxId = FMath::Min<int64>(ActivityMaxId, Event.LastActivityIdToSync);
		}

		FirstActivityIdToSync = FMath::Max<int64>(1, Event.FirstActivityIdToSync);
		NumActivitiesToSync = FMath::Max<int64>(0, (ActivityMaxId - FirstActivityIdToSync) + 1);
	}

	// Manual sync requests will be time-sliced until they've finished their requested sync
	ManualSyncEndpoints.AddUnique(Context.SourceEndpointId);
	SyncCommandQueue->SetCommandProcessingMethod(Context.SourceEndpointId, FConcertServerSyncCommandQueue::ESyncCommandProcessingMethod::ProcessTimeSliced);

	// Sync all endpoints
	LiveSession->GetSessionDatabase().EnumerateEndpointIds([this, &Context](FGuid InEndpointId)
	{
		SyncCommandQueue->QueueCommand(Context.SourceEndpointId, [this, SyncEndpointId = InEndpointId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncEndpointEvent(InEndpointId, SyncEndpointId, InSyncCommandContext.GetNumRemainingCommands());
		});
		return true;
	});

	// Sync all activity
	LiveSession->GetSessionDatabase().EnumerateActivityIdsAndEventTypesInRange(FirstActivityIdToSync, NumActivitiesToSync, [this, &Context](const int64 InActivityId, const EConcertSyncActivityEventType InEventType)
	{
		SyncCommandQueue->QueueCommand(Context.SourceEndpointId, [this, SyncActivityId = InActivityId, SyncEventType = InEventType](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			switch (SyncEventType)
			{
			case EConcertSyncActivityEventType::Connection:
				SendSyncConnectionActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
				break;

			case EConcertSyncActivityEventType::Lock:
				SendSyncLockActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
				break;

			case EConcertSyncActivityEventType::Transaction:
				SendSyncTransactionActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
				break;

			case EConcertSyncActivityEventType::Package:
				SendSyncPackageActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
				break;

			default:
				checkf(false, TEXT("Unhandled EConcertSyncActivityEventType when syncing session activity"));
				break;
			}
		});

		return true;
	});

	// Sync live resource locks
	if (LockedResources)
	{
		SyncCommandQueue->QueueCommand(Context.SourceEndpointId, [this](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			FConcertWorkspaceSyncLockEvent SyncEvent;
			SyncEvent.NumRemainingSyncEvents = InSyncCommandContext.GetNumRemainingCommands();
			Algo::Transform(*LockedResources, SyncEvent.LockedResources, [](const TPair<FName, FLockOwner>& Pair)
			{
				return TPair<FName, FGuid>{ Pair.Key, Pair.Value.EndpointId };
			});
			LiveSession->GetSession().SendCustomEvent(SyncEvent, InEndpointId, EConcertMessageFlags::ReliableOrdered);
		});
	}

	// Sync PIE/SIE play states
	SyncCommandQueue->QueueCommand(Context.SourceEndpointId, [this](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
	{
		for (const TPair<FName, TArray<FPlaySessionInfo>>& PlayInfoPair : ActivePlaySessions)
		{
			for (const FPlaySessionInfo& PlayInfo : PlayInfoPair.Value)
			{
				LiveSession->GetSession().SendCustomEvent(FConcertPlaySessionEvent{ EConcertPlaySessionEventType::BeginPlay, PlayInfo.EndpointId, PlayInfoPair.Key, PlayInfo.bIsSimulating }, InEndpointId, EConcertMessageFlags::ReliableOrdered);
			}
		}
	});
}

void FConcertServerWorkspace::HandlePackageUpdateEvent(const FConcertSessionContext& Context, const FConcertPackageUpdateEvent& Event)
{
	if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnablePackages))
	{
		return;
	}
	
	// Consider acquiring lock on asset saving an explicit lock
	const bool bLockOwned = LockWorkspaceResource(Event.Package.Info.PackageName, Context.SourceEndpointId, EConcertLockFlags::Temporary);
	if (bLockOwned)
	{
		// If the client has the lock, then add the package activity
		{
			FConcertSyncPackageActivity PackageActivity;
			PackageActivity.EndpointId = Context.SourceEndpointId;
			PackageActivity.EventData.Package = Event.Package;
			if (PackageActivity.EventData.Package.Info.PackageUpdateType == EConcertPackageUpdateType::Dummy && PackageActivity.EventData.Package.PackageData.Num() == 0)
			{
				// If this is a dummy package, attempt to migrate the package data from the current head package revision so that newly synced clients will receive the correct package data
				FConcertPackageInfo HeadPackageInfo;
				if (LiveSession->GetSessionDatabase().GetPackageDataForRevision(PackageActivity.EventData.Package.Info.PackageName, HeadPackageInfo, &PackageActivity.EventData.Package.PackageData))
				{
					PackageActivity.EventData.Package.Info.PackageFileExtension = HeadPackageInfo.PackageFileExtension;
				}
			}
			PackageActivity.EventSummary.SetTypedPayload(FConcertSyncPackageActivitySummary::CreateSummaryForEvent(PackageActivity.EventData));
			AddPackageActivity(PackageActivity);
		}

		// Explicitly unlock the resource after saving it
		UnlockWorkspaceResource(Event.Package.Info.PackageName, Context.SourceEndpointId, EConcertLockFlags::Explicit);
	}
	else
	{
		// If the client didn't have the lock, then queue a rejection event so that the client will re-load the head-revision of the package
		SyncCommandQueue->QueueCommand(Context.SourceEndpointId, [this, PackageName = Event.Package.Info.PackageName](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			FConcertPackageRejectedEvent PackageRejectedEvent;
			PackageRejectedEvent.PackageName = PackageName;
			LiveSession->GetSession().SendCustomEvent(PackageRejectedEvent, InEndpointId, EConcertMessageFlags::ReliableOrdered);
		});
	}
}

void FConcertServerWorkspace::HandleTransactionFinalizedEvent(const FConcertSessionContext& InEventContext, const FConcertTransactionFinalizedEvent& InEvent)
{
	if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableTransactions))
	{
		return;
	}

	// Implicitly acquire locks for all objects in the transaction
	TArray<FName> ResourceNames;
	Algo::Transform(InEvent.ExportedObjects, ResourceNames, [](const FConcertExportedObject& InObject)
	{
		// TODO: This isn't always the correct way to build the object path (re: SUBOBJECT_DELIMITER)
		return FName(*FString::Printf(TEXT("%s.%s"), *InObject.ObjectId.ObjectOuterPathName.ToString(), *InObject.ObjectId.ObjectName.ToString()));
	});

	const bool bLockOwned = LockWorkspaceResources(ResourceNames, InEventContext.SourceEndpointId, EConcertLockFlags::Temporary);
	if (bLockOwned)
	{
		// If the client has the lock, then add the transaction activity
		{
			FConcertSyncTransactionActivity TransactionActivity;
			TransactionActivity.EndpointId = InEventContext.SourceEndpointId;
			TransactionActivity.EventData.Transaction = InEvent;
			TransactionActivity.EventSummary.SetTypedPayload(FConcertSyncTransactionActivitySummary::CreateSummaryForEvent(TransactionActivity.EventData));
			AddTransactionActivity(TransactionActivity);
		}

		// Implicitly unlock resources in the transaction (TODO: should use sync queue?)
		bool bUnlocked = UnlockWorkspaceResources(ResourceNames, InEventContext.SourceEndpointId, EConcertLockFlags::None);
		check(bUnlocked);
	}
	else
	{
		// If the client didn't have the lock, then queue a rejection event
		FConcertTransactionRejectedEvent TransactionRejectedEvent;
		TransactionRejectedEvent.TransactionId = InEvent.TransactionId;
		LiveSession->GetSession().SendCustomEvent(TransactionRejectedEvent, InEventContext.SourceEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
}

void FConcertServerWorkspace::HandleTransactionSnapshotEvent(const FConcertSessionContext& InEventContext, const FConcertTransactionSnapshotEvent& InEvent)
{
	if (!EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableTransactions))
	{
		return;
	}

	// Implicitly acquire locks for objects in the transaction
	TArray<FName> ResourceNames;
	Algo::Transform(InEvent.ExportedObjects, ResourceNames, [](const FConcertExportedObject& InObject)
	{
		// TODO: This isn't always the correct way to build the object path (re: SUBOBJECT_DELIMITER)
		return FName(*FString::Printf(TEXT("%s.%s"), *InObject.ObjectId.ObjectOuterPathName.ToString(), *InObject.ObjectId.ObjectName.ToString()));
	});

	const bool bLockOwned = LockWorkspaceResources(ResourceNames, InEventContext.SourceEndpointId, EConcertLockFlags::None);
	if (bLockOwned)
	{
		// If the client has the lock, then forward the snapshot
		TArray<FGuid> QueueClientEndpointIds = LiveSession->GetSession().GetSessionClientEndpointIds();
		QueueClientEndpointIds.Remove(InEventContext.SourceEndpointId);
		LiveSession->GetSession().SendCustomEvent(InEvent, QueueClientEndpointIds, EConcertMessageFlags::UniqueId);
	}
	// otherwise do nothing, we will reject the finalized transaction (TODO: send notification back?)
}

void FConcertServerWorkspace::HandlePlaySessionEvent(const FConcertSessionContext& Context, const FConcertPlaySessionEvent& Event)
{
	// Forward this notification onto all clients except the one that entered the play session
	{
		TArray<FGuid> NotifyEndpointIds = LiveSession->GetSession().GetSessionClientEndpointIds();
		NotifyEndpointIds.Remove(Context.SourceEndpointId);
		LiveSession->GetSession().SendCustomEvent(Event, NotifyEndpointIds, EConcertMessageFlags::ReliableOrdered);
	}

	if (Event.EventType == EConcertPlaySessionEventType::BeginPlay)
	{
		HandleBeginPlaySession(Event.PlayPackageName, Event.PlayEndpointId, Event.bIsSimulating);
	}
	else if (Event.EventType == EConcertPlaySessionEventType::EndPlay)
	{
		HandleEndPlaySession(Event.PlayPackageName, Event.PlayEndpointId);
	}
	else
	{
		check(Event.EventType == EConcertPlaySessionEventType::SwitchPlay);
		HandleSwitchPlaySession(Event.PlayPackageName, Event.PlayEndpointId);
	}
}

EConcertSessionResponseCode FConcertServerWorkspace::HandleResourceLockRequest(const FConcertSessionContext& Context, const FConcertResourceLockRequest& Request, FConcertResourceLockResponse& Response)
{
	check(Context.SourceEndpointId == Request.ClientId);
	Response.LockType = Request.LockType;

	switch (Response.LockType)
	{
	case EConcertResourceLockType::Lock:
		LockWorkspaceResources(Request.ResourceNames, Request.ClientId, EConcertLockFlags::Explicit, &Response.FailedResources);
		break;
	case EConcertResourceLockType::Unlock:
		UnlockWorkspaceResources(Request.ResourceNames, Request.ClientId, EConcertLockFlags::Explicit, &Response.FailedResources);
		break;
	default:
		return EConcertSessionResponseCode::InvalidRequest;
		break;
	}
	return EConcertSessionResponseCode::Success;
}

void FConcertServerWorkspace::HandleBeginPlaySession(const FName InPlayPackageName, const FGuid& InEndpointId, bool bIsSimulating)
{
	TArray<FPlaySessionInfo>& PlaySessionEndpoints = ActivePlaySessions.FindOrAdd(InPlayPackageName);
	PlaySessionEndpoints.AddUnique({InEndpointId, bIsSimulating});
}

void FConcertServerWorkspace::HandleSwitchPlaySession(const FName InPlayPackageName, const FGuid& InEndpointId)
{
	// The client has toggled between PIE/SIE play type.
	if (TArray<FPlaySessionInfo>* PlaySessionInfo = ActivePlaySessions.Find(InPlayPackageName))
	{
		if (FPlaySessionInfo* PlayInfo = PlaySessionInfo->FindByPredicate([InEndpointId](const FPlaySessionInfo& Info) { return InEndpointId == Info.EndpointId; }))
		{
			PlayInfo->bIsSimulating = !PlayInfo->bIsSimulating; // Toggle the status.
		}
	}
}

void FConcertServerWorkspace::HandleEndPlaySession(const FName InPlayPackageName, const FGuid& InEndpointId)
{
	bool bDiscardPackage = false;
	if (TArray<FPlaySessionInfo>* PlaySessionInfo = ActivePlaySessions.Find(InPlayPackageName))
	{
		PlaySessionInfo->RemoveAll([InEndpointId](const FPlaySessionInfo& Info) {return Info.EndpointId == InEndpointId; });
		if (PlaySessionInfo->Num() == 0)
		{
			bDiscardPackage = true;
			ActivePlaySessions.Remove(InPlayPackageName);
		}
	}

	if (bDiscardPackage)
	{
		// Save a dummy package to discard the live transactions for the previous play world
		// Play worlds are never saved, so we don't have to worry about migrating over the previous data here
		FConcertSyncPackageActivity DummyPackageActivity;
		DummyPackageActivity.EndpointId = InEndpointId;
		DummyPackageActivity.EventData.Package.Info.PackageUpdateType = EConcertPackageUpdateType::Dummy;
		DummyPackageActivity.EventData.Package.Info.PackageName = InPlayPackageName;
		LiveSession->GetSessionDatabase().GetTransactionMaxEventId(DummyPackageActivity.EventData.Package.Info.TransactionEventIdAtSave);
		DummyPackageActivity.EventSummary.SetTypedPayload(FConcertSyncPackageActivitySummary::CreateSummaryForEvent(DummyPackageActivity.EventData));
		AddPackageActivity(DummyPackageActivity);
	}
}

void FConcertServerWorkspace::HandleEndPlaySessions(const FGuid& InEndpointId)
{
	FName PlayPackageName = FindPlaySession(InEndpointId);
	if (!PlayPackageName.IsNone())
	{
		HandleEndPlaySession(PlayPackageName, InEndpointId);

#if DO_CHECK
		// Verify that there are no sessions left using this endpoint (they should only ever end up in a single session)
		PlayPackageName = FindPlaySession(InEndpointId);
		ensureAlwaysMsgf(PlayPackageName.IsNone(), TEXT("Endpoint '%s' has in multiple play sessions!"), *InEndpointId.ToString());
#endif
	}
}

FName FConcertServerWorkspace::FindPlaySession(const FGuid& InEndpointId)
{
	for (const auto& ActivePlaySessionPair : ActivePlaySessions)
	{
		if (ActivePlaySessionPair.Value.ContainsByPredicate([InEndpointId](const FPlaySessionInfo& Info){ return Info.EndpointId == InEndpointId; }))
		{
			return ActivePlaySessionPair.Key;
		}
	}
	return FName();
}

bool FConcertServerWorkspace::LockWorkspaceResource(const FName InResourceName, const FGuid& InLockEndpointId, EConcertLockFlags InLockFlags)
{
	if (!LockedResources)
	{
		return true;
	}

	FLockOwner& Owner = LockedResources->FindOrAdd(InResourceName);
	if (!Owner.EndpointId.IsValid() || EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Force))
	{
		Owner.EndpointId = InLockEndpointId;
		Owner.bExplicit = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Explicit);
		Owner.bTemporary = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Temporary);

		// Add lock activity and notify for non-temporary locks
		if (!Owner.bTemporary)
		{
			FConcertSyncLockActivity LockActivity;
			LockActivity.EndpointId = InLockEndpointId;
			LockActivity.EventData.LockEventType = EConcertSyncLockEventType::Locked;
			LockActivity.EventData.ResourceNames.Add(InResourceName);
			LockActivity.EventSummary.SetTypedPayload(FConcertSyncLockActivitySummary::CreateSummaryForEvent(LockActivity.EventData));
			AddLockActivity(LockActivity);

			FConcertResourceLockEvent LockEvent{ InLockEndpointId, {InResourceName}, EConcertResourceLockType::Lock };
			LiveSession->GetSession().SendCustomEvent(LockEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
		}
	}
	return Owner.EndpointId == InLockEndpointId;
}

bool FConcertServerWorkspace::LockWorkspaceResources(const TArray<FName>& InResourceNames, const FGuid& InLockEndpointId, EConcertLockFlags InLockFlags, TMap<FName, FGuid>* OutFailedRessources)
{
	if (!LockedResources)
	{
		return true;
	}

	const bool bForce = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Force);
	const bool bExplicit = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Explicit);
	const bool bTemporary = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Temporary);

	int32 AcquiredLockCount = 0;
	FConcertResourceLockEvent LockEvent{ InLockEndpointId, {}, EConcertResourceLockType::Lock };
	for (const FName& ResourceName : InResourceNames)
	{
		FLockOwner* Owner = LockedResources->Find(ResourceName);
		if (Owner == nullptr || bForce)
		{
			LockEvent.ResourceNames.Add(ResourceName);
			++AcquiredLockCount;
		}
		else if (Owner->EndpointId == InLockEndpointId)
		{
			++AcquiredLockCount;
		}
		else if (OutFailedRessources != nullptr)
		{
			OutFailedRessources->Add(ResourceName, Owner->EndpointId);
		}
	}

	// if the operation was successful and any new locks were acquired, add them and send an update
	const bool bSuccess = AcquiredLockCount == InResourceNames.Num();
	if (bSuccess && LockEvent.ResourceNames.Num() > 0)
	{
		for (const FName& ResourceName : LockEvent.ResourceNames)
		{
			LockedResources->Add(ResourceName, { InLockEndpointId, bExplicit, bTemporary });
		}

		// Add lock activity and notify for non-temporary locks
		if (!bTemporary)
		{
			FConcertSyncLockActivity LockActivity;
			LockActivity.EndpointId = InLockEndpointId;
			LockActivity.EventData.LockEventType = EConcertSyncLockEventType::Locked;
			LockActivity.EventData.ResourceNames = LockEvent.ResourceNames;
			LockActivity.EventSummary.SetTypedPayload(FConcertSyncLockActivitySummary::CreateSummaryForEvent(LockActivity.EventData));
			AddLockActivity(LockActivity);

			LiveSession->GetSession().SendCustomEvent(LockEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
		}
	}

	return bSuccess;
}

bool FConcertServerWorkspace::UnlockWorkspaceResource(const FName InResourceName, const FGuid& InLockEndpointId, EConcertLockFlags InLockFlags)
{
	if (!LockedResources)
	{
		return true;
	}

	const bool bForce = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Force);
	const bool bExplicit = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Explicit);

	if (const FLockOwner* Owner = LockedResources->Find(InResourceName))
	{
		if (Owner->EndpointId == InLockEndpointId || bForce)
		{
			if (!Owner->bExplicit || bExplicit || bForce)
			{
				const bool bWasTemporary = Owner->bTemporary;
				LockedResources->Remove(InResourceName);
				Owner = nullptr;

				// Add lock activity and notify for non-temporary locks
				if (!bWasTemporary)
				{
					FConcertSyncLockActivity LockActivity;
					LockActivity.EndpointId = InLockEndpointId;
					LockActivity.EventData.LockEventType = EConcertSyncLockEventType::Unlocked;
					LockActivity.EventData.ResourceNames.Add(InResourceName);
					LockActivity.EventSummary.SetTypedPayload(FConcertSyncLockActivitySummary::CreateSummaryForEvent(LockActivity.EventData));
					AddLockActivity(LockActivity);

					FConcertResourceLockEvent LockEvent{ InLockEndpointId, {InResourceName}, EConcertResourceLockType::Unlock };
					LiveSession->GetSession().SendCustomEvent(LockEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
				}
			}
			return true;
		}
	}

	return false;
}

bool FConcertServerWorkspace::UnlockWorkspaceResources(const TArray<FName>& InResourceNames, const FGuid& InLockEndpointId, EConcertLockFlags InLockFlags, TMap<FName, FGuid>* OutFailedRessources)
{
	if (!LockedResources)
	{
		return true;
	}

	const bool bForce = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Force);
	const bool bExplicit = EnumHasAnyFlags(InLockFlags, EConcertLockFlags::Explicit);

	int32 ReleasedLockCount = 0;
	FConcertResourceLockEvent LockEvent{ InLockEndpointId, {}, EConcertResourceLockType::Unlock };
	for (const FName& ResourceName : InResourceNames)
	{
		const FLockOwner Owner = LockedResources->FindRef(ResourceName);
		if (Owner.EndpointId == InLockEndpointId || bForce)
		{
			if (Owner.bExplicit == bExplicit || bForce)
			{
				if (!Owner.bTemporary)
				{
					LockEvent.ResourceNames.Add(ResourceName);
				}
				LockedResources->Remove(ResourceName);
			}
			++ReleasedLockCount;
		}
		else if (OutFailedRessources != nullptr)
		{
			OutFailedRessources->Add(ResourceName, Owner.EndpointId);
		}
	}

	// Add lock activity and notify for non-temporary locks
	if (LockEvent.ResourceNames.Num() > 0)
	{
		FConcertSyncLockActivity LockActivity;
		LockActivity.EndpointId = InLockEndpointId;
		LockActivity.EventData.LockEventType = EConcertSyncLockEventType::Unlocked;
		LockActivity.EventData.ResourceNames = LockEvent.ResourceNames;
		LockActivity.EventSummary.SetTypedPayload(FConcertSyncLockActivitySummary::CreateSummaryForEvent(LockActivity.EventData));
		AddLockActivity(LockActivity);

		LiveSession->GetSession().SendCustomEvent(LockEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
	}

	return ReleasedLockCount == InResourceNames.Num();
}

void FConcertServerWorkspace::UnlockAllWorkspaceResources(const FGuid& InLockEndpointId)
{
	if (!LockedResources)
	{
		return;
	}

	FConcertResourceLockEvent LockEvent;
	for (auto It = LockedResources->CreateIterator(); It; ++It)
	{
		if (It->Value.EndpointId == InLockEndpointId)
		{
			LockEvent.ResourceNames.Add(It->Key);
			It.RemoveCurrent();
		}
	}
	// Notify lock state change
	if (LockEvent.ResourceNames.Num() > 0)
	{
		// Add lock activity
		{
			FConcertSyncLockActivity LockActivity;
			LockActivity.EndpointId = InLockEndpointId;
			LockActivity.EventData.LockEventType = EConcertSyncLockEventType::Unlocked;
			LockActivity.EventData.ResourceNames = LockEvent.ResourceNames;
			LockActivity.EventSummary.SetTypedPayload(FConcertSyncLockActivitySummary::CreateSummaryForEvent(LockActivity.EventData));
			AddLockActivity(LockActivity);
		}

		LockEvent.ClientId = InLockEndpointId;
		LockEvent.LockType = EConcertResourceLockType::Unlock;
		LiveSession->GetSession().SendCustomEvent(LockEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered);
	}
}

bool FConcertServerWorkspace::IsWorkspaceResourceLocked(const FName InResourceName, const FGuid& InLockEndpointId) const
{
	if (!LockedResources)
	{
		return true;
	}

	const FLockOwner* Owner = LockedResources->Find(InResourceName);
	return Owner && Owner->EndpointId == InLockEndpointId;
}

void FConcertServerWorkspace::SetEndpoint(const FGuid& InEndpointId, const FConcertSyncEndpointData& InEndpointData)
{
	// Update this endpoint and sync it
	if (LiveSession->GetSessionDatabase().SetEndpoint(InEndpointId, InEndpointData))
	{
		SyncCommandQueue->QueueCommand(LiveSyncEndpoints, [this, SyncEndpointId = InEndpointId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncEndpointEvent(InEndpointId, SyncEndpointId, InSyncCommandContext.GetNumRemainingCommands());
		});
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set endpoint '%s' on live session '%s': %s"), *InEndpointId.ToString(), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::SendSyncEndpointEvent(const FGuid& InTargetEndpointId, const FGuid& InSyncEndpointId, const int32 InNumRemainingSyncEvents) const
{
	FConcertWorkspaceSyncEndpointEvent SyncEvent;
	SyncEvent.NumRemainingSyncEvents = InNumRemainingSyncEvents;
	SyncEvent.Endpoint.EndpointId = InSyncEndpointId;
	if (LiveSession->GetSessionDatabase().GetEndpoint(InSyncEndpointId, SyncEvent.Endpoint.EndpointData))
	{
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InTargetEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to get endpoint '%s' from live session '%s': %s"), *InSyncEndpointId.ToString(), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::AddConnectionActivity(const FConcertSyncConnectionActivity& InConnectionActivity)
{
	// Add the activity and sync it
	int64 ActivityId = 0;
	int64 EventId = 0;
	if (LiveSession->GetSessionDatabase().AddConnectionActivity(InConnectionActivity, ActivityId, EventId))
	{
		PostActivityAdded(ActivityId);
		SyncCommandQueue->QueueCommand(LiveSyncEndpoints, [this, SyncActivityId = ActivityId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncConnectionActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
		});
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set connection activity '%s' on live session '%s': %s"), *LexToString(ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::SendSyncConnectionActivityEvent(const FGuid& InTargetEndpointId, const int64 InSyncActivityId, const int32 InNumRemainingSyncEvents) const
{
	FConcertSyncConnectionActivity SyncActivity;
	if (LiveSession->GetSessionDatabase().GetConnectionActivity(InSyncActivityId, SyncActivity))
	{
		FConcertWorkspaceSyncActivityEvent SyncEvent;
		SyncEvent.NumRemainingSyncEvents = InNumRemainingSyncEvents;
		SyncEvent.Activity.SetTypedPayload(SyncActivity);
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InTargetEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to get connection activity '%s' from live session '%s': %s"), *LexToString(InSyncActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::AddLockActivity(const FConcertSyncLockActivity& InLockActivity)
{
	// Add the activity and sync it
	int64 ActivityId = 0;
	int64 EventId = 0;
	if (LiveSession->GetSessionDatabase().AddLockActivity(InLockActivity, ActivityId, EventId))
	{
		PostActivityAdded(ActivityId);
		SyncCommandQueue->QueueCommand(LiveSyncEndpoints, [this, SyncActivityId = ActivityId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncLockActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
		});
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set lock activity '%s' on live session '%s': %s"), *LexToString(ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::SendSyncLockActivityEvent(const FGuid& InTargetEndpointId, const int64 InSyncActivityId, const int32 InNumRemainingSyncEvents) const
{
	FConcertSyncLockActivity SyncActivity;
	if (LiveSession->GetSessionDatabase().GetLockActivity(InSyncActivityId, SyncActivity))
	{
		FConcertWorkspaceSyncActivityEvent SyncEvent;
		SyncEvent.NumRemainingSyncEvents = InNumRemainingSyncEvents;
		SyncEvent.Activity.SetTypedPayload(SyncActivity);
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InTargetEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to get lock activity '%s' from live session '%s': %s"), *LexToString(InSyncActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::AddTransactionActivity(const FConcertSyncTransactionActivity& InTransactionActivity)
{
	// Add the activity and sync it
	int64 ActivityId = 0;
	int64 EventId = 0;
	if (LiveSession->GetSessionDatabase().AddTransactionActivity(InTransactionActivity, ActivityId, EventId))
	{
		PostActivityAdded(ActivityId);
		SyncCommandQueue->QueueCommand(LiveSyncEndpoints, [this, SyncActivityId = ActivityId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncTransactionActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
		});
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set transaction activity '%s' on live session '%s': %s"), *LexToString(ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::SendSyncTransactionActivityEvent(const FGuid& InTargetEndpointId, const int64 InSyncActivityId, const int32 InNumRemainingSyncEvents, const bool InLiveOnly) const
{
	FConcertSyncTransactionActivity SyncActivity;
	if (LiveSession->GetSessionDatabase().GetActivity(InSyncActivityId, SyncActivity))
	{
		bool bMetaDataOnly = false;
		{
			FConcertSessionFilter SessionFilter;
			SessionFilter.bOnlyLiveData = InLiveOnly;
			bMetaDataOnly = !ConcertSyncSessionDatabaseFilterUtil::TransactionEventPassesFilter(SyncActivity.EventId, SessionFilter, LiveSession->GetSessionDatabase());
		}

		if (!LiveSession->GetSessionDatabase().GetTransactionEvent(SyncActivity.EventId, SyncActivity.EventData, bMetaDataOnly))
		{
			UE_LOG(LogConcert, Error, TEXT("Failed to get transaction event '%s' from live session '%s': %s"), *LexToString(SyncActivity.EventId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
		}

		FConcertWorkspaceSyncActivityEvent SyncEvent;
		SyncEvent.NumRemainingSyncEvents = InNumRemainingSyncEvents;
		SyncEvent.Activity.SetTypedPayload(SyncActivity);
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InTargetEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to get transaction activity '%s' from live session '%s': %s"), *LexToString(InSyncActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::AddPackageActivity(const FConcertSyncPackageActivity& InPackageActivity)
{
	// Add the activity and sync it
	int64 ActivityId = 0;
	int64 EventId = 0;
	if (LiveSession->GetSessionDatabase().AddPackageActivity(InPackageActivity, ActivityId, EventId))
	{
		PostActivityAdded(ActivityId);
		SyncCommandQueue->QueueCommand(LiveSyncEndpoints, [this, SyncActivityId = ActivityId](const FConcertServerSyncCommandQueue::FSyncCommandContext& InSyncCommandContext, const FGuid& InEndpointId)
		{
			SendSyncPackageActivityEvent(InEndpointId, SyncActivityId, InSyncCommandContext.GetNumRemainingCommands());
		});
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set package activity '%s' on live session '%s': %s"), *LexToString(ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::SendSyncPackageActivityEvent(const FGuid& InTargetEndpointId, const int64 InSyncActivityId, const int32 InNumRemainingSyncEvents, const bool InHeadOnly) const
{
	FConcertSyncPackageActivity SyncActivity;
	if (LiveSession->GetSessionDatabase().GetActivity(InSyncActivityId, SyncActivity))
	{
		bool bMetaDataOnly = false;
		{
			FConcertSessionFilter SessionFilter;
			SessionFilter.bOnlyLiveData = InHeadOnly;
			bMetaDataOnly = !ConcertSyncSessionDatabaseFilterUtil::PackageEventPassesFilter(SyncActivity.EventId, SessionFilter, LiveSession->GetSessionDatabase());
		}

		if (!LiveSession->GetSessionDatabase().GetPackageEvent(SyncActivity.EventId, SyncActivity.EventData, bMetaDataOnly))
		{
			UE_LOG(LogConcert, Error, TEXT("Failed to get package event '%s' from live session '%s': %s"), *LexToString(SyncActivity.EventId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
		}

		FConcertWorkspaceSyncActivityEvent SyncEvent;
		SyncEvent.NumRemainingSyncEvents = InNumRemainingSyncEvents;
		SyncEvent.Activity.SetTypedPayload(SyncActivity);
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InTargetEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to get package activity '%s' from live session '%s': %s"), *LexToString(InSyncActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertServerWorkspace::PostActivityAdded(const int64 InActivityId)
{
	FConcertSyncPackageActivity Activity;
	if (LiveSession->GetSessionDatabase().GetActivity(InActivityId, Activity))
	{
		FConcertSyncEndpointData EndpointData;
		if (LiveSession->GetSessionDatabase().GetEndpoint(Activity.EndpointId, EndpointData))
		{
			FStructOnScope ActivitySummary;
			if (Activity.EventSummary.GetPayload(ActivitySummary))
			{
				check(ActivitySummary.GetStruct()->IsChildOf(FConcertSyncActivitySummary::StaticStruct()));
				const FConcertSyncActivitySummary* ActivitySummaryPtr = (FConcertSyncActivitySummary*)ActivitySummary.GetStructMemory();
				UE_LOG(LogConcert, Display, TEXT("Endpoint '%s' produced activity '%s': %s"), *Activity.EndpointId.ToString(), *LexToString(Activity.ActivityId), *ActivitySummaryPtr->ToDisplayText(FText::AsCultureInvariant(EndpointData.ClientInfo.DisplayName)).ToString());
			}
		}
	}
}
