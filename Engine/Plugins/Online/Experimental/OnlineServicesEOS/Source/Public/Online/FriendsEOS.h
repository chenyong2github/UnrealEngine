// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/FriendsCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_friends_types.h"

namespace UE::Online {

class FOnlineServicesEOS;
	
class FFriendsEOS : public FFriendsCommon
{
public:
	FFriendsEOS(FOnlineServicesEOS& InServices);

	virtual void Initialize() override;
	virtual void PreShutdown() override;

	virtual TOnlineAsyncOpHandle<FQueryFriends> QueryFriends(FQueryFriends::Params&& Params) override;
	virtual TOnlineResult<FGetFriends> GetFriends(FGetFriends::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAddFriend> AddFriend(FAddFriend::Params&& Params) override;

protected:
	void OnEOSFriendsUpdate(FOnlineAccountIdHandle LocalUserId, FOnlineAccountIdHandle FriendUserId, EOS_EFriendsStatus PreviousStatus, EOS_EFriendsStatus CurrentStatus);
protected:
	TMap<FOnlineAccountIdHandle, TMap<FOnlineAccountIdHandle, TSharedRef<FFriend>>> FriendsLists;
	EOS_HFriends FriendsHandle = nullptr;

	EOS_NotificationId NotifyFriendsUpdateNotificationId = 0;
};

/* UE::Online */ }
