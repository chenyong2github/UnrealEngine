// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

/**
 * Friend invite status - if an invite is pending
 */
enum class EFriendInviteStatus : uint8
{
	/** Friend has sent player an invite, but it has not been accepted/rejected */
	PendingInbound,
	/** Player has sent friend an invite, but it has not been accepted/rejected */
	PendingOutbound,
};

const TCHAR* LexToString(EFriendInviteStatus);

/**
 * Information about a friend
 */
struct FFriend
{
	/** Id of the friend */
	FOnlineAccountIdHandle UserId;
	/** Friendship invite status - if an invite is pending */
	TOptional<EFriendInviteStatus> InviteStatus;
};

struct FQueryFriends
{
	static constexpr TCHAR Name[] = TEXT("QueryFriends");

	/** Input struct for Friends::QueryFriends */
	struct Params
	{
		/** Account Id of the local user making the request */
		FOnlineAccountIdHandle LocalUserId;
	};

	/**
	 * Output struct for Friends::QueryFriends
	 * Obtain the friends list via GetFriends
	 */
	struct Result
	{
	};
};

struct FGetFriends
{
	static constexpr TCHAR Name[] = TEXT("GetFriends");

	/** Input struct for Friends::GetFriends */
	struct Params
	{
		/** Account Id of the local user making the request */
		FOnlineAccountIdHandle LocalUserId;
	};

	/** Output struct for Friends::GetFriends */
	struct Result
	{
		/** Array of friends */
		TArray<TSharedRef<FFriend>> Friends;
	};
};

struct FAddFriend
{
	static constexpr TCHAR Name[] = TEXT("AddFriend");

	/** Input struct for Friends::AddFriend */
	struct Params
	{
		/** Account Id of the local user making the request */
		FOnlineAccountIdHandle LocalUserId;
		/** Friend to add */
		FOnlineAccountIdHandle FriendId;
	};

	/** Output struct for Friends::AddFriend */
	struct Result
	{
		/** Added friend */
		TSharedRef<FFriend> Friend;
	};
};

/** Struct for FriendsListUpdated event */
struct FFriendsListUpdated
{
	/** Local user who's friends list changed */
	FOnlineAccountIdHandle LocalUserId;
	/** Account ids of friends that have been added */
	TArrayView<const FOnlineAccountIdHandle> AddedFriends;
	/** Account ids of friends that have been removed */
	TArrayView<const FOnlineAccountIdHandle> RemovedFriends;
	/** Account ids of friends that have been updated */
	TArrayView<const FOnlineAccountIdHandle> UpdatedFriends;
};

class IFriends
{
public:
	/**
	 * Query the friends list
	 * 
	 * @param Params for the QueryFriends call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FQueryFriends> QueryFriends(FQueryFriends::Params&& Params) = 0;

	/**
	 * Get the contents of a previously queried friends list
	 *
	 * @param Params for the GetFriends call
	 * @return
	 */
	virtual TOnlineResult<FGetFriends> GetFriends(FGetFriends::Params&& Params) = 0;

	/**
	 * Add a user to the friends list
	 *
	 * @param Params for the AddFriend call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FAddFriend> AddFriend(FAddFriend::Params&& Params) = 0;

	/**
	 * Get the event that is triggered when a friends list is updated
	 * This typically happens when QueryFriends is called, a friend list modifying function is called (like AddFriend), or an event coming from a backend service
	 *
	 * @return Event that can be bound to
	 */
	virtual TOnlineEvent<void(const FFriendsListUpdated&)> OnFriendsListUpdated() = 0;
};

inline const TCHAR* LexToString(EFriendInviteStatus FriendInviteStatus)
{
	switch (FriendInviteStatus)
	{
	case EFriendInviteStatus::PendingInbound:
		return TEXT("PendingInbound");
	case EFriendInviteStatus::PendingOutbound:
		return TEXT("PendingOutbound");
	}
	return TEXT("Invalid");
}

namespace Meta {
// TODO: Move to Friends_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FFriend)
	ONLINE_STRUCT_FIELD(FFriend, UserId),
	ONLINE_STRUCT_FIELD(FFriend, InviteStatus)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryFriends::Params)
	ONLINE_STRUCT_FIELD(FQueryFriends::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryFriends::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetFriends::Params)
	ONLINE_STRUCT_FIELD(FGetFriends::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetFriends::Result)
	ONLINE_STRUCT_FIELD(FGetFriends::Result, Friends)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAddFriend::Params)
	ONLINE_STRUCT_FIELD(FAddFriend::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FAddFriend::Params, FriendId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAddFriend::Result)
	ONLINE_STRUCT_FIELD(FAddFriend::Result, Friend)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FFriendsListUpdated)
	ONLINE_STRUCT_FIELD(FFriendsListUpdated, LocalUserId),
	ONLINE_STRUCT_FIELD(FFriendsListUpdated, AddedFriends),
	ONLINE_STRUCT_FIELD(FFriendsListUpdated, RemovedFriends),
	ONLINE_STRUCT_FIELD(FFriendsListUpdated, UpdatedFriends)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
