// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/FriendsCommon.h"

#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FFriendsCommon::FFriendsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Friends"), InServices.AsShared())
	, Services(InServices)
{
}

TOnlineAsyncOpHandle<FQueryFriends> FFriendsCommon::QueryFriends(FQueryFriends::Params&& Params)
{
	TOnlineAsyncOp<FQueryFriends>& Operation = Services.OpCache.GetOp<FQueryFriends>(MoveTemp(Params));
	Operation.SetError(Errors::Unimplemented());
	return Operation.GetHandle();
}

TOnlineResult<FGetFriends::Result> FFriendsCommon::GetFriends(FGetFriends::Params&& Params)
{
	return TOnlineResult<FGetFriends::Result>(Errors::Unimplemented());
}

TOnlineEvent<void(const FFriendsListUpdated&)> FFriendsCommon::OnFriendsListUpdated()
{
	return OnFriendsListUpdatedEvent;
}

/* UE::Online */ }
