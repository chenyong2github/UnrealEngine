// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertServerSequencerManager.h"
#include "IConcertSession.h"
#include "ConcertSyncServerLiveSession.h"

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
}

void FConcertServerSequencerManager::UnbindSession()
{
	if (LiveSession)
	{
		LiveSession->GetSession().OnSessionClientChanged().RemoveAll(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerOpenEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerCloseEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertSequencerStateEvent>(this);
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

void FConcertServerSequencerManager::HandleSequencerCloseEvent(const FConcertSessionContext& InEventContext, const FConcertSequencerCloseEvent& InEvent)
{
	FConcertOpenSequencerState* SequencerState = SequencerStates.Find(*InEvent.SequenceObjectPath);
	if (SequencerState)
	{
		SequencerState->ClientEndpointIds.Remove(InEventContext.SourceEndpointId);
		if (SequencerState->ClientEndpointIds.Num() == 0)
		{
			// Forward a normal close event to clients
			FConcertSequencerCloseEvent CloseEvent;
			CloseEvent.bMasterClose = false;
			CloseEvent.SequenceObjectPath = InEvent.SequenceObjectPath;
			LiveSession->GetSession().SendCustomEvent(CloseEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
			SequencerStates.Remove(*InEvent.SequenceObjectPath);
		}
		// if a sequence was close while it was the master, forward it to client
		else if (InEvent.bMasterClose)
		{
			LiveSession->GetSession().SendCustomEvent(InEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered | EConcertMessageFlags::UniqueId);
		}
	}
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
			if (It->Value.ClientEndpointIds.Num() == 0)
			{
				// Forward the close event to clients
				FConcertSequencerCloseEvent CloseEvent;
				CloseEvent.SequenceObjectPath = It->Key.ToString();
				LiveSession->GetSession().SendCustomEvent(CloseEvent, LiveSession->GetSession().GetSessionClientEndpointIds(), EConcertMessageFlags::ReliableOrdered|EConcertMessageFlags::UniqueId);

				It.RemoveCurrent();
			}
		}
	}
	// Send the current Sequencers states to the newly connected client
	else
	{
		FConcertSequencerStateSyncEvent SyncEvent;
		for (const auto& Pair : SequencerStates)
		{
			SyncEvent.SequencerStates.Add(Pair.Value.State);
		}
		LiveSession->GetSession().SendCustomEvent(SyncEvent, InClientInfo.ClientEndpointId, EConcertMessageFlags::ReliableOrdered);
	}
}
