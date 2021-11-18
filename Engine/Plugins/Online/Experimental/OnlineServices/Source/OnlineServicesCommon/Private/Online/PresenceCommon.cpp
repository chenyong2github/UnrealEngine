// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/PresenceCommon.h"

#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FPresenceCommon::FPresenceCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Presence"), InServices)
	, Services(InServices)
{
}

TOnlineAsyncOpHandle<FQueryPresence> FPresenceCommon::QueryPresence(FQueryPresence::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryPresence> Operation = GetOp<FQueryPresence>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FGetPresence> FPresenceCommon::GetPresence(FGetPresence::Params&& Params)
{
	return TOnlineResult<FGetPresence>(Errors::NotImplemented());
}

TOnlineAsyncOpHandle<FUpdatePresence> FPresenceCommon::UpdatePresence(FUpdatePresence::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdatePresence> Operation = GetOp<FUpdatePresence>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineEvent<void(const FPresenceUpdated&)> FPresenceCommon::OnPresenceUpdated()
{
	return OnPresenceUpdatedEvent;
}

/* UE::Online */ }
