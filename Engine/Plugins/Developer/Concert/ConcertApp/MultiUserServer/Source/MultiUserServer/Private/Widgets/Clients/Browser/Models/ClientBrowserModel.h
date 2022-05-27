// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IClientBrowserModel.h"

class IConcertServerSession;
class IConcertServer;

namespace UE::MultiUserServer
{
	class FClientBrowserModel : public IClientBrowserModel
	{
	public:

		FClientBrowserModel(TSharedRef<IConcertServer> InServer);
		virtual ~FClientBrowserModel() override;

		//~ Begin IClientBrowserModel Interface
		virtual TSet<FGuid> GetSessions() const override;
		virtual TOptional<FConcertSessionInfo> GetSessionInfo(const FGuid& SessionID) const override;
		virtual TArray<FConcertSessionClientInfo> GetSessionClients(const FGuid& SessionID) const override;
		virtual FMessageAddress GetClientAddress(const FGuid& ClientEndpointId) const override;
		virtual FOnClientListChanged& OnClientListChanged() override { return OnClientListChangedEvent; }
		virtual FOnSessionListChanged& OnSessionCreated() override { return OnSessionCreatedEvent; }
		virtual FOnSessionListChanged& OnSessionDestroyed() override { return OnSessionDestroyedEvent; }
		//~ End IClientBrowserModel Interface

	private:

		TSharedRef<IConcertServer> Server;

		FOnClientListChanged OnClientListChangedEvent;
		FOnSessionListChanged OnSessionCreatedEvent;
		FOnSessionListChanged OnSessionDestroyedEvent;

		void OnLiveSessionCreated(bool bSuccess, const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) const;
		void OnLiveSessionDestroyed(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) const;
		
		void SubscribeToClientConnectionEvents(const TSharedRef<IConcertServerSession>& InLiveSession) const;
		void UnsubscribeFromClientConnectionEvents(const TSharedRef<IConcertServerSession>& InLiveSession) const;
		void OnClientListUpdated(IConcertServerSession& Session, EConcertClientStatus Status, const FConcertSessionClientInfo& ClientInfo) const;
	};
}


