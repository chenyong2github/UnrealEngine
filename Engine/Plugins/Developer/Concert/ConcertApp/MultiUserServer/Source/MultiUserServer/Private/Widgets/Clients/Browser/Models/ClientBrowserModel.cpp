// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClientBrowserModel.h"

#include "ConcertServerEvents.h"
#include "ConcertUtil.h"
#include "IConcertServer.h"
#include "Algo/Transform.h"

UE::MultiUserServer::FClientBrowserModel::FClientBrowserModel(TSharedRef<IConcertServer> InServer)
	: Server(MoveTemp(InServer))
{
	ConcertServerEvents::OnLiveSessionCreated().AddRaw(this, &FClientBrowserModel::OnLiveSessionCreated);
	ConcertServerEvents::OnLiveSessionDestroyed().AddRaw(this, &FClientBrowserModel::OnLiveSessionDestroyed);
	for (const TSharedPtr<IConcertServerSession>& LiveSession : Server->GetLiveSessions())
	{
		SubscribeToClientConnectionEvents(LiveSession.ToSharedRef());
	}
}

UE::MultiUserServer::FClientBrowserModel::~FClientBrowserModel()
{
	ConcertServerEvents::OnLiveSessionCreated().RemoveAll(this);
	ConcertServerEvents::OnLiveSessionDestroyed().RemoveAll(this);
	for (const TSharedPtr<IConcertServerSession>& LiveSession : Server->GetLiveSessions())
	{
		UnsubscribeFromClientConnectionEvents(LiveSession.ToSharedRef());
	}
}

TSet<FGuid> UE::MultiUserServer::FClientBrowserModel::GetSessions() const
{
	const TArray<TSharedPtr<IConcertServerSession>> LiveSessions = Server->GetLiveSessions();
	TSet<FGuid> Result;
	Algo::Transform(LiveSessions, Result, [](const TSharedPtr<IConcertServerSession>& ServerSession){ return ServerSession->GetId(); });
	return Result;
}

TOptional<FConcertSessionInfo> UE::MultiUserServer::FClientBrowserModel::GetSessionInfo(const FGuid& SessionID) const
{
	const TSharedPtr<IConcertServerSession> LiveSession = Server->GetLiveSession(SessionID);
	return LiveSession
		? LiveSession->GetSessionInfo()
		: TOptional<FConcertSessionInfo>();
}

TArray<FConcertSessionClientInfo> UE::MultiUserServer::FClientBrowserModel::GetSessionClients(const FGuid& SessionID) const
{
	return ConcertUtil::GetSessionClients(*Server, SessionID);
}

FMessageAddress UE::MultiUserServer::FClientBrowserModel::GetClientAddress(const FGuid& ClientEndpointId) const
{
	const TSharedPtr<IConcertServerSession> ServerSession = ConcertUtil::GetLiveSessionClientConnectedTo(*Server, ClientEndpointId);
	return ServerSession->GetClientAddress(ClientEndpointId);
}

void UE::MultiUserServer::FClientBrowserModel::OnLiveSessionCreated(bool bSuccess, const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) const
{
	if (bSuccess)
	{
		SubscribeToClientConnectionEvents(InLiveSession);
	}
	
	OnSessionCreatedEvent.Broadcast(InLiveSession->GetId());
}

void UE::MultiUserServer::FClientBrowserModel::OnLiveSessionDestroyed(const IConcertServer& InServer, TSharedRef<IConcertServerSession> InLiveSession) const
{
	OnSessionCreatedEvent.Broadcast(InLiveSession->GetId());
}

void UE::MultiUserServer::FClientBrowserModel::SubscribeToClientConnectionEvents(const TSharedRef<IConcertServerSession>& InLiveSession) const
{
	InLiveSession->OnSessionClientChanged().AddRaw(this, &FClientBrowserModel::OnClientListUpdated);
}

void UE::MultiUserServer::FClientBrowserModel::UnsubscribeFromClientConnectionEvents( const TSharedRef<IConcertServerSession>& InLiveSession) const
{
	InLiveSession->OnSessionClientChanged().RemoveAll(this);
}

void UE::MultiUserServer::FClientBrowserModel::OnClientListUpdated(IConcertServerSession& Session, EConcertClientStatus Status, const FConcertSessionClientInfo& ClientInfo) const
{
	OnClientListChangedEvent.Broadcast(Session.GetId(), Status, ClientInfo);
}
