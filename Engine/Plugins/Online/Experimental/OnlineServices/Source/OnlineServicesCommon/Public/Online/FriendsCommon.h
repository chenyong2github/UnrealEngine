// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Friends.h"
#include "Online/OnlineComponent.h"

namespace UE::Online {

class FOnlineServicesCommon;

class ONLINESERVICESCOMMON_API FFriendsCommon : public TOnlineComponent<IFriends>
{
public:
	using Super = IFriends;

	FFriendsCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void RegisterCommands() override;

	// IFriends
	virtual TOnlineAsyncOpHandle<FQueryFriends> QueryFriends(FQueryFriends::Params&& Params) override;
	virtual TOnlineResult<FGetFriends> GetFriends(FGetFriends::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAddFriend> AddFriend(FAddFriend::Params&& Params) override;
	virtual TOnlineEvent<void(const FFriendsListUpdated&)> OnFriendsListUpdated() override;

protected:
	TOnlineEventCallable<void(const FFriendsListUpdated&)> OnFriendsListUpdatedEvent;
};

/* UE::Online */ }
