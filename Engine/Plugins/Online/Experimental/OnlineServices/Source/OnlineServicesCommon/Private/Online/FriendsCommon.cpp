// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/FriendsCommon.h"

#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FFriendsCommon::FFriendsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Friends"), InServices)
{
}

void FFriendsCommon::RegisterCommands()
{
	RegisterCommand(&FFriendsCommon::QueryFriends);
	RegisterCommand(&FFriendsCommon::GetFriends);
}

TOnlineAsyncOpHandle<FQueryFriends> FFriendsCommon::QueryFriends(FQueryFriends::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryFriends> Operation = GetOp<FQueryFriends>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FGetFriends> FFriendsCommon::GetFriends(FGetFriends::Params&& Params)
{
	return TOnlineResult<FGetFriends>(Errors::NotImplemented());
}

TOnlineAsyncOpHandle<FAddFriend> FFriendsCommon::AddFriend(FAddFriend::Params&& Params)
{
	TOnlineAsyncOpRef<FAddFriend> Operation = GetOp<FAddFriend>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineEvent<void(const FFriendsListUpdated&)> FFriendsCommon::OnFriendsListUpdated()
{
	return OnFriendsListUpdatedEvent;
}

/* UE::Online */ }
