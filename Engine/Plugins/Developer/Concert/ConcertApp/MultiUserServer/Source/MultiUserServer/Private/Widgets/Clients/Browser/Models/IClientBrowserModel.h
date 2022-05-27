// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessages.h"

struct FConcertSessionClientInfo;
struct FConcertSessionInfo;
struct FMessageAddress;

namespace UE::MultiUserServer
{
	/** Decouples the UI from the server functions. */
	class IClientBrowserModel
	{
	public:

		/** Gets the IDs of all availabel IDs */
		virtual TSet<FGuid> GetSessions() const = 0;
		/** Gets more info about a session returned by GetSessions */
		virtual TOptional<FConcertSessionInfo> GetSessionInfo(const FGuid& SessionID) const = 0;
		/** Gets the clients connected to a session returned by GetSessions */
		virtual TArray<FConcertSessionClientInfo> GetSessionClients(const FGuid& SessionID) const = 0;
		/** Gets the network address of a given client */
		virtual FMessageAddress GetClientAddress(const FGuid& ClientEndpointId) const = 0; 
		
		DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnClientListChanged, const FGuid& /*SessionId*/, EConcertClientStatus /*UpdateType*/, const FConcertSessionClientInfo& /*ClientInfo*/);
		virtual FOnClientListChanged& OnClientListChanged() = 0;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnSessionListChanged, const FGuid& /*SessionId*/);
		virtual FOnSessionListChanged& OnSessionCreated() = 0;
		virtual FOnSessionListChanged& OnSessionDestroyed() = 0;
		
		virtual ~IClientBrowserModel() = default;
	};
}


