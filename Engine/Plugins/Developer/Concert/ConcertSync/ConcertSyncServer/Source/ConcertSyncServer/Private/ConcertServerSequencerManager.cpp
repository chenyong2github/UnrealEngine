// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSequencerManager.h"
#include "IConcertSession.h"
#include "ConcertSyncServerLiveSession.h"
#include "ConcertWorkspaceMessages.h"
#include "ConcertLogGlobal.h"

FConcertServerSequencerManager::FConcertServerSequencerManager(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession)
{
	BindSession(InLiveSession);
}

FConcertServerSequencerManager::~FConcertServerSequencerManager()
{
	UnbindSession();
}

void FConcertServerSequencerManager::BindSession(const TSharedRef<FConcertSyncServerLiveSession>& InLiveSession)
{
	check(InLiveSession->IsValidSession());

	UnbindSession();
	LiveSession = InLiveSession;

	LiveSession->GetSession().OnSessionClientChanged().AddRaw(this, &FConcertServerSequencerManager::HandleSessionClientChanged);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertSequencerCloseEvent>(this, &FConcertServerSequencerManager::HandleSequencerCloseEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertSequencerStateEvent>(this, &FConcertServerSequencerManager::HandleSequencerStateEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertSequencerOpenEvent>(this, &FConcertServerSequencerManager::HandleSequencerOpenEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertSequencerTimeAdjustmentEvent>(this, &FConcertServerSequencerManager::HandleSequencerTimeAdjustmentEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncAndFinalizeCompletedEvent>(this, &FConcertServerSequencerManager::HandleWorkspaceSyncAndFinalizeCompletedEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertSequencerPrecacheEvent>(this, &FConcertServerSequencerManager::HandleSequencerPrecacheEvent);
}

void FConcertServerSequencerManager::UnbindSession()
{
	if (LiveSession)
	{
		LiveSession->GetSession().OnSessionClientChanged().RemoveAll(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerCloseEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerStateEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerOpenEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerTimeAdjustmentEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncAndFinalizeCompletedEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerPrecacheEvent>(this);

		LiveSession.Reset();
	}
}

void FConcertServerSequencerManager::HandleSequencerStateEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerStateEvent& InEvent)
{
	// Create or update the Sequencer state 
	FConcertOpenSequencerState& SequencerState = SequencerStates.FindOrAdd(*InEvent.State.SequenceObjectPath);
	SequencerState.ClientEndpointIds.AddUnique(InEventContext.SourceEndpointId);
	SequencerState.State = InEvent.State;

	// Forward the message to the other clients
	TArray<FGuid> ClientIds = LiveSession->GetSession().GetSessionClientEndpointIds();
	ClientIds.Remove(InEventContext.SourceEndpointId);
	LiveSession->GetSession().SendCustomEvent(InEvent, ClientIds, EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
}

void FConcertServerSequencerManager::HandleSequencerOpenEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerOpenEvent& InEvent)
{
	// Create or update the Sequencer state 
	FConcertOpenSequencerState& SequencerState = SequencerStates.FindOrAdd(*InEvent.SequenceObjectPath);
	SequencerState.ClientEndpointIds.AddUnique(InEventContext.SourceEndpointId);
	SequencerState.State.SequenceObjectPath = InEvent.SequenceObjectPath;

	// Forward the message to the other clients
	TArray<FGuid> ClientIds = LiveSession->GetSession().GetSessionClientEndpointIds();
	ClientIds.Remove(InEventContext.SourceEndpointId);
	LiveSession->GetSession().SendCustomEvent(InEvent, ClientIds, EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
}

void FConcertServerSequencerManager::HandleSequencerTimeAdjustmentEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerTimeAdjustmentEvent& InEvent)
{
	// Verify that we have sequencers with the given SequenceObjectPath open.
	//
	FConcertOpenSequencerState* SequencerState = SequencerStates.Find(*InEvent.SequenceObjectPath);
	if (SequencerState)
	{
		// Forward the message to the other clients
		TArray<FGuid> ClientIds = LiveSession->GetSession().GetSessionClientEndpointIds();
		ClientIds.Remove(InEventContext.SourceEndpointId);
		LiveSession->GetSession().SendCustomEvent(InEvent, ClientIds, EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
	}
}

void FConcertServerSequencerManager::HandleSequencerCloseEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerCloseEvent& InEvent)
{
	FConcertOpenSequencerState* SequencerState = SequencerStates.Find(*InEvent.SequenceObjectPath);
	if (SequencerState)
	{
		SequencerState->ClientEndpointIds.Remove(InEventContext.SourceEndpointId);
		// Forward a normal close event to clients
		const int32 NumOpen = SequencerState->ClientEndpointIds.Num();
		FConcertSequencerCloseEvent CloseEvent;
		CloseEvent.bControllerClose = NumOpen != 0 && InEvent.bControllerClose;
		CloseEvent.EditorsWithSequencerOpened = NumOpen;
		CloseEvent.SequenceObjectPath = InEvent.SequenceObjectPath;
		LiveSession->GetSession().SendCustomEvent(CloseEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
		if (NumOpen == 0)
		{
			SequencerStates.Remove(*InEvent.SequenceObjectPath);
		}
	}
}

void FConcertServerSequencerManager::HandleSequencerPrecacheEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerPrecacheEvent& InEvent)
{
	const FGuid& RequestClient = InEventContext.SourceEndpointId;
	const bool bClientWantsPrecached = InEvent.bShouldBePrecached;

	UE_LOG(LogConcert, Verbose,
		TEXT("FConcertServerSequencerManager: Precache request from client %s to %s %u sequences"),
		*RequestClient.ToString(),
		bClientWantsPrecached ? TEXT("add") : TEXT("remove"),
		InEvent.SequenceObjectPaths.Num());

	// Represents the net result to broadcast, if any. Contains only sequences
	// which gained their first, or lost their last, referencer.
	FConcertSequencerPrecacheEvent OutChanges;
	OutChanges.bShouldBePrecached = bClientWantsPrecached;

	for (const FString& SequenceObjectPath : InEvent.SequenceObjectPaths)
	{
		if (bClientWantsPrecached)
		{
			const bool bAddedFirstReferencer = AddSequencePrecacheForClient(RequestClient, SequenceObjectPath);
			if (bAddedFirstReferencer)
			{
				OutChanges.SequenceObjectPaths.Add(SequenceObjectPath);
			}
		}
		else
		{
			const bool bRemovedLastReferencer = RemoveSequencePrecacheForClient(RequestClient, SequenceObjectPath);
			if (bRemovedLastReferencer)
			{
				OutChanges.SequenceObjectPaths.Add(SequenceObjectPath);
			}
		}
	}

	if (OutChanges.SequenceObjectPaths.Num() > 0)
	{
		for (const FString& SequenceObjectPath : OutChanges.SequenceObjectPaths)
		{
			UE_LOG(LogConcert, Verbose,
				TEXT("FConcertServerSequencerManager: Sequence '%s' %s precache set"),
				*SequenceObjectPath,
				bClientWantsPrecached ? TEXT("added to") : TEXT("removed from"));
		}

		LiveSession->GetSession().SendCustomEvent(OutChanges, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
	}
}

void FConcertServerSequencerManager::HandleWorkspaceSyncAndFinalizeCompletedEvent(const FConcertSessionContext& InEventContext, const FConcertWorkspaceSyncAndFinalizeCompletedEvent& InEvent)
{
	FConcertSequencerStateSyncEvent SequencerStateSyncEvent;
	for (const auto& Pair : SequencerStates)
	{
		SequencerStateSyncEvent.SequencerStates.Add(Pair.Value.State);
	}

	LiveSession->GetSession().SendCustomEvent(SequencerStateSyncEvent, InEventContext.SourceEndpointId, EConcertMessageFlags::ReliableOrdered);
}

void FConcertServerSequencerManager::HandleSessionClientChanged(IConcertServerSession& InSession, EConcertClientStatus InClientStatus, const FConcertSessionClientInfo& InClientInfo)
{
	check(&InSession == &LiveSession->GetSession());
	// Remove the client from all open sequences
	if (InClientStatus == EConcertClientStatus::Disconnected)
	{
		for (auto It = SequencerStates.CreateIterator(); It; ++It)
		{
			It->Value.ClientEndpointIds.Remove(InClientInfo.ClientEndpointId);
			const int32 NumOpen = It->Value.ClientEndpointIds.Num();
			// Forward the close event to clients
			FConcertSequencerCloseEvent CloseEvent;
			CloseEvent.EditorsWithSequencerOpened = NumOpen;
			CloseEvent.SequenceObjectPath = It->Key.ToString();
			LiveSession->GetSession().SendCustomEvent(CloseEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered|EConcertMessageFlags::UniqueId);

			if (NumOpen == 0)
			{
				It.RemoveCurrent();
			}
		}
	}

	// Newly connected clients need to be sent the current set of precached sequences.
	// Disconnecting clients need their references removed, which may update other clients.
	if (InClientStatus == EConcertClientStatus::Connected ||
		InClientStatus == EConcertClientStatus::Disconnected)
	{
		FConcertSequencerPrecacheEvent PrecacheEvent;
		TArray<FGuid> EventRecipients;

		if (InClientStatus == EConcertClientStatus::Connected)
		{
			PrecacheEvent.bShouldBePrecached = true;
			EventRecipients.Add(InClientInfo.ClientEndpointId);
		}
		else
		{
			PrecacheEvent.bShouldBePrecached = false;
			EventRecipients = LiveSession->GetSession().GetSessionClientEndpointIds();
		}

		for (TMap<FName, FPrecachingState>::TIterator It = PrecacheStates.CreateIterator(); It; ++It)
		{
			if (InClientStatus == EConcertClientStatus::Connected)
			{
				ensure(It->Value.ReferencingClientEndpoints.Num() > 0);
				PrecacheEvent.SequenceObjectPaths.Add(It->Key.ToString());
			}
			else
			{
				if (It->Value.ReferencingClientEndpoints.Remove(InClientInfo.ClientEndpointId))
				{
					if (It->Value.ReferencingClientEndpoints.Num() == 0)
					{
						PrecacheEvent.SequenceObjectPaths.Add(It->Key.ToString());
						It.RemoveCurrent();
					}
				}
			}
		}

		if (PrecacheEvent.SequenceObjectPaths.Num() > 0)
		{
			for (const FString& SequenceObjectPath : PrecacheEvent.SequenceObjectPaths)
			{
				if (InClientStatus == EConcertClientStatus::Connected)
				{
					UE_LOG(LogConcert, Verbose,
						TEXT("FConcertServerSequencerManager: Client connected; notifying precache set contains sequence '%s'"),
						*SequenceObjectPath);
				}
				else
				{
					UE_LOG(LogConcert, Verbose,
						TEXT("FConcertServerSequencerManager: Client disconnected; last reference to '%s' was released, removed from precache set"),
						*SequenceObjectPath);
				}
			}

			LiveSession->GetSession().SendCustomEvent(PrecacheEvent, EventRecipients, EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
		}
	}

	// Newly connected clients won't be sent the Sequencer state sync event
	// until they have synced and finalized their workspace since an open
	// sequence could have been created by a transaction in the activity
	// stream.
}

bool FConcertServerSequencerManager::AddSequencePrecacheForClient(const FGuid& RequestClient, const FString& SequenceObjectPath)
{
	bool bAddedFirstReferencer = false;

	FPrecachingState& State = PrecacheStates.FindOrAdd(*SequenceObjectPath);
	bool bAlreadyInSet = false;
	State.ReferencingClientEndpoints.Add(RequestClient, &bAlreadyInSet);
	if (!bAlreadyInSet)
	{
		if (State.ReferencingClientEndpoints.Num() == 1)
		{
			bAddedFirstReferencer = true;
		}
	}
	else
	{
		UE_LOG(LogConcert, Warning, TEXT("FConcertServerSequencerManager: Client %s requested redundant add precache for sequence %s"),
			*RequestClient.ToString(), *SequenceObjectPath);
	}

	return bAddedFirstReferencer;
}

bool FConcertServerSequencerManager::RemoveSequencePrecacheForClient(const FGuid& RequestClient, const FString& SequenceObjectPath)
{
	bool bRemovedLastReferencer = false;

	FPrecachingState* MaybeState = PrecacheStates.Find(*SequenceObjectPath);
	if (MaybeState && MaybeState->ReferencingClientEndpoints.Remove(RequestClient))
	{
		if (MaybeState->ReferencingClientEndpoints.Num() == 0)
		{
			// Removed last reference.
			MaybeState = nullptr;
			PrecacheStates.Remove(*SequenceObjectPath);
			bRemovedLastReferencer = true;
		}
	}
	else
	{
		UE_LOG(LogConcert, Warning, TEXT("FConcertServerSequencerManager: Client %s attempted invalid release precache for sequence %s"),
			*RequestClient.ToString(), *SequenceObjectPath);
	}

	return bRemovedLastReferencer;
}
